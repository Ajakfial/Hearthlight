#pragma once

#include "Task.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class DownloadManager;

// Modrinth API v2 client (spec section 9).
// Descriptive User-Agent (hearthlight/<version> (contact)), rate-limit
// friendly, responses cached under meta/modrinth/ and reusable offline.
//
// Pure parsing/picking helpers live in ModrinthMeta (unit-tested, no
// network). Blocking fetches run on Task worker threads and reuse
// DownloadManager so offline handling, retries and hashing stay in one place.
struct ModrinthFile {
    QString url;
    QString filename;
    QString sha512;
    QString sha1;
    qint64 size = -1;
    bool primary = false;
};

struct ModrinthDependency {
    QString projectId; // may be empty for version-only pins
    QString versionId; // exact pinned version, may be empty
    QString type; // required | optional | incompatible | embedded
};

struct ModrinthVersion {
    QString id;
    QString projectId;
    QString name;
    QString versionNumber;
    QString versionType; // release | beta | alpha
    QStringList loaders;
    QStringList gameVersions;
    QString changelog;
    QList<ModrinthFile> files;
    QList<ModrinthDependency> dependencies;
    bool featured = false;

    ModrinthFile bestFile() const; // primary first, else first
};

struct ModrinthProject {
    QString id;
    QString slug;
    QString title;
    QString author; // owner username (from members list, may be empty)
    QString summary;
    QString body; // markdown description
    QString iconUrl;
    qint64 downloads = 0;
    int follows = 0;
    QStringList categories;
    QStringList loaders;
    QStringList gameVersions;
    QString projectType; // mod | modpack | resourcepack | shader | datapack
    QString clientSide; // required | optional | unsupported
    QString serverSide;
    QString licenseId;
    QString licenseName;
    QString licenseUrl;
    QString sourceUrl;
    QString issuesUrl;
    QString wikiUrl;
    QString discordUrl;
    struct GalleryImage {
        QString url;
        QString title;
    };
    QList<GalleryImage> gallery;
};

struct ModrinthSearchHit {
    QString projectId;
    QString slug;
    QString title;
    QString author;
    QString summary;
    QString iconUrl;
    qint64 downloads = 0;
    int follows = 0;
    QStringList categories; // includes loaders in the search index
    QStringList gameVersions;
    QString projectType;
};

struct ModrinthSearchPage {
    QList<ModrinthSearchHit> hits;
    int totalHits = 0;
    int offset = 0;
    int limit = 20;
};

struct ModrinthFilters {
    QString query;
    QString projectType; // empty = all; else mod|modpack|resourcepack|shader|datapack
    QString loader; // empty = any; else fabric|forge|quilt|neoforge|minecraft
    QString gameVersion; // empty = any
    QString category; // empty = any (maps to categories:<slug>)
    QString sort = QStringLiteral("relevance"); // relevance|downloads|follows|newest|updated
    int offset = 0;
    int limit = 20;
};

// One resolved install unit (a mod version + where it goes).
struct ModInstallPlan {
    ModrinthProject project;
    ModrinthVersion version;
    ModrinthFile file;
    bool isDependency = false;
    QString targetDir; // e.g. "mods", "resourcepacks", "shaderpacks", "datapacks"
};

namespace ModrinthMeta {
// Search facets per Modrinth v2 docs: AND of OR-groups. Loaders are matched
// through the `categories` facet in the search index.
QString buildFacets(const ModrinthFilters &f);
QString searchPath(const ModrinthFilters &f); // full /v2/search?... path

ModrinthSearchPage parseSearch(const QJsonObject &o);
ModrinthProject parseProject(const QJsonObject &o, const QString &ownerName = {});
ModrinthVersion parseVersion(const QJsonObject &o);
QList<ModrinthVersion> parseVersionList(const QJsonObject &o); // object wrapper (defensive)
QList<ModrinthVersion> parseVersionArray(const QByteArray &json); // real shape: bare array

// True when this version can run on (mcVersion, loader) for its project type.
bool versionCompatible(const ModrinthVersion &v, const QString &mcVersion, const QString &loader,
                       const QString &projectType);
// Newest compatible version (release preferred, then beta, then alpha).
// Returns index into `all`, or -1.
int pickBestVersion(const QList<ModrinthVersion> &all, const QString &mcVersion, const QString &loader,
                    const QString &projectType);
// Required dependency project ids (deduplicated, embedded excluded).
QStringList requiredDeps(const ModrinthVersion &v);

// Target folder inside the game dir for a project type.
QString targetDirForType(const QString &projectType); // mods|resourcepacks|shaderpacks|datapacks|mods(pack)

// Human counts: 1234 -> "1.2k".
QString prettyCount(qint64 n);
} // namespace ModrinthMeta

class ModrinthApi : public QObject {
    Q_OBJECT
public:
    explicit ModrinthApi(const QString &dataDir, DownloadManager *downloads, QObject *parent = nullptr);

    static QString apiBase(); // https://api.modrinth.com/v2
    QString cacheDir() const;
    QString iconCacheDir() const;

    // Blocking fetches (Task worker threads). Cache-first when offline;
    // refresh-then-cache when online. Failures are human-readable.
    ModrinthSearchPage searchBlocking(const ModrinthFilters &f, Task::Context &ctx, bool *fromCache = nullptr);
    ModrinthProject projectBlocking(const QString &idOrSlug, Task::Context &ctx);
    // Server-side filtered list (loaders/game_versions params), client
    // re-filtered for safety.
    QList<ModrinthVersion> versionsBlocking(const QString &projectId, const QString &loader,
                                            const QString &gameVersion, Task::Context &ctx);
    ModrinthVersion versionBlocking(const QString &versionId, Task::Context &ctx);

    // Resolve required dependencies recursively (breadth-first, cycle-safe).
    // Returns the full ordered plan (roots first, then deps). `warnings`
    // collects optional-dep notes and skipped items. Incompatible pairs fail.
    QList<ModInstallPlan> resolveInstallPlan(const QList<ModInstallPlan> &roots, const QString &mcVersion,
                                             const QString &loader, Task::Context &ctx, QStringList *warnings,
                                             QString *error);

    // Last successful search hits (for the offline "cached results" view).
    ModrinthSearchPage lastSearchFromCache() const;

signals:
    void cacheChanged();

private:
    QByteArray fetchJsonBlocking(const QString &path, const QString &cacheName, Task::Context &ctx, bool *fromCache,
                                 const QString &stepName);
    QString m_dataDir;
    DownloadManager *m_downloads = nullptr;
};
