#pragma once

#include "GamePaths.h"
#include "Task.h"
#include "VersionModel.h"

#include <QList>
#include <QObject>
#include <QString>

class DownloadManager;
class Task;

// Mod loader support (spec section 8):
//   Fabric + Quilt via official meta APIs (version list + profile JSON merge)
//   Forge via promotions + installer (legacy pre-1.13 + modern formats)
//   NeoForge via Maven metadata + modern installer flow
//
// Installation is a Task; the resolved loader version is recorded in
// instance.json. Only compatible loaders are listed; "Latest stable" is
// the default recommendation. Everything cached stays launchable offline.

enum class LoaderType { Vanilla = 0, Fabric, Quilt, Forge, NeoForge };

QString loaderTypeToString(LoaderType t); // "vanilla"|"fabric"|…
LoaderType loaderTypeFromString(const QString &s);
QString loaderDisplayName(LoaderType t); // "Vanilla"|"Fabric"|…
bool loaderNeedsInstall(LoaderType t);

struct LoaderVersion {
    QString version; // loader version string
    QString mcVersion; // game version it targets
    bool stable = true;
    QString url; // installer/profile source (where applicable)
};

struct LoaderProfile {
    bool ok = false;
    ParsedVersion overlay; // merge over vanilla via ParsedVersion::merge
    QJsonObject raw;
    QString profileId; // effective version id, e.g. "1.20.4-fabric-0.16.9"
};

// Pure metadata helpers (unit-tested, no network).
namespace LoaderMeta {
QList<LoaderVersion> parseFabricLoaders(const QJsonArray &arr, const QString &mcVersion);
QList<LoaderVersion> parseQuiltLoaders(const QJsonObject &obj, const QString &mcVersion);
QList<LoaderVersion> parseForgePromotions(const QJsonObject &promos, const QString &mcVersion);
QList<LoaderVersion> parseMavenMetadataXml(const QByteArray &xml, const QString &mcPrefix = {});
QString latestStable(const QList<LoaderVersion> &all);
QList<LoaderVersion> filterMc(const QList<LoaderVersion> &all, const QString &mcVersion);
} // namespace LoaderMeta

class ModLoaderInstaller : public QObject {
    Q_OBJECT
public:
    ModLoaderInstaller(const GamePaths &paths, DownloadManager *downloads, QObject *parent = nullptr);

    // Metadata cache dir: meta/loaders/.
    QString loaderCacheDir() const;
    QString loaderProfileFile(const QString &mcVersion, LoaderType type, const QString &loaderVersion) const;

    // Blocking metadata fetches (Task worker threads). Cache-first, then
    // network. Offline with cold cache fails honestly.
    QList<LoaderVersion> fabricLoadersBlocking(const QString &mcVersion, Task::Context &ctx);
    QList<LoaderVersion> quiltLoadersBlocking(const QString &mcVersion, Task::Context &ctx);
    QList<LoaderVersion> forgeVersionsBlocking(const QString &mcVersion, Task::Context &ctx);
    QList<LoaderVersion> neoForgeVersionsBlocking(const QString &mcVersion, Task::Context &ctx);

    // Install a loader for an instance: fetch profile/installer, download
    // loader libraries, extract natives if any. Records nothing itself —
    // the caller writes instance.json (loaderType/loaderVersion).
    // Blocking on a Task worker thread.
    bool installBlocking(const QString &mcVersion, LoaderType type, const QString &loaderVersion, int maxParallel,
                         Task::Context &ctx, LoaderProfile *outProfile = nullptr);

    // Load the cached effective overlay for launch (no network). Returns
    // ok=false when the loader was never installed (go online once).
    LoaderProfile loadCachedProfile(const QString &mcVersion, LoaderType type, const QString &loaderVersion,
                                    QString *error = nullptr) const;

    // Merge vanilla + loader overlay into one effective launch profile.
    static ParsedVersion effectiveVersion(const ParsedVersion &vanilla, const LoaderProfile &loader);

    // Ensure every library jar in the effective profile exists (downloads
    // missing ones). Natives are extracted by VersionInstaller at version
    // install; loader natives are rare but handled here too.
    bool ensureLoaderFilesBlocking(const ParsedVersion &effective, int maxParallel, Task::Context &ctx);

signals:
    void installFinished(const QString &mcVersion, const QString &loader, bool ok);

private:
    bool installFabricLikeBlocking(const QString &mcVersion, LoaderType type, const QString &loaderVersion,
                                   int maxParallel, Task::Context &ctx, LoaderProfile *out);
    bool installForgeLikeBlocking(const QString &mcVersion, LoaderType type, const QString &loaderVersion,
                                  int maxParallel, Task::Context &ctx, LoaderProfile *out);
    bool downloadAndSave(const QUrl &url, const QString &dest, Task::Context &ctx, const char *step);

    GamePaths m_paths;
    DownloadManager *m_downloads = nullptr;
};
