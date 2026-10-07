#include "ModrinthApi.h"

#include "Constants.h"
#include "DownloadManager.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

namespace ModrinthMeta {

QString buildFacets(const ModrinthFilters &f)
{
    // AND of OR-groups, e.g. [["project_type:mod"],["versions:1.20.1"],["categories:fabric"]].
    QStringList groups;
    if (!f.projectType.isEmpty() && f.projectType != QStringLiteral("all")) {
        groups.append(QStringLiteral("[[\"project_type:%1\"]]").arg(f.projectType));
    }
    Q_UNUSED(groups);
    QJsonArray andGroups;
    auto addOr = [&](const QStringList &alts) {
        if (alts.isEmpty()) {
            return;
        }
        QJsonArray orGroup;
        for (const auto &a : alts) {
            orGroup.append(a);
        }
        andGroups.append(orGroup);
    };
    if (!f.projectType.isEmpty() && f.projectType != QStringLiteral("all")) {
        addOr({ QStringLiteral("project_type:") + f.projectType });
    }
    if (!f.gameVersion.isEmpty() && f.gameVersion != QStringLiteral("any")) {
        addOr({ QStringLiteral("versions:") + f.gameVersion });
    }
    if (!f.loader.isEmpty() && f.loader != QStringLiteral("any")) {
        addOr({ QStringLiteral("categories:") + f.loader.toLower() });
    }
    if (!f.category.isEmpty() && f.category != QStringLiteral("any")) {
        addOr({ QStringLiteral("categories:") + f.category.toLower() });
    }
    return QString::fromUtf8(QJsonDocument(andGroups).toJson(QJsonDocument::Compact));
}

QString searchPath(const ModrinthFilters &f)
{
    QUrlQuery q;
    if (!f.query.trimmed().isEmpty()) {
        q.addQueryItem(QStringLiteral("query"), f.query.trimmed());
    }
    const QString facets = buildFacets(f);
    if (facets != QStringLiteral("[]")) {
        q.addQueryItem(QStringLiteral("facets"), facets);
    }
    static const QSet<QString> kSorts = { QStringLiteral("relevance"), QStringLiteral("downloads"),
                                          QStringLiteral("follows"), QStringLiteral("newest"),
                                          QStringLiteral("updated") };
    q.addQueryItem(QStringLiteral("index"), kSorts.contains(f.sort) ? f.sort : QStringLiteral("relevance"));
    q.addQueryItem(QStringLiteral("offset"), QString::number(qMax(0, f.offset)));
    q.addQueryItem(QStringLiteral("limit"), QString::number(qBound(5, f.limit, 100)));
    return QStringLiteral("/v2/search?") + q.query(QUrl::FullyEncoded);
}

static QStringList strList(const QJsonValue &v)
{
    QStringList out;
    for (const auto &e : v.toArray()) {
        const QString s = e.toString();
        if (!s.isEmpty()) {
            out.append(s);
        }
    }
    return out;
}

ModrinthSearchPage parseSearch(const QJsonObject &o)
{
    ModrinthSearchPage page;
    page.totalHits = o.value(QStringLiteral("total_hits")).toInt(0);
    page.offset = o.value(QStringLiteral("offset")).toInt(0);
    page.limit = o.value(QStringLiteral("limit")).toInt(20);
    for (const auto &v : o.value(QStringLiteral("hits")).toArray()) {
        const QJsonObject h = v.toObject();
        ModrinthSearchHit hit;
        hit.projectId = h.value(QStringLiteral("project_id")).toString();
        hit.slug = h.value(QStringLiteral("slug")).toString();
        hit.title = h.value(QStringLiteral("title")).toString();
        hit.author = h.value(QStringLiteral("author")).toString();
        hit.summary = h.value(QStringLiteral("description")).toString();
        hit.iconUrl = h.value(QStringLiteral("icon_url")).toString();
        hit.downloads = (qint64)h.value(QStringLiteral("downloads")).toDouble(0);
        hit.follows = h.value(QStringLiteral("follows")).toInt(0);
        hit.categories = strList(h.value(QStringLiteral("categories")));
        hit.gameVersions = strList(h.value(QStringLiteral("versions")));
        hit.projectType = h.value(QStringLiteral("project_type")).toString();
        if (!hit.projectId.isEmpty()) {
            page.hits.append(hit);
        }
    }
    return page;
}

ModrinthProject parseProject(const QJsonObject &o, const QString &ownerName)
{
    ModrinthProject p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.slug = o.value(QStringLiteral("slug")).toString();
    p.title = o.value(QStringLiteral("title")).toString();
    p.author = ownerName;
    p.summary = o.value(QStringLiteral("description")).toString();
    p.body = o.value(QStringLiteral("body")).toString();
    p.iconUrl = o.value(QStringLiteral("icon_url")).toString();
    p.downloads = (qint64)o.value(QStringLiteral("downloads")).toDouble(0);
    p.follows = o.value(QStringLiteral("followers")).toInt(0);
    p.categories = strList(o.value(QStringLiteral("categories")));
    p.loaders = strList(o.value(QStringLiteral("loaders")));
    p.gameVersions = strList(o.value(QStringLiteral("game_versions")));
    p.projectType = o.value(QStringLiteral("project_type")).toString();
    p.clientSide = o.value(QStringLiteral("client_side")).toString();
    p.serverSide = o.value(QStringLiteral("server_side")).toString();
    const QJsonObject lic = o.value(QStringLiteral("license")).toObject();
    p.licenseId = lic.value(QStringLiteral("id")).toString();
    p.licenseName = lic.value(QStringLiteral("name")).toString();
    p.licenseUrl = lic.value(QStringLiteral("url")).toString();
    p.sourceUrl = o.value(QStringLiteral("source_url")).toString();
    p.issuesUrl = o.value(QStringLiteral("issues_url")).toString();
    p.wikiUrl = o.value(QStringLiteral("wiki_url")).toString();
    p.discordUrl = o.value(QStringLiteral("discord_url")).toString();
    for (const auto &v : o.value(QStringLiteral("gallery")).toArray()) {
        const QJsonObject g = v.toObject();
        const QString url = g.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            p.gallery.append({ url, g.value(QStringLiteral("title")).toString() });
        }
    }
    return p;
}

static ModrinthFile parseFile(const QJsonObject &o)
{
    ModrinthFile f;
    f.url = o.value(QStringLiteral("url")).toString();
    f.filename = o.value(QStringLiteral("filename")).toString();
    const QJsonObject h = o.value(QStringLiteral("hashes")).toObject();
    f.sha512 = h.value(QStringLiteral("sha512")).toString();
    f.sha1 = h.value(QStringLiteral("sha1")).toString();
    f.size = (qint64)o.value(QStringLiteral("size")).toDouble(-1);
    f.primary = o.value(QStringLiteral("primary")).toBool(false);
    return f;
}

ModrinthVersion parseVersion(const QJsonObject &o)
{
    ModrinthVersion v;
    v.id = o.value(QStringLiteral("id")).toString();
    v.projectId = o.value(QStringLiteral("project_id")).toString();
    v.name = o.value(QStringLiteral("name")).toString();
    v.versionNumber = o.value(QStringLiteral("version_number")).toString();
    v.versionType = o.value(QStringLiteral("version_type")).toString();
    v.loaders = strList(o.value(QStringLiteral("loaders")));
    v.gameVersions = strList(o.value(QStringLiteral("game_versions")));
    v.changelog = o.value(QStringLiteral("changelog")).toString();
    v.featured = o.value(QStringLiteral("featured")).toBool(false);
    for (const auto &fv : o.value(QStringLiteral("files")).toArray()) {
        const ModrinthFile f = parseFile(fv.toObject());
        if (!f.url.isEmpty() && !f.filename.isEmpty()) {
            v.files.append(f);
        }
    }
    for (const auto &dv : o.value(QStringLiteral("dependencies")).toArray()) {
        const QJsonObject d = dv.toObject();
        ModrinthDependency dep;
        dep.projectId = d.value(QStringLiteral("project_id")).toString();
        dep.versionId = d.value(QStringLiteral("version_id")).toString();
        dep.type = d.value(QStringLiteral("dependency_type")).toString();
        if (!dep.type.isEmpty()) {
            v.dependencies.append(dep);
        }
    }
    return v;
}

QList<ModrinthVersion> parseVersionList(const QJsonObject &o)
{
    // Defensive: some endpoints wrap; the real list endpoint returns an array.
    QList<ModrinthVersion> out;
    for (const auto &v : o.value(QStringLiteral("versions")).toArray()) {
        if (v.isObject()) {
            out.append(parseVersion(v.toObject()));
        }
    }
    return out;
}

QList<ModrinthVersion> parseVersionArray(const QByteArray &json)
{
    QList<ModrinthVersion> out;
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !doc.isArray()) {
        return out;
    }
    for (const auto &v : doc.array()) {
        if (!v.isObject()) {
            continue;
        }
        const ModrinthVersion ver = parseVersion(v.toObject());
        if (!ver.id.isEmpty() && !ver.files.isEmpty()) {
            out.append(ver);
        }
    }
    return out;
}

bool versionCompatible(const ModrinthVersion &v, const QString &mcVersion, const QString &loader,
                       const QString &projectType)
{
    if (v.files.isEmpty()) {
        return false;
    }
    if (!mcVersion.isEmpty() && !v.gameVersions.isEmpty() && !v.gameVersions.contains(mcVersion)) {
        return false;
    }
    const QString lt = loader.trimmed().toLower();
    const QString pt = projectType.trimmed().toLower();
    if (lt.isEmpty() || lt == QStringLiteral("vanilla") || lt == QStringLiteral("any")) {
        // Non-mod content runs on vanilla; mods need a real loader.
        return (pt == QStringLiteral("mod") || pt == QStringLiteral("modpack")) ? v.loaders.isEmpty()
                                                                                : true;
    }
    if (v.loaders.isEmpty()) {
        return true;
    }
    if (v.loaders.contains(lt)) {
        return true;
    }
    // Resource packs / shaders / datapacks target "minecraft", not a loader.
    if (pt == QStringLiteral("resourcepack") || pt == QStringLiteral("shader") || pt == QStringLiteral("datapack")) {
        return v.loaders.contains(QStringLiteral("minecraft"));
    }
    return false;
}

static int typeRank(const QString &t)
{
    if (t == QStringLiteral("release")) {
        return 0;
    }
    if (t == QStringLiteral("beta")) {
        return 1;
    }
    return 2;
}

int pickBestVersion(const QList<ModrinthVersion> &all, const QString &mcVersion, const QString &loader,
                    const QString &projectType)
{
    int best = -1;
    for (int i = 0; i < all.size(); ++i) {
        if (!versionCompatible(all.at(i), mcVersion, loader, projectType)) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        const auto &a = all.at(best);
        const auto &b = all.at(i);
        // Lists are newest-first: keep the earlier one unless its channel is
        // worse (release beats beta beats alpha).
        if (typeRank(b.versionType) < typeRank(a.versionType)
            && !(a.featured && !b.featured)) {
            best = i;
        }
    }
    return best;
}

QStringList requiredDeps(const ModrinthVersion &v)
{
    QStringList out;
    for (const auto &d : v.dependencies) {
        if (d.type == QStringLiteral("required") && !d.projectId.isEmpty() && !out.contains(d.projectId)) {
            out.append(d.projectId);
        }
    }
    return out;
}

QString targetDirForType(const QString &projectType)
{
    const QString t = projectType.trimmed().toLower();
    if (t == QStringLiteral("resourcepack")) {
        return QStringLiteral("resourcepacks");
    }
    if (t == QStringLiteral("shader")) {
        return QStringLiteral("shaderpacks");
    }
    if (t == QStringLiteral("datapack")) {
        return QStringLiteral("datapacks");
    }
    return QStringLiteral("mods"); // mod, modpack files, unknown
}

QString prettyCount(qint64 n)
{
    if (n < 1000) {
        return QString::number(n);
    }
    if (n < 1000000) {
        QString s = QString::number(n / 100.0 / 10.0, 'f', 1);
        return s + QStringLiteral("k");
    }
    QString s = QString::number(n / 100000.0 / 10.0, 'f', 1);
    return s + QStringLiteral("M");
}

} // namespace ModrinthMeta

ModrinthFile ModrinthVersion::bestFile() const
{
    for (const auto &f : files) {
        if (f.primary) {
            return f;
        }
    }
    return files.isEmpty() ? ModrinthFile{} : files.first();
}

QString ModrinthApi::apiBase()
{
    return QStringLiteral("https://api.modrinth.com/v2");
}

ModrinthApi::ModrinthApi(const QString &dataDir, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_downloads(downloads)
{
    QDir().mkpath(cacheDir());
    QDir().mkpath(iconCacheDir());
}

QString ModrinthApi::cacheDir() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("meta/modrinth"));
}

QString ModrinthApi::iconCacheDir() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("cache/modrinth-icons"));
}

static QString cacheNameFor(const QString &path)
{
    const QByteArray h = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(h.left(24)) + QStringLiteral(".json");
}

QByteArray ModrinthApi::fetchJsonBlocking(const QString &path, const QString &cacheName, Task::Context &ctx,
                                         bool *fromCache, const QString &stepName)
{
    const QString dest = QDir(cacheDir()).filePath(cacheName);
    const bool offline = NetworkStatus::instance().isEffectivelyOffline();
    if (offline) {
        QFile f(dest);
        if (f.open(QIODevice::ReadOnly)) {
            if (fromCache) {
                *fromCache = true;
            }
            return f.readAll();
        }
        ctx.fail(QObject::tr("You're offline and this %1 isn't cached. Go online once to fetch it, or browse "
                             "installed content.")
                     .arg(stepName));
        return {};
    }
    DownloadRequest req{ QUrl(ModrinthApi::apiBase() + path), dest };
    req.resume = false;
    QString err;
    if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
        const QString low = err.toLower();
        if (low.contains(QStringLiteral("429")) || low.contains(QStringLiteral("rate"))) {
            ctx.fail(QObject::tr("Modrinth is rate-limiting requests right now. Wait a minute and try again."));
        } else {
            ctx.fail(err);
        }
        // Fall back to cache when the network fails but we have data.
        QFile f(dest);
        if (f.open(QIODevice::ReadOnly)) {
            Logger::warning(QStringLiteral("Modrinth fetch failed; serving cached %1.").arg(cacheName));
            if (fromCache) {
                *fromCache = true;
            }
            return f.readAll();
        }
        return {};
    }
    if (fromCache) {
        *fromCache = false;
    }
    emit cacheChanged();
    QFile f(dest);
    if (!f.open(QIODevice::ReadOnly)) {
        ctx.fail(QObject::tr("Couldn't read the Modrinth answer."));
        return {};
    }
    return f.readAll();
}

ModrinthSearchPage ModrinthApi::searchBlocking(const ModrinthFilters &f, Task::Context &ctx, bool *fromCache)
{
    const QString path = ModrinthMeta::searchPath(f);
    const QByteArray raw = fetchJsonBlocking(path, cacheNameFor(path), ctx, fromCache, tr("search"));
    if (raw.isEmpty() && ctx.isCancelled()) {
        return {};
    }
    if (raw.isEmpty()) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    ModrinthSearchPage page;
    if (e.error == QJsonParseError::NoError && doc.isObject()) {
        page = ModrinthMeta::parseSearch(doc.object());
        page.offset = f.offset;
        page.limit = f.limit;
        // Remember the last good search for the offline view.
        QFile last(QDir(cacheDir()).filePath(QStringLiteral("last-search.json")));
        if (last.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            last.write(raw.left(256 * 1024));
        }
    } else {
        ctx.fail(tr("Modrinth sent an unexpected answer."));
    }
    return page;
}

ModrinthSearchPage ModrinthApi::lastSearchFromCache() const
{
    QFile f(QDir(cacheDir()).filePath(QStringLiteral("last-search.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return ModrinthMeta::parseSearch(doc.object());
}

static QString ownerFromMembers(const QByteArray &json)
{
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }
    // Prefer the Owner role, else the first member.
    QString first;
    for (const auto &v : doc.array()) {
        const QJsonObject m = v.toObject();
        const QString name = m.value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString();
        if (first.isEmpty() && !name.isEmpty()) {
            first = name;
        }
        if (m.value(QStringLiteral("role")).toString().compare(QStringLiteral("owner"), Qt::CaseInsensitive) == 0
            && !name.isEmpty()) {
            return name;
        }
    }
    return first;
}

ModrinthProject ModrinthApi::projectBlocking(const QString &idOrSlug, Task::Context &ctx)
{
    const QString path = QStringLiteral("/v2/project/") + QUrl::toPercentEncoding(idOrSlug.trimmed());
    bool cached = false;
    const QByteArray raw = fetchJsonBlocking(path, QStringLiteral("project-") + cacheNameFor(path), ctx, &cached,
                                             tr("project"));
    if (raw.isEmpty()) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        ctx.fail(tr("Modrinth sent an unexpected answer."));
        return {};
    }
    QString owner;
    if (!NetworkStatus::instance().isEffectivelyOffline()) {
        bool dummy = false;
        const QByteArray members =
            fetchJsonBlocking(path + QStringLiteral("/members"), QStringLiteral("members-") + cacheNameFor(path),
                              ctx, &dummy, tr("team"));
        if (!members.isEmpty()) {
            owner = ownerFromMembers(members);
        }
    } else {
        QFile f(QDir(cacheDir()).filePath(QStringLiteral("members-") + cacheNameFor(path)));
        if (f.open(QIODevice::ReadOnly)) {
            owner = ownerFromMembers(f.readAll());
        }
    }
    return ModrinthMeta::parseProject(doc.object(), owner);
}

QList<ModrinthVersion> ModrinthApi::versionsBlocking(const QString &projectId, const QString &loader,
                                                     const QString &gameVersion, Task::Context &ctx)
{
    QString path = QStringLiteral("/v2/project/") + QUrl::toPercentEncoding(projectId.trimmed())
        + QStringLiteral("/version");
    QUrlQuery q;
    if (!loader.isEmpty() && loader != QStringLiteral("any")) {
        QString cleanLoader = loader.toLower();
        cleanLoader.remove(QLatin1Char('"'));
        q.addQueryItem(QStringLiteral("loaders"), QStringLiteral("[\"%1\"]").arg(cleanLoader));
    }
    if (!gameVersion.isEmpty() && gameVersion != QStringLiteral("any")) {
        QString cleanGame = gameVersion;
        cleanGame.remove(QLatin1Char('"'));
        q.addQueryItem(QStringLiteral("game_versions"), QStringLiteral("[\"%1\"]").arg(cleanGame));
    }
    const QString qs = q.query(QUrl::FullyEncoded);
    if (!qs.isEmpty()) {
        path += QStringLiteral("?") + qs;
    }
    const QByteArray raw =
        fetchJsonBlocking(path, QStringLiteral("versions-") + cacheNameFor(path), ctx, nullptr, tr("versions"));
    if (raw.isEmpty()) {
        return {};
    }
    QList<ModrinthVersion> all = ModrinthMeta::parseVersionArray(raw);
    if (all.isEmpty()) {
        // Honest empty vs corrupt: an empty array is a valid "no versions".
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
        if (e.error != QJsonParseError::NoError || !doc.isArray()) {
            ctx.fail(tr("Modrinth sent an unexpected answer."));
        }
        return {};
    }
    // Client-side re-filter for safety (server params are best-effort).
    if (!loader.isEmpty() && loader != QStringLiteral("any") && !gameVersion.isEmpty()
        && gameVersion != QStringLiteral("any")) {
        QList<ModrinthVersion> kept;
        for (const auto &v : all) {
            const bool loaderOk = v.loaders.isEmpty() || v.loaders.contains(loader.toLower())
                || v.loaders.contains(QStringLiteral("minecraft"));
            const bool gameOk = v.gameVersions.isEmpty() || v.gameVersions.contains(gameVersion);
            if (loaderOk && gameOk) {
                kept.append(v);
            }
        }
        return kept;
    }
    return all;
}

ModrinthVersion ModrinthApi::versionBlocking(const QString &versionId, Task::Context &ctx)
{
    const QString path = QStringLiteral("/v2/version/") + QUrl::toPercentEncoding(versionId.trimmed());
    const QByteArray raw = fetchJsonBlocking(path, QStringLiteral("version-") + cacheNameFor(path), ctx, nullptr,
                                             tr("version"));
    if (raw.isEmpty()) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        ctx.fail(tr("Modrinth sent an unexpected answer."));
        return {};
    }
    return ModrinthMeta::parseVersion(doc.object());
}

QList<ModInstallPlan> ModrinthApi::resolveInstallPlan(const QList<ModInstallPlan> &roots, const QString &mcVersion,
                                                      const QString &loader, Task::Context &ctx,
                                                      QStringList *warnings, QString *error)
{
    QList<ModInstallPlan> plan = roots;
    QSet<QString> seen; // project ids already in the plan
    for (const auto &r : roots) {
        if (!r.project.id.isEmpty()) {
            seen.insert(r.project.id);
        }
    }
    // Incompatible pairs across the whole plan fail loudly, never silently.
    auto checkIncompatible = [&](const ModrinthVersion &v, const QString &title) -> bool {
        for (const auto &d : v.dependencies) {
            if (d.type == QStringLiteral("incompatible") && !d.projectId.isEmpty() && seen.contains(d.projectId)) {
                if (error) {
                    *error = QObject::tr("“%1” is marked incompatible with another selected mod. Remove one of "
                                         "them to continue.")
                                 .arg(title);
                }
                return false;
            }
        }
        return true;
    };
    for (const auto &r : roots) {
        if (!checkIncompatible(r.version, r.project.title)) {
            return {};
        }
    }
    // Breadth-first over required deps (cycle-safe via `seen`).
    QList<QString> queue;
    for (const auto &r : roots) {
        for (const auto &dep : ModrinthMeta::requiredDeps(r.version)) {
            if (!seen.contains(dep)) {
                queue.append(dep);
            }
        }
    }
    int guard = 0;
    while (!queue.isEmpty()) {
        if (ctx.isCancelled() || ++guard > 200) {
            if (error && guard > 200) {
                *error = QObject::tr("Dependency resolution went too deep (over 200 mods). Aborting safely.");
            }
            return guard > 200 ? QList<ModInstallPlan>{} : plan;
        }
        const QString pid = queue.takeFirst();
        if (seen.contains(pid)) {
            continue;
        }
        seen.insert(pid);
        const ModrinthProject proj = projectBlocking(pid, ctx);
        if (proj.id.isEmpty()) {
            if (warnings) {
                warnings->append(QObject::tr("A required dependency (%1) couldn't be fetched — skipping it. The "
                                             "mod may not work without it.")
                                     .arg(pid));
            }
            continue;
        }
        const QList<ModrinthVersion> vers = versionsBlocking(pid, loader, mcVersion, ctx);
        const int best = ModrinthMeta::pickBestVersion(vers, mcVersion, loader, proj.projectType);
        if (best < 0) {
            if (error) {
                *error = QObject::tr("“%1” needs “%2”, but no version of it works with Minecraft %3 + %4. Pick "
                                     "a different version or skip the mod.")
                             .arg(plan.first().project.title, proj.title, mcVersion,
                                  loader.isEmpty() ? QObject::tr("vanilla") : loader);
            }
            return {};
        }
        ModInstallPlan unit;
        unit.project = proj;
        unit.version = vers.at(best);
        unit.file = unit.version.bestFile();
        unit.isDependency = true;
        unit.targetDir = ModrinthMeta::targetDirForType(proj.projectType);
        if (!checkIncompatible(unit.version, proj.title)) {
            return {};
        }
        plan.append(unit);
        for (const auto &dep : ModrinthMeta::requiredDeps(unit.version)) {
            if (!seen.contains(dep) && !queue.contains(dep)) {
                queue.append(dep);
            }
        }
    }
    return plan;
}

#include "ModrinthApi.moc"
