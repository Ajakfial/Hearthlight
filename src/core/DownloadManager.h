#pragma once

#include "Task.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>

// Parallel downloader with resume, hash verification, retries and offline handling.
//
//   DownloadManager mgr(cacheDir, this);
//   mgr.setMaxParallel(8);
//   Task *t = mgr.enqueue({ QUrl("https://…"), destPath, expectedSha1 });
//   connect(t, &Task::finished, ...);
//   t->start();
//
// Semantics:
//  - downloads go to <dest>.part first, then atomically rename on success
//  - resume uses HTTP Range when the server supports it
//  - SHA-1 / SHA-256 / SHA-512 verified when the corresponding field is set
//  - retry with exponential backoff (up to task maxRetries)
//  - when NetworkStatus reports offline, tasks fail fast with a human message
//  - every download is also mirrored into a local content cache keyed by hash
struct DownloadRequest {
    QUrl url;
    QString destPath;
    QByteArray expectedSha1; // hex, lowercase; empty = skip
    QByteArray expectedSha256; // hex; empty = skip
    QByteArray expectedSha512; // hex; empty = skip
    qint64 expectedSize = -1; // bytes; -1 = unknown (size check skipped)
    bool resume = true;
    QNetworkRequest extraRequest; // optional headers (User-Agent is set automatically)
};

class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(const QString &cacheDir, QObject *parent = nullptr);

    int maxParallel() const { return m_maxParallel; }
    void setMaxParallel(int n);

    QString cacheDir() const { return m_cacheDir; }
    static qint64 cacheSize(const QString &cacheDir);
    static void clearCache(const QString &cacheDir);

    // Returns a Task owned by the caller. Call start() to begin.
    Task *enqueue(const DownloadRequest &req, QObject *owner = nullptr);

    // Blocking batch download for use inside Task::execute on worker threads.
    // Runs up to maxParallel concurrent transfers multiplexed on the calling
    // thread (one QNetworkAccessManager + event loop), so thousands of small
    // files (e.g. game assets) download fast without thread explosion.
    // Files already present and verified (size + given hashes) are skipped
    // with no network. Offline: succeeds only if everything is cached,
    // otherwise fails with a human message naming the first missing file.
    static bool downloadManyBlocking(const QList<DownloadRequest> &reqs, int maxParallel, Task::Context &ctx,
                                     QString *error);
    // Fast up-to-date check: size match (when expectedSize >= 0) plus hash
    // verification for every hash that is given. No network.
    static bool fileUpToDate(const QString &path, const DownloadRequest &req);
    static bool verifyHashes(const QString &path, const QByteArray &sha1, const QByteArray &sha256,
                             const QByteArray &sha512, QString *error);
    static bool publishFile(const QString &partPath, const QString &destPath, QString *error);

signals:
    void queueChanged();

private:
    QString m_cacheDir;
    int m_maxParallel = 8;
};
