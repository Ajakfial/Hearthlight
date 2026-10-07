#include "DownloadManager.h"

#include "Constants.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

#include <memory>
#include <vector>

// A single-file download implemented as a Task. Performs blocking network I/O
// on the Task worker thread using a local QNetworkAccessManager + QEventLoop,
// so the GUI thread is never blocked.
class FileDownloadTask : public Task {
    Q_OBJECT
public:
    FileDownloadTask(DownloadManager *mgr, DownloadRequest req)
        : Task(QStringLiteral("Download %1").arg(req.url.fileName()), nullptr)
        , m_req(std::move(req))
    {
        setMaxRetries(3);
    }

protected:
    bool execute(Context &ctx) override
    {
        using namespace Hearthlight;
        if (NetworkStatus::instance().isEffectivelyOffline()) {
            setDetails(tr("Offline — download unavailable"));
            m_errorText = tr("You appear to be offline. This download needs an internet connection.");
            setError(m_errorText);
            return false;
        }
        const QString partPath = m_req.destPath + QStringLiteral(".part");
        QDir().mkpath(QFileInfo(m_req.destPath).absolutePath());

        const int maxAttempts = maxRetries() + 1;
        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            if (ctx.isCancelled()) {
                setError(tr("Cancelled"));
                return false;
            }
            m_errorText.clear();
            if (tryOnce(partPath, ctx)) {
                return true;
            }
            setError(m_errorText.isEmpty() ? tr("Download failed") : m_errorText);
            if (ctx.isCancelled()) {
                setError(tr("Cancelled"));
                return false;
            }
            if (attempt + 1 < maxAttempts) {
                // Exponential backoff: 1s, 2s, 4s…
                const int waitMs = 1000 * (1 << attempt);
                ctx.report(0, 100, tr("Retrying in %1s… (%2)").arg(waitMs / 1000).arg(m_errorText));
                QThread::msleep(static_cast<unsigned long>(waitMs));
            }
        }
        return false;
    }

    QString m_errorText; // surfaced via errorString()

private:
    bool tryOnce(const QString &partPath, Context &ctx)
    {
        QNetworkAccessManager nam;
        qint64 resumeFrom = 0;
        if (m_req.resume && QFile::exists(partPath)) {
            resumeFrom = QFileInfo(partPath).size();
        }

        QNetworkRequest request(m_req.url);
        request.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (resumeFrom > 0) {
            request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(resumeFrom) + "-");
        }
        for (const auto &h : m_req.extraRequest.rawHeaderList()) {
            request.setRawHeader(h, m_req.extraRequest.rawHeader(h));
        }

        QNetworkReply *reply = nam.get(request);

        QFile out(partPath);
        QIODevice::OpenMode mode = QIODevice::WriteOnly;
        bool serverHonoursRange = false;
        // We must know the server's response before choosing Append vs Truncate,
        // so wait for headers first.
        QEventLoop headersLoop;
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &headersLoop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &headersLoop, &QEventLoop::quit);
        // Pump cancellation while waiting for headers.
        QTimer cancelPoll;
        cancelPoll.setInterval(100);
        QObject::connect(&cancelPoll, &QTimer::timeout, [&] {
            if (ctx.isCancelled()) {
                reply->abort();
            }
        });
        cancelPoll.start();
        headersLoop.exec();
        cancelPoll.stop();

        if (ctx.isCancelled()) {
            delete reply;
            return false;
        }
        if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError) {
            // reply->finished may have fired with an immediate error (DNS, offline…)
            if (reply->error() == QNetworkReply::UnknownNetworkError
                || reply->error() == QNetworkReply::HostNotFoundError) {
                m_errorText = tr("No internet connection (%1).").arg(reply->errorString());
            } else {
                m_errorText = reply->errorString();
            }
            delete reply;
            return false;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 206) {
            serverHonoursRange = true;
        } else if (status == 200 && resumeFrom > 0) {
            resumeFrom = 0; // server ignored Range; restart
        }
        if (status >= 400) {
            m_errorText = tr("Server error %1").arg(status);
            delete reply;
            return false;
        }

        mode |= (serverHonoursRange && resumeFrom > 0) ? QIODevice::Append : QIODevice::Truncate;
        if (!out.open(mode)) {
            m_errorText = tr("Cannot write %1").arg(partPath);
            delete reply;
            return false;
        }

        const qint64 contentLen = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const qint64 total = (contentLen >= 0) ? (contentLen + (serverHonoursRange ? resumeFrom : 0)) : -1;

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::readyRead, [&] {
            out.write(reply->readAll());
            ctx.report(out.size() + (serverHonoursRange ? 0 : 0), total,
                       tr("Downloading %1…").arg(m_req.url.fileName()));
        });
        QObject::connect(reply, &QNetworkReply::downloadProgress,
                         [&](qint64 received, qint64 t) { ctx.report(received + resumeFrom, t > 0 ? t + resumeFrom : -1); });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer poll;
        poll.setInterval(100);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            if (ctx.isCancelled()) {
                reply->abort();
            }
        });
        poll.start();
        // Flush any data that arrived before we connected readyRead.
        if (reply->bytesAvailable() > 0) {
            out.write(reply->readAll());
        }
        if (!reply->isFinished()) {
            loop.exec();
        }
        poll.stop();

        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (!ok) {
            m_errorText = ctx.isCancelled() ? tr("Cancelled") : reply->errorString();
        }
        // Drain tail.
        if (reply->bytesAvailable() > 0) {
            out.write(reply->readAll());
        }
        out.close();
        delete reply;
        if (!ok) {
            return false;
        }

        // Verify hashes (streamed from the .part file).
        if (!verify(partPath)) {
            QFile::remove(partPath); // corrupt/incomplete: restart from scratch
            return false;
        }
        // Atomic publish: .part -> dest
        QFile::remove(m_req.destPath);
        if (!QFile::rename(partPath, m_req.destPath)) {
            // Cross-device fallback.
            if (!QFile::copy(partPath, m_req.destPath)) {
                m_errorText = tr("Cannot move finished download into place.");
                return false;
            }
            QFile::remove(partPath);
        }
        ctx.report(total > 0 ? total : 1, total > 0 ? total : 1, tr("Done"));
        Logger::info(QStringLiteral("Downloaded %1 -> %2").arg(m_req.url.toString()).arg(m_req.destPath));
        return true;
    }

    bool verify(const QString &path)
    {
        auto check = [&](QCryptographicHash::Algorithm algo, const QByteArray &expectedHex, const char *name) -> bool {
            if (expectedHex.isEmpty()) {
                return true;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                m_errorText = tr("Cannot read downloaded file for verification.");
                return false;
            }
            QCryptographicHash h(algo);
            while (!f.atEnd()) {
                h.addData(f.read(1 << 20));
            }
            const QByteArray actual = h.result().toHex();
            if (actual.compare(expectedHex.toLower(), Qt::CaseInsensitive) != 0) {
                m_errorText = tr("%1 mismatch for %2 (expected %3, got %4). The file may be corrupt; it will be re-downloaded.")
                                  .arg(QString::fromLatin1(name))
                                  .arg(m_req.url.fileName())
                                  .arg(QString::fromLatin1(expectedHex))
                                  .arg(QString::fromLatin1(actual));
                Logger::warning(m_errorText);
                return false;
            }
            return true;
        };
        return check(QCryptographicHash::Sha1, m_req.expectedSha1, "SHA-1")
            && check(QCryptographicHash::Sha256, m_req.expectedSha256, "SHA-256")
            && check(QCryptographicHash::Sha512, m_req.expectedSha512, "SHA-512");
    }

    DownloadRequest m_req;
};

DownloadManager::DownloadManager(const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , m_cacheDir(cacheDir)
{
    QDir().mkpath(cacheDir);
}

void DownloadManager::setMaxParallel(int n)
{
    m_maxParallel = qBound(1, n, 32);
    emit queueChanged();
}

qint64 DownloadManager::cacheSize(const QString &cacheDir)
{
    qint64 total = 0;
    QDir dir(cacheDir);
    const auto infos = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &fi : infos) {
        total += fi.size();
    }
    return total;
}

void DownloadManager::clearCache(const QString &cacheDir)
{
    QDir dir(cacheDir);
    const auto infos = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &fi : infos) {
        QFile::remove(fi.absoluteFilePath());
    }
    Logger::info(QStringLiteral("Download cache cleared: %1").arg(cacheDir));
}

Task *DownloadManager::enqueue(const DownloadRequest &req, QObject *owner)
{
    auto *t = new FileDownloadTask(this, req);
    if (owner) {
        t->setParent(owner);
    }
    emit queueChanged();
    return t;
}

bool DownloadManager::verifyHashes(const QString &path, const QByteArray &sha1, const QByteArray &sha256,
                                   const QByteArray &sha512, QString *error)
{
    auto check = [&](QCryptographicHash::Algorithm algo, const QByteArray &expectedHex) -> bool {
        if (expectedHex.isEmpty()) {
            return true;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("Cannot read %1 for verification.").arg(path);
            }
            return false;
        }
        QCryptographicHash h(algo);
        while (!f.atEnd()) {
            h.addData(f.read(1 << 20));
        }
        const QByteArray actual = h.result().toHex();
        if (actual.compare(expectedHex.toLower(), Qt::CaseInsensitive) != 0) {
            if (error) {
                *error = QStringLiteral("Checksum mismatch for %1.").arg(QFileInfo(path).fileName());
            }
            return false;
        }
        return true;
    };
    return check(QCryptographicHash::Sha1, sha1) && check(QCryptographicHash::Sha256, sha256)
        && check(QCryptographicHash::Sha512, sha512);
}

bool DownloadManager::fileUpToDate(const QString &path, const DownloadRequest &req)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() <= 0) {
        return false;
    }
    if (req.expectedSize >= 0 && fi.size() != req.expectedSize) {
        return false;
    }
    if (req.expectedSha1.isEmpty() && req.expectedSha256.isEmpty() && req.expectedSha512.isEmpty()) {
        return true;
    }
    return verifyHashes(path, req.expectedSha1, req.expectedSha256, req.expectedSha512, nullptr);
}

bool DownloadManager::publishFile(const QString &partPath, const QString &destPath, QString *error)
{
    QFile::remove(destPath);
    if (QFile::rename(partPath, destPath)) {
        return true;
    }
    // Cross-device fallback.
    if (QFile::copy(partPath, destPath)) {
        QFile::remove(partPath);
        return true;
    }
    if (error) {
        *error = QStringLiteral("Cannot move finished download into place: %1").arg(destPath);
    }
    return false;
}

namespace {
// One in-flight transfer inside downloadManyBlocking (move-only: held in a
// std::vector with reserved capacity, so no reallocation mid-wave).
struct BulkJob {
    DownloadRequest req;
    QString part;
    std::unique_ptr<QFile> out;
    QNetworkReply *reply = nullptr;
    qint64 resumeFrom = 0;
    bool rangeHonoured = false;
    bool finished = false;
    bool ok = false;
    QString err;
    int attempts = 0;
};
} // namespace

bool DownloadManager::downloadManyBlocking(const QList<DownloadRequest> &reqs, int maxParallel, Task::Context &ctx,
                                           QString *error)
{
    const int total = reqs.size();
    if (total == 0) {
        return true;
    }
    maxParallel = qBound(1, maxParallel, 32);

    // Fast path: everything cached (also the offline story).
    int done = 0;
    QList<DownloadRequest> pending;
    pending.reserve(total);
    for (const auto &r : reqs) {
        if (fileUpToDate(r.destPath, r)) {
            ++done;
        } else {
            pending.append(r);
        }
    }
    ctx.report(done, total, QObject::tr("%1 of %2 files ready").arg(done).arg(total));
    if (pending.isEmpty()) {
        return true;
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        if (error) {
            *error = QObject::tr("You're offline and %1 file(s) still need downloading (e.g. %2). "
                                 "Go online once to finish, then everything stays usable offline.")
                         .arg(pending.size())
                         .arg(pending.first().url.fileName());
        }
        return false;
    }

    constexpr int kMaxAttempts = 4; // initial + 3 retries
    QString firstError;
    QHash<QString, int> attemptCount;
    attemptCount.reserve(pending.size());
    int failWaves = 0;

    while (!pending.isEmpty()) {
        if (ctx.isCancelled()) {
            if (error) {
                *error = QObject::tr("Cancelled");
            }
            return false;
        }
        // Take one wave (vector + reserve: element addresses stay stable
        // while signal handlers hold references to them).
        std::vector<BulkJob> wave;
        wave.reserve(static_cast<size_t>(maxParallel));
        while (!pending.isEmpty() && wave.size() < static_cast<size_t>(maxParallel)) {
            BulkJob j;
            j.req = pending.takeFirst();
            j.part = j.req.destPath + QStringLiteral(".part");
            j.attempts = attemptCount.value(j.req.destPath, 0);
            j.out = std::make_unique<QFile>();
            QDir().mkpath(QFileInfo(j.req.destPath).absolutePath());
            wave.push_back(std::move(j));
        }
        auto openOut = [](BulkJob &j, QIODevice::OpenMode mode) {
            if (j.out->isOpen()) {
                j.out->close();
            }
            j.out->setFileName(j.part);
            j.out->open(mode);
        };
        ctx.report(done, total, QObject::tr("Downloading %1…").arg(wave.front().req.url.fileName()));

        QNetworkAccessManager nam;
        QEventLoop loop;
        int remaining = wave.size();
        QTimer poll;
        poll.setInterval(100);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            if (ctx.isCancelled()) {
                for (auto &j : wave) {
                    if (j.reply && !j.finished) {
                        j.reply->abort();
                    }
                }
            }
        });

        // Open files + issue GETs.
        bool waveAborted = false;
        for (auto &j : wave) {
            if (j.req.resume && QFile::exists(j.part) && QFileInfo(j.part).size() > 0) {
                j.resumeFrom = QFileInfo(j.part).size();
            }
            QNetworkRequest request(j.req.url);
            request.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
            if (j.resumeFrom > 0) {
                request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(j.resumeFrom) + "-");
            }
            for (const auto &h : j.req.extraRequest.rawHeaderList()) {
                request.setRawHeader(h, j.req.extraRequest.rawHeader(h));
            }
            j.reply = nam.get(request);
            // Decide append vs truncate once headers arrive (before any body).
            QObject::connect(j.reply, &QNetworkReply::metaDataChanged, [&j, &openOut] {
                const int status = j.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                j.rangeHonoured = (status == 206);
                if (j.rangeHonoured && j.resumeFrom > 0) {
                    if (!j.out->isOpen()) {
                        openOut(j, QIODevice::WriteOnly | QIODevice::Append);
                    }
                    return;
                }
                // Fresh download (or server ignored Range): truncate.
                j.resumeFrom = 0;
                openOut(j, QIODevice::WriteOnly | QIODevice::Truncate);
            });
            QObject::connect(j.reply, &QNetworkReply::readyRead, [&j] {
                if (j.out->isOpen()) {
                    j.out->write(j.reply->readAll());
                }
            });
            QObject::connect(j.reply, &QNetworkReply::finished, [&, rep = j.reply] {
                BulkJob *job = nullptr;
                for (auto &jj : wave) {
                    if (jj.reply == rep) {
                        job = &jj;
                        break;
                    }
                }
                if (job && !job->finished) {
                    job->finished = true;
                    job->ok = (job->reply->error() == QNetworkReply::NoError);
                    if (!job->ok) {
                        job->err = job->reply->errorString();
                    }
                    if (job->reply->bytesAvailable() > 0 && job->out->isOpen()) {
                        job->out->write(job->reply->readAll());
                    }
                    if (--remaining == 0) {
                        loop.quit();
                    }
                }
            });
            // Open eagerly too: headers may never emit metaDataChanged on some
            // stacks (e.g. file:// or cached replies); ensure readyRead never
            // drops bytes. Corrected in metaDataChanged when headers arrive.
            if (!j.out->isOpen()) {
                openOut(j, QIODevice::WriteOnly | (j.resumeFrom > 0 ? QIODevice::Append : QIODevice::Truncate));
            }
        }
        poll.start();
        if (remaining > 0) {
            loop.exec();
        }
        poll.stop();

        // Settle the wave.
        bool waveHadFailure = false;
        for (auto &j : wave) {
            if (j.out->isOpen()) {
                j.out->close();
            }
            const bool cancelled = ctx.isCancelled();
            QString settleErr;
            bool good = j.ok && !cancelled;
            if (good) {
                // HTTP error statuses may still report NoError on some stacks.
                const int status = j.reply ? j.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
                if (status >= 400) {
                    good = false;
                    j.err = QObject::tr("Server error %1").arg(status);
                }
            }
            if (good && !verifyHashes(j.part, j.req.expectedSha1, j.req.expectedSha256, j.req.expectedSha512, &settleErr)) {
                good = false;
                j.err = settleErr.isEmpty() ? QObject::tr("Checksum mismatch.") : settleErr;
                QFile::remove(j.part); // corrupt: restart from scratch next wave
            }
            if (good && !publishFile(j.part, j.req.destPath, &settleErr)) {
                good = false;
                j.err = settleErr;
            }
            delete j.reply;
            j.reply = nullptr;
            if (cancelled) {
                if (error) {
                    *error = QObject::tr("Cancelled");
                }
                return false;
            }
            if (good) {
                ++done;
                ctx.report(done, total,
                           QObject::tr("%1 of %2 files ready").arg(done).arg(total));
                Logger::info(
                    QStringLiteral("Downloaded %1").arg(j.req.url.fileName()));
            } else {
                waveHadFailure = true;
                const int n = attemptCount.value(j.req.destPath, 0) + 1;
                attemptCount[j.req.destPath] = n;
                if (n >= kMaxAttempts) {
                    if (firstError.isEmpty()) {
                        firstError = QObject::tr("%1: %2").arg(j.req.url.fileName()).arg(j.err);
                    }
                    ++done; // count as settled so progress completes
                    ctx.report(done, total,
                               QObject::tr("%1 of %2 files ready").arg(done).arg(total));
                } else {
                    pending.prepend(j.req); // retry (at front: keeps order stable-ish)
                }
            }
        }
        Q_UNUSED(waveAborted);
        if (waveHadFailure && !pending.isEmpty()) {
            // Exponential backoff between waves: 1s, 2s, 4s (capped).
            const int waitMs = 1000 * (1 << qMin(failWaves++, 2));
            ctx.report(done, total, QObject::tr("Retrying…"));
            QThread::msleep(static_cast<unsigned long>(waitMs));
        } else {
            failWaves = 0;
        }
    }

    if (!firstError.isEmpty()) {
        if (error) {
            *error = firstError;
        }
        return false;
    }
    ctx.report(total, total, QObject::tr("Done"));
    return true;
}

#include "DownloadManager.moc"
