#include "MojangApi.h"

#include "Constants.h"
#include "DownloadManager.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

VersionManifest VersionManifest::fromJson(const QJsonObject &o)
{
    VersionManifest m;
    const QJsonObject latest = o.value(QStringLiteral("latest")).toObject();
    m.latestRelease = latest.value(QStringLiteral("release")).toString();
    m.latestSnapshot = latest.value(QStringLiteral("snapshot")).toString();
    for (const auto &v : o.value(QStringLiteral("versions")).toArray()) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject e = v.toObject();
        VersionEntry entry;
        entry.id = e.value(QStringLiteral("id")).toString();
        entry.type = e.value(QStringLiteral("type")).toString();
        entry.url = e.value(QStringLiteral("url")).toString();
        entry.time = e.value(QStringLiteral("time")).toString();
        entry.releaseTime = e.value(QStringLiteral("releaseTime")).toString();
        if (!entry.id.isEmpty()) {
            m.versions.append(entry);
        }
    }
    return m;
}

MojangApi::MojangApi(const GamePaths &paths, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_downloads(downloads)
{
}

VersionManifest MojangApi::cachedManifest(bool *ok) const
{
    VersionManifest empty;
    QFile f(m_paths.manifestFile());
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        return empty;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok) {
            *ok = false;
        }
        return empty;
    }
    if (ok) {
        *ok = true;
    }
    return VersionManifest::fromJson(doc.object());
}

QList<VersionEntry> MojangApi::filterEntries(const QList<VersionEntry> &all, const QSet<QString> &typeFilter,
                                            const QString &search)
{
    QList<VersionEntry> out;
    const QString q = search.trimmed().toLower();
    for (const auto &e : all) {
        if (!typeFilter.isEmpty() && !typeFilter.contains(e.type)) {
            continue;
        }
        if (!q.isEmpty() && !e.id.toLower().contains(q)) {
            continue;
        }
        out.append(e);
    }
    return out;
}

Task *MojangApi::refreshManifestTask(QObject *owner)
{
    auto *t = new LambdaTask(
        tr("Refresh version list"),
        [this](Task::Context &ctx) {
            DownloadRequest req;
            req.url = QUrl(QString::fromLatin1(Hearthlight::kVersionManifestUrl));
            req.destPath = m_paths.manifestFile();
            req.resume = false;
            QString err;
            // Single-file path through the parallel downloader (offline-aware).
            if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
                ctx.fail(err);
                return false;
            }
            // Validate JSON before publishing success.
            bool ok = false;
            cachedManifest(&ok);
            if (!ok) {
                ctx.fail(tr("Downloaded version list is not valid JSON."));
                return false;
            }
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        owner ? owner : this);
    // Manifest view refreshes wherever it is shown.
    QObject::connect(t, &Task::finished, this, [this](bool) { emit manifestChanged(); });
    Q_UNUSED(owner);
    return t;
}

Task *MojangApi::fetchVersionJsonTask(const QString &versionId, const QString &url, QObject *owner)
{
    auto *t = new LambdaTask(
        tr("Fetch %1 info").arg(versionId),
        [this, versionId, url](Task::Context &ctx) {
            QString useUrl = url;
            if (useUrl.isEmpty()) {
                bool ok = false;
                const VersionManifest m = cachedManifest(&ok);
                if (ok) {
                    for (const auto &e : m.versions) {
                        if (e.id == versionId) {
                            useUrl = e.url;
                            break;
                        }
                    }
                }
            }
            if (useUrl.isEmpty()) {
                ctx.fail(tr("Unknown version “%1”. Refresh the list while online.").arg(versionId));
                return false;
            }
            DownloadRequest req;
            req.url = QUrl(useUrl);
            req.destPath = m_paths.versionJsonFile(versionId);
            req.resume = false;
            QString err;
            if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
                ctx.fail(err);
                return false;
            }
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        owner ? owner : this);
    return t;
}

bool MojangApi::versionJsonCached(const QString &versionId) const
{
    return QFile::exists(m_paths.versionJsonFile(versionId));
}

static ParsedVersion readOneJson(const QString &path, bool *ok, QString *error)
{
    ParsedVersion empty;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        if (error) {
            *error = QStringLiteral("Missing file: %1").arg(path);
        }
        return empty;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok) {
            *ok = false;
        }
        if (error) {
            *error = QStringLiteral("Invalid JSON in %1: %2").arg(path).arg(perr.errorString());
        }
        return empty;
    }
    QString perr2;
    ParsedVersion v = ParsedVersion::fromJson(doc.object(), &perr2);
    if (!perr2.isEmpty()) {
        if (ok) {
            *ok = false;
        }
        if (error) {
            *error = perr2;
        }
        return v;
    }
    if (ok) {
        *ok = true;
    }
    return v;
}

ParsedVersion MojangApi::loadMergedVersion(const QString &versionId, bool *ok, QString *error) const
{
    // Follow inheritsFrom up the chain (max 5), then merge base-first.
    QStringList chain;
    QString cur = versionId;
    for (int depth = 0; depth < 5; ++depth) {
        bool good = false;
        QString err;
        ParsedVersion v = readOneJson(m_paths.versionJsonFile(cur), &good, &err);
        if (!good) {
            if (ok) {
                *ok = false;
            }
            if (error) {
                const bool offline = NetworkStatus::instance().isEffectivelyOffline();
                *error = offline
                    ? tr("“%1” isn't downloaded yet and you're offline. Go online once to fetch it.")
                          .arg(versionId)
                    : tr("Couldn't read “%1”: %2").arg(cur).arg(err);
            }
            return {};
        }
        chain.prepend(cur);
        if (v.inheritsFrom.isEmpty()) {
            break;
        }
        cur = v.inheritsFrom;
        if (chain.contains(cur)) {
            if (ok) {
                *ok = false;
            }
            if (error) {
                *error = tr("Circular inheritsFrom chain at “%1”.").arg(cur);
            }
            return {};
        }
    }
    ParsedVersion merged;
    bool first = true;
    for (const auto &id : chain) {
        bool good = false;
        const ParsedVersion v = readOneJson(m_paths.versionJsonFile(id), &good, nullptr);
        if (!good) {
            if (ok) {
                *ok = false;
            }
            if (error) {
                *error = tr("Couldn't read “%1”.").arg(id);
            }
            return {};
        }
        merged = first ? v : ParsedVersion::merge(merged, v);
        first = false;
    }
    // The playable id is the requested one (overlay keeps its own id anyway).
    merged.id = versionId;
    if (ok) {
        *ok = true;
    }
    return merged;
}
