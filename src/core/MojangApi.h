#pragma once

#include "GamePaths.h"
#include "VersionModel.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class DownloadManager;
class Task;

struct VersionEntry {
    QString id;
    QString type; // release | snapshot | old_beta | old_alpha
    QString url; // version json url
    QString time;
    QString releaseTime;
};

struct VersionManifest {
    QString latestRelease;
    QString latestSnapshot;
    QList<VersionEntry> versions;
    static VersionManifest fromJson(const QJsonObject &o);
};

// Official Mojang metadata client (spec section 4).
// Fetches piston-meta manifest + version JSON, caches under meta/, refreshes
// in the background, and stays fully usable offline from cache.
class MojangApi : public QObject {
    Q_OBJECT
public:
    explicit MojangApi(const GamePaths &paths, DownloadManager *downloads, QObject *parent = nullptr);

    // Synchronous cached reads (no network, safe on GUI thread).
    VersionManifest cachedManifest(bool *ok = nullptr) const;
    // Filtered + searched view for the UI. typeFilter entries are Mojang
    // type strings; empty set shows all. Releases are the default view.
    static QList<VersionEntry> filterEntries(const QList<VersionEntry> &all, const QSet<QString> &typeFilter,
                                            const QString &search);
    static QSet<QString> defaultTypeFilter() { return { QStringLiteral("release") }; }

    // Background refresh of the manifest cache (no-op-ish offline: fails the
    // task with a human message, cached data keeps working).
    Task *refreshManifestTask(QObject *owner = nullptr);
    // Download one version JSON into the cache (follows nothing).
    Task *fetchVersionJsonTask(const QString &versionId, const QString &url, QObject *owner = nullptr);

    // Load a cached version JSON and follow inheritsFrom from cache.
    // Returns merged ParsedVersion. ok=false + error when anything in the
    // chain is missing (offline message tells the user to go online once).
    ParsedVersion loadMergedVersion(const QString &versionId, bool *ok = nullptr, QString *error = nullptr) const;
    bool versionJsonCached(const QString &versionId) const;

signals:
    void manifestChanged();

private:
    GamePaths m_paths;
    DownloadManager *m_downloads = nullptr;
};
