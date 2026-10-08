#pragma once

#include "Task.h"

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class DownloadManager;

// CurseForge API v1 client (https://api.curseforge.com/v1).
//
// CurseForge file downloads require an API key. Hearthlight ships no key:
// the user pastes their own (from https://console.curseforge.com/) into
// Settings → Mod sources, where it is kept in the OS credential store via
// SecureTokenStore (account "curseforge", key "apiKey") — never in
// accounts.json, instance.json or logs.
//
// Pure parsing helpers live in CurseForgeMeta (unit-tested, no network).
// Blocking fetches run on Task worker threads and reuse DownloadManager so
// offline handling, retries and hashing stay in one place.
struct CurseForgeFile {
    int projectId = 0;
    qint64 fileId = 0;
    QString fileName;
    QString downloadUrl;
    qint64 size = -1;
    bool serverPack = false;
};

namespace CurseForgeMeta {
// SecureTokenStore location for the user-supplied key.
inline QString storeAccountId()
{
    return QStringLiteral("curseforge");
}
inline QString storeKey()
{
    return QStringLiteral("apiKey");
}

inline QString apiBase()
{
    return QStringLiteral("https://api.curseforge.com/v1");
}

// Parse GET /mods/{projectId}/files/{fileId} answer ({"data": {...}}).
CurseForgeFile parseFileResponse(const QByteArray &json, int projectId, qint64 fileId);

// Manifest helpers for a CurseForge-style modpack zip (manifest.json).
QString manifestMinecraftVersion(const QByteArray &manifestJson);
QString manifestLoaderType(const QByteArray &manifestJson); // vanilla|fabric|quilt|forge|neoforge
QList<QPair<int, qint64>> manifestFiles(const QByteArray &manifestJson); // (projectID, fileID)
QString manifestName(const QByteArray &manifestJson);
} // namespace CurseForgeMeta

class CurseForgeApi : public QObject {
    Q_OBJECT
public:
    explicit CurseForgeApi(const QString &dataDir, DownloadManager *downloads, QObject *parent = nullptr);

    QString cacheDir() const;

    // Resolve one (projectID, fileID) pair into a direct download URL.
    // Blocking on a Task worker thread. Empty apiKey fails honestly with a
    // message pointing at Settings → Mod sources. Offline serves cache only.
    CurseForgeFile fileBlocking(int projectId, qint64 fileId, const QString &apiKey, Task::Context &ctx);

private:
    QByteArray fetchJsonBlocking(const QString &path, const QString &cacheName, const QString &apiKey,
                                 Task::Context &ctx);

    QString m_dataDir;
    DownloadManager *m_downloads = nullptr;
};
