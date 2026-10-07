#include "ModLoader.h"

#include "DownloadManager.h"
#include "Logger.h"
#include "NetworkStatus.h"
#include "VersionInstaller.h"
#include "ZipUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

QString loaderTypeToString(LoaderType t)
{
    switch (t) {
    case LoaderType::Fabric:
        return QStringLiteral("fabric");
    case LoaderType::Quilt:
        return QStringLiteral("quilt");
    case LoaderType::Forge:
        return QStringLiteral("forge");
    case LoaderType::NeoForge:
        return QStringLiteral("neoforge");
    case LoaderType::Vanilla:
    default:
        return QStringLiteral("vanilla");
    }
}

LoaderType loaderTypeFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QStringLiteral("fabric")) {
        return LoaderType::Fabric;
    }
    if (v == QStringLiteral("quilt")) {
        return LoaderType::Quilt;
    }
    if (v == QStringLiteral("forge")) {
        return LoaderType::Forge;
    }
    if (v == QStringLiteral("neoforge") || v == QStringLiteral("neoforged")) {
        return LoaderType::NeoForge;
    }
    return LoaderType::Vanilla;
}

QString loaderDisplayName(LoaderType t)
{
    switch (t) {
    case LoaderType::Fabric:
        return QObject::tr("Fabric");
    case LoaderType::Quilt:
        return QObject::tr("Quilt");
    case LoaderType::Forge:
        return QObject::tr("Forge");
    case LoaderType::NeoForge:
        return QObject::tr("NeoForge");
    case LoaderType::Vanilla:
    default:
        return QObject::tr("Vanilla");
    }
}

bool loaderNeedsInstall(LoaderType t)
{
    return t != LoaderType::Vanilla;
}

namespace LoaderMeta {

QList<LoaderVersion> parseFabricLoaders(const QJsonArray &arr, const QString &mcVersion)
{
    QList<LoaderVersion> out;
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        const QJsonObject loader = o.value(QStringLiteral("loader")).toObject();
        const QString ver = loader.value(QStringLiteral("version")).toString();
        const bool stable = loader.value(QStringLiteral("stable")).toBool(true);
        if (ver.isEmpty()) {
            continue;
        }
        out.append({ ver, mcVersion, stable, {} });
    }
    return out;
}

QList<LoaderVersion> parseQuiltLoaders(const QJsonObject &obj, const QString &mcVersion)
{
    QList<LoaderVersion> out;
    // meta.quiltmc.org v3 returns an array directly; accept both shapes.
    QJsonArray arr;
    if (obj.contains(QStringLiteral("loader"))) {
        arr = obj.value(QStringLiteral("loader")).toArray();
    }
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        const QString ver = o.value(QStringLiteral("version")).toString();
        if (ver.isEmpty()) {
            continue;
        }
        // Quilt marks `-beta` in the version string; treat non-beta as stable.
        out.append({ ver, mcVersion, !ver.contains(QStringLiteral("beta"), Qt::CaseInsensitive), {} });
    }
    return out;
}

QList<LoaderVersion> parseForgePromotions(const QJsonObject &promos, const QString &mcVersion)
{
    QList<LoaderVersion> out;
    for (auto it = promos.begin(); it != promos.end(); ++it) {
        const QString key = it.key(); // e.g. "1.20.1-latest" / "1.20.1-recommended"
        if (!key.startsWith(mcVersion + QLatin1Char('-'))) {
            continue;
        }
        const QString ver = it.value().toString();
        if (ver.isEmpty()) {
            continue;
        }
        const bool stable = key.endsWith(QStringLiteral("recommended")) || key.endsWith(QStringLiteral("latest"));
        LoaderVersion lv;
        lv.version = ver;
        lv.mcVersion = mcVersion;
        lv.stable = stable;
        out.append(lv);
    }
    // De-duplicate (latest + recommended often point at the same build).
    QMap<QString, LoaderVersion> uniq;
    for (const auto &lv : out) {
        auto it = uniq.find(lv.version);
        if (it == uniq.end() || (lv.stable && !it->stable)) {
            uniq.insert(lv.version, lv);
        }
    }
    return uniq.values();
}

QList<LoaderVersion> parseMavenMetadataXml(const QByteArray &xml, const QString &mcPrefix)
{
    QList<LoaderVersion> out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("version")) {
            const QString ver = r.readElementText().trimmed();
            if (ver.isEmpty()) {
                continue;
            }
            if (!mcPrefix.isEmpty() && !ver.startsWith(mcPrefix)) {
                continue;
            }
            const bool stable = !ver.contains(QStringLiteral("beta"), Qt::CaseInsensitive)
                && !ver.contains(QStringLiteral("alpha"), Qt::CaseInsensitive)
                && !ver.contains(QStringLiteral("rc"), Qt::CaseInsensitive);
            out.append({ ver, {}, stable, {} });
        }
    }
    return out;
}

QString latestStable(const QList<LoaderVersion> &all)
{
    for (const auto &lv : all) {
        if (lv.stable) {
            return lv.version; // APIs return newest-first
        }
    }
    return all.isEmpty() ? QString() : all.first().version;
}

QList<LoaderVersion> filterMc(const QList<LoaderVersion> &all, const QString &mcVersion)
{
    if (mcVersion.isEmpty()) {
        return all;
    }
    QList<LoaderVersion> out;
    for (const auto &lv : all) {
        if (lv.mcVersion.isEmpty() || lv.mcVersion == mcVersion) {
            out.append(lv);
        }
    }
    return out;
}

} // namespace LoaderMeta

ModLoaderInstaller::ModLoaderInstaller(const GamePaths &paths, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_downloads(downloads)
{
    QDir().mkpath(loaderCacheDir());
}

QString ModLoaderInstaller::loaderCacheDir() const
{
    return QDir(m_paths.metaDir).filePath(QStringLiteral("loaders"));
}

QString ModLoaderInstaller::loaderProfileFile(const QString &mcVersion, LoaderType type,
                                              const QString &loaderVersion) const
{
    const QString safe = QStringLiteral("%1-%2-%3.json")
                             .arg(mcVersion, loaderTypeToString(type),
                                  QString(loaderVersion).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                                                                QStringLiteral("_")));
    return QDir(loaderCacheDir()).filePath(safe);
}

static QJsonDocument readJsonFile(const QString &path, bool *ok)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (ok) {
        *ok = (e.error == QJsonParseError::NoError);
    }
    return doc;
}

bool ModLoaderInstaller::downloadAndSave(const QUrl &url, const QString &dest, Task::Context &ctx, const char *step)
{
    Q_UNUSED(step);
    DownloadRequest req{ url, dest };
    req.resume = false;
    QString err;
    if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
        ctx.fail(err);
        return false;
    }
    return true;
}

QList<LoaderVersion> ModLoaderInstaller::fabricLoadersBlocking(const QString &mcVersion, Task::Context &ctx)
{
    const QString cache =
        QDir(loaderCacheDir()).filePath(QStringLiteral("fabric-%1.json").arg(mcVersion));
    const QUrl url(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1").arg(mcVersion));
    if (QFile::exists(cache) && NetworkStatus::instance().isEffectivelyOffline()) {
        bool ok = false;
        const QJsonDocument doc = readJsonFile(cache, &ok);
        if (ok && doc.isArray()) {
            return LoaderMeta::parseFabricLoaders(doc.array(), mcVersion);
        }
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        ctx.fail(QObject::tr("Loader list isn't cached and you're offline. Go online once to fetch it."));
        return {};
    }
    if (!downloadAndSave(url, cache, ctx, "fabric")) {
        return {};
    }
    bool ok = false;
    const QJsonDocument doc = readJsonFile(cache, &ok);
    if (!ok || !doc.isArray()) {
        ctx.fail(QObject::tr("Fabric sent an unexpected answer."));
        return {};
    }
    return LoaderMeta::parseFabricLoaders(doc.array(), mcVersion);
}

QList<LoaderVersion> ModLoaderInstaller::quiltLoadersBlocking(const QString &mcVersion, Task::Context &ctx)
{
    const QString cache = QDir(loaderCacheDir()).filePath(QStringLiteral("quilt-%1.json").arg(mcVersion));
    const QUrl url(QStringLiteral("https://meta.quiltmc.org/v3/versions/loader/%1").arg(mcVersion));
    if (QFile::exists(cache) && NetworkStatus::instance().isEffectivelyOffline()) {
        bool ok = false;
        const QJsonDocument doc = readJsonFile(cache, &ok);
        if (ok && doc.isArray()) {
            QJsonObject wrap;
            wrap[QStringLiteral("loader")] = doc.array();
            return LoaderMeta::parseQuiltLoaders(wrap, mcVersion);
        }
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        ctx.fail(QObject::tr("Loader list isn't cached and you're offline. Go online once to fetch it."));
        return {};
    }
    if (!downloadAndSave(url, cache, ctx, "quilt")) {
        return {};
    }
    bool ok = false;
    const QJsonDocument doc = readJsonFile(cache, &ok);
    if (!ok) {
        ctx.fail(QObject::tr("Quilt sent an unexpected answer."));
        return {};
    }
    if (doc.isArray()) {
        QJsonObject wrap;
        wrap[QStringLiteral("loader")] = doc.array();
        return LoaderMeta::parseQuiltLoaders(wrap, mcVersion);
    }
    if (doc.isObject()) {
        return LoaderMeta::parseQuiltLoaders(doc.object(), mcVersion);
    }
    ctx.fail(QObject::tr("Quilt sent an unexpected answer."));
    return {};
}

QList<LoaderVersion> ModLoaderInstaller::forgeVersionsBlocking(const QString &mcVersion, Task::Context &ctx)
{
    const QString cache = QDir(loaderCacheDir()).filePath(QStringLiteral("forge-promotions.json"));
    const QUrl promoUrl(QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"));
    QList<LoaderVersion> fromPromos;
    if (!NetworkStatus::instance().isEffectivelyOffline()) {
        if (downloadAndSave(promoUrl, cache, ctx, "forge")) {
            bool ok = false;
            const QJsonDocument doc = readJsonFile(cache, &ok);
            if (ok && doc.isObject()) {
                fromPromos = LoaderMeta::parseForgePromotions(doc.object().value(QStringLiteral("promos")).toObject(),
                                                             mcVersion);
            }
        }
    } else if (QFile::exists(cache)) {
        bool ok = false;
        const QJsonDocument doc = readJsonFile(cache, &ok);
        if (ok && doc.isObject()) {
            fromPromos = LoaderMeta::parseForgePromotions(doc.object().value(QStringLiteral("promos")).toObject(),
                                                         mcVersion);
        }
    }
    if (!fromPromos.isEmpty()) {
        return fromPromos;
    }
    // Fallback: Maven metadata for this MC line (forge versions embed the MC version).
    const QString metaCache = QDir(loaderCacheDir()).filePath(QStringLiteral("forge-maven.xml"));
    const QUrl metaUrl(QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml"));
    if (!NetworkStatus::instance().isEffectivelyOffline()) {
        if (!downloadAndSave(metaUrl, metaCache, ctx, "forge")) {
            return {};
        }
    } else if (!QFile::exists(metaCache)) {
        ctx.fail(QObject::tr("Loader list isn't cached and you're offline. Go online once to fetch it."));
        return {};
    }
    QFile f(metaCache);
    if (!f.open(QIODevice::ReadOnly)) {
        ctx.fail(QObject::tr("Couldn't read the Forge version list."));
        return {};
    }
    const QByteArray xml = f.readAll();
    QList<LoaderVersion> all = LoaderMeta::parseMavenMetadataXml(xml, mcVersion + QStringLiteral("-"));
    for (auto &lv : all) {
        lv.mcVersion = mcVersion;
        // Forge maven version is "<mc>-<forge>"; display the forge part.
        const QString full = lv.version;
        lv.version = full.mid(mcVersion.size() + 1);
    }
    // Newest-first: metadata is oldest-first.
    std::reverse(all.begin(), all.end());
    // Keep a sane window for the combo box.
    if (all.size() > 60) {
        all = all.mid(0, 60);
    }
    return all;
}

QList<LoaderVersion> ModLoaderInstaller::neoForgeVersionsBlocking(const QString &mcVersion, Task::Context &ctx)
{
    const QString metaCache = QDir(loaderCacheDir()).filePath(QStringLiteral("neoforge-maven.xml"));
    const QUrl metaUrl(
        QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"));
    if (!NetworkStatus::instance().isEffectivelyOffline()) {
        if (!downloadAndSave(metaUrl, metaCache, ctx, "neoforge")) {
            return {};
        }
    } else if (!QFile::exists(metaCache)) {
        ctx.fail(QObject::tr("Loader list isn't cached and you're offline. Go online once to fetch it."));
        return {};
    }
    QFile f(metaCache);
    if (!f.open(QIODevice::ReadOnly)) {
        ctx.fail(QObject::tr("Couldn't read the NeoForge version list."));
        return {};
    }
    QList<LoaderVersion> all = LoaderMeta::parseMavenMetadataXml(f.readAll());
    // NeoForge versions encode the MC version in the major (e.g. 20.4.x for MC 1.20.4).
    // Filter loosely: keep versions whose leading "XX.Y" matches the MC "1.XX.Y".
    const QStringList mcParts = mcVersion.split(QLatin1Char('.'));
    QList<LoaderVersion> kept;
    for (auto &lv : all) {
        lv.mcVersion = mcVersion;
        if (mcParts.size() >= 3) {
            const QString want = mcParts[1] + QStringLiteral(".") + mcParts[2]; // "20.4"
            if (!lv.version.startsWith(want) && !lv.version.startsWith(mcParts[1] + QStringLiteral("."))) {
                continue;
            }
        }
        kept.append(lv);
    }
    std::reverse(kept.begin(), kept.end());
    if (kept.size() > 60) {
        kept = kept.mid(0, 60);
    }
    if (kept.isEmpty()) {
        // Be honest rather than empty: show everything newest-first and let the
        // user pick (install validates by actually resolving the installer).
        std::reverse(all.begin(), all.end());
        for (auto &lv : all) {
            lv.mcVersion = mcVersion;
        }
        return all.mid(0, 60);
    }
    return kept;
}

LoaderProfile ModLoaderInstaller::loadCachedProfile(const QString &mcVersion, LoaderType type,
                                                    const QString &loaderVersion, QString *error) const
{
    LoaderProfile p;
    if (type == LoaderType::Vanilla || loaderVersion.isEmpty()) {
        return p; // no overlay: vanilla
    }
    const QString path = loaderProfileFile(mcVersion, type, loaderVersion);
    bool ok = false;
    const QJsonDocument doc = readJsonFile(path, &ok);
    if (!ok || !doc.isObject()) {
        if (error) {
            *error = NetworkStatus::instance().isEffectivelyOffline()
                ? QObject::tr("The %1 profile isn't downloaded and you're offline. Go online once to install it.")
                      .arg(loaderDisplayName(type))
                : QObject::tr("The %1 profile for %2 isn't installed yet. Install it first.")
                      .arg(loaderDisplayName(type))
                      .arg(mcVersion);
        }
        return p;
    }
    QString perr;
    p.overlay = ParsedVersion::fromJson(doc.object(), &perr);
    if (!perr.isEmpty() || p.overlay.mainClass.isEmpty()) {
        if (error) {
            *error = QObject::tr("The saved %1 profile is corrupt. Reinstall it while online.")
                         .arg(loaderDisplayName(type));
        }
        return p;
    }
    p.raw = doc.object();
    p.profileId = p.overlay.id;
    p.ok = true;
    return p;
}

ParsedVersion ModLoaderInstaller::effectiveVersion(const ParsedVersion &vanilla, const LoaderProfile &loader)
{
    if (!loader.ok) {
        return vanilla;
    }
    ParsedVersion merged = ParsedVersion::merge(vanilla, loader.overlay);
    merged.id = vanilla.id; // keep the vanilla id for folders/markers
    return merged;
}

bool ModLoaderInstaller::ensureLoaderFilesBlocking(const ParsedVersion &effective, int maxParallel,
                                                   Task::Context &ctx)
{
    const OsInfo os = currentOsInfo();
    const QSet<QString> features;
    const VersionInstaller::WorkPlan plan =
        VersionInstaller::planLibraries(effective, os, features, m_paths);
    QList<DownloadRequest> missing;
    for (const auto &req : plan.jarDownloads) {
        if (!DownloadManager::fileUpToDate(req.destPath, req)) {
            missing.append(req);
        }
    }
    for (const auto &job : plan.natives) {
        if (job.url.isEmpty()) {
            continue;
        }
        DownloadRequest req{ QUrl(job.url), job.zipPath };
        req.expectedSha1 = job.sha1.toLatin1();
        req.expectedSize = job.size;
        if (!DownloadManager::fileUpToDate(req.destPath, req)) {
            missing.append(req);
        }
    }
    if (missing.isEmpty()) {
        return true;
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        ctx.fail(QObject::tr("Some loader files are missing and you're offline. Go online once to fetch them."));
        return false;
    }
    QString err;
    if (!DownloadManager::downloadManyBlocking(missing, maxParallel, ctx, &err)) {
        ctx.fail(err);
        return false;
    }
    return true;
}

bool ModLoaderInstaller::installBlocking(const QString &mcVersion, LoaderType type, const QString &loaderVersion,
                                         int maxParallel, Task::Context &ctx, LoaderProfile *outProfile)
{
    if (type == LoaderType::Vanilla) {
        if (outProfile) {
            *outProfile = LoaderProfile{};
        }
        ctx.report(1, 1, QObject::tr("Done"));
        return true;
    }
    bool ok = false;
    if (type == LoaderType::Fabric || type == LoaderType::Quilt) {
        ok = installFabricLikeBlocking(mcVersion, type, loaderVersion, maxParallel, ctx, outProfile);
    } else {
        ok = installForgeLikeBlocking(mcVersion, type, loaderVersion, maxParallel, ctx, outProfile);
    }
    emit installFinished(mcVersion, loaderTypeToString(type), ok);
    return ok;
}

bool ModLoaderInstaller::installFabricLikeBlocking(const QString &mcVersion, LoaderType type,
                                                   const QString &loaderVersion, int maxParallel,
                                                   Task::Context &ctx, LoaderProfile *out)
{
    Q_UNUSED(maxParallel);
    QString ver = loaderVersion.trimmed();
    if (ver.isEmpty()) {
        ctx.report(0, 10, QObject::tr("Finding the recommended %1 version…").arg(loaderDisplayName(type)));
        const QList<LoaderVersion> all = (type == LoaderType::Fabric) ? fabricLoadersBlocking(mcVersion, ctx)
                                                                      : quiltLoadersBlocking(mcVersion, ctx);
        if (all.isEmpty()) {
            if (ctx.isCancelled()) {
                ctx.fail(QObject::tr("Cancelled"));
            } else if (true) {
                // Error already recorded by the list fetcher in most paths.
            }
            return false;
        }
        ver = LoaderMeta::latestStable(all);
        if (ver.isEmpty()) {
            ctx.fail(QObject::tr("No %1 version found for %2.").arg(loaderDisplayName(type)).arg(mcVersion));
            return false;
        }
    }
    QUrl profileUrl;
    if (type == LoaderType::Fabric) {
        profileUrl = QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json")
                              .arg(mcVersion, ver));
    } else {
        profileUrl = QUrl(QStringLiteral("https://meta.quiltmc.org/v3/versions/loader/%1/%2/profile/json")
                              .arg(mcVersion, ver));
    }
    const QString dest = loaderProfileFile(mcVersion, type, ver);
    ctx.report(2, 10, QObject::tr("Downloading the %1 profile…").arg(loaderDisplayName(type)));
    if (!downloadAndSave(profileUrl, dest, ctx, "profile")) {
        return false;
    }
    QString err;
    LoaderProfile prof = loadCachedProfile(mcVersion, type, ver, &err);
    if (!prof.ok) {
        ctx.fail(err);
        return false;
    }
    // Download any loader libraries missing from the shared cache.
    ctx.report(5, 10, QObject::tr("Downloading %1 files…").arg(loaderDisplayName(type)));
    // Merge needs the vanilla base only for file planning at install time;
    // full merge happens at launch. Plan against the overlay alone plus a
    // minimal vanilla shell so VersionInstaller::planLibraries works.
    ParsedVersion shell;
    shell.id = mcVersion;
    ParsedVersion eff = ParsedVersion::merge(shell, prof.overlay);
    eff.id = mcVersion;
    if (!ensureLoaderFilesBlocking(eff, 8, ctx)) {
        return false;
    }
    if (out) {
        *out = prof;
    }
    ctx.report(1, 1, QObject::tr("Done"));
    Logger::info(QStringLiteral("Installed %1 %2 for %3").arg(loaderDisplayName(type)).arg(ver).arg(mcVersion));
    return true;
}

// Forge/NeoForge installer flow: download the official installer jar,
// extract install_profile.json (+ the version json it carries or references),
// download + verify all libraries, run headless processors with the managed
// Java, then save the resulting version overlay for offline launch.
bool ModLoaderInstaller::installForgeLikeBlocking(const QString &mcVersion, LoaderType type,
                                                  const QString &loaderVersion, int maxParallel,
                                                  Task::Context &ctx, LoaderProfile *out)
{
    QString ver = loaderVersion.trimmed();
    if (ver.isEmpty()) {
        ctx.report(0, 100, QObject::tr("Finding the recommended %1 version…").arg(loaderDisplayName(type)));
        const QList<LoaderVersion> all = (type == LoaderType::Forge) ? forgeVersionsBlocking(mcVersion, ctx)
                                                                     : neoForgeVersionsBlocking(mcVersion, ctx);
        if (all.isEmpty()) {
            return false;
        }
        ver = LoaderMeta::latestStable(all);
        if (ver.isEmpty()) {
            ctx.fail(QObject::tr("No %1 version found for %2.").arg(loaderDisplayName(type)).arg(mcVersion));
            return false;
        }
    }
    // Installer coordinates.
    QString installerUrl, installerName;
    if (type == LoaderType::Forge) {
        installerName = QStringLiteral("forge-%1-%2-installer.jar").arg(mcVersion, ver);
        installerUrl = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/%3")
                           .arg(mcVersion, ver, installerName);
    } else {
        installerName = QStringLiteral("neoforge-%1-installer.jar").arg(ver);
        installerUrl = QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/%2")
                           .arg(ver, installerName);
    }
    const QString installerPath = QDir(loaderCacheDir()).filePath(installerName);
    ctx.report(5, 100, QObject::tr("Downloading the %1 installer…").arg(loaderDisplayName(type)));
    if (!QFile::exists(installerPath) || QFileInfo(installerPath).size() <= 0) {
        if (NetworkStatus::instance().isEffectivelyOffline()) {
            ctx.fail(QObject::tr("The %1 installer isn't cached and you're offline. Go online once to fetch it.")
                         .arg(loaderDisplayName(type)));
            return false;
        }
        if (!downloadAndSave(QUrl(installerUrl), installerPath, ctx, "installer")) {
            return false;
        }
    }
    // Extract the installer payload to a scratch dir.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        ctx.fail(QObject::tr("Couldn't create a temporary folder."));
        return false;
    }
    QString zerr;
    if (!ZipUtil::extractZipFile(installerPath, scratch.path(), {}, &zerr)) {
        ctx.fail(QObject::tr("The %1 installer is corrupt (%2).").arg(loaderDisplayName(type)).arg(zerr));
        return false;
    }
    auto readScratchJson = [&](const QString &name, QJsonObject *outObj) -> bool {
        QFile f(QDir(scratch.path()).filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            return false;
        }
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
        if (e.error != QJsonParseError::NoError || !doc.isObject()) {
            return false;
        }
        *outObj = doc.object();
        return true;
    };
    QJsonObject installProfile;
    const bool hasInstallProfile = readScratchJson(QStringLiteral("install_profile.json"), &installProfile);
    QJsonObject versionJson;
    const bool hasVersionJson = readScratchJson(QStringLiteral("version.json"), &versionJson);
    QJsonObject installJsonLegacy;
    const bool hasInstallJson = readScratchJson(QStringLiteral("install.json"), &installJsonLegacy);
    if (!hasInstallProfile && !hasInstallJson) {
        ctx.fail(QObject::tr("The %1 installer has an unexpected layout.").arg(loaderDisplayName(type)));
        return false;
    }
    // The overlay version json: modern install_profile.json references it via
    // "versionInfo" inline or a "version" file; legacy install.json carries
    // "versionInfo" too. Prefer inline, then version.json from the jar.
    QJsonObject overlayObj;
    if (hasInstallProfile && installProfile.contains(QStringLiteral("versionInfo"))) {
        overlayObj = installProfile.value(QStringLiteral("versionInfo")).toObject();
    } else if (hasInstallJson && installJsonLegacy.contains(QStringLiteral("versionInfo"))) {
        overlayObj = installJsonLegacy.value(QStringLiteral("versionInfo")).toObject();
    } else if (hasVersionJson) {
        overlayObj = versionJson;
    } else {
        ctx.fail(QObject::tr("The %1 installer carries no version info.").arg(loaderDisplayName(type)));
        return false;
    }
    QString perr;
    ParsedVersion overlay = ParsedVersion::fromJson(overlayObj, &perr);
    if (!perr.isEmpty() || overlay.libraries.isEmpty()) {
        ctx.fail(QObject::tr("The %1 version info is incomplete.").arg(loaderDisplayName(type)));
        return false;
    }
    // Download installer libraries (install_profile "libraries" + overlay libs).
    ctx.report(20, 100, QObject::tr("Downloading %1 libraries…").arg(loaderDisplayName(type)));
    QList<DownloadRequest> libReqs;
    auto collectLibs = [&](const QJsonObject &src) {
        for (const auto &v : src.value(QStringLiteral("libraries")).toArray()) {
            const Library lib = Library::fromJson(v.toObject());
            if (lib.hasArtifact && !lib.artifact.url.isEmpty() && !lib.artifact.path.isEmpty()) {
                DownloadRequest req{ QUrl(lib.artifact.url),
                                     QDir(m_paths.librariesDir).filePath(lib.artifact.path) };
                req.expectedSha1 = lib.artifact.sha1.toLatin1();
                if (!DownloadManager::fileUpToDate(req.destPath, req)) {
                    libReqs.append(req);
                }
            }
        }
    };
    if (hasInstallProfile) {
        collectLibs(installProfile);
    }
    if (hasInstallJson) {
        collectLibs(installJsonLegacy);
    }
    collectLibs(overlayObj);
    if (!libReqs.isEmpty()) {
        if (NetworkStatus::instance().isEffectivelyOffline()) {
            ctx.fail(QObject::tr("Some %1 files are missing and you're offline. Go online once to fetch them.")
                         .arg(loaderDisplayName(type)));
            return false;
        }
        QString derr;
        if (!DownloadManager::downloadManyBlocking(libReqs, maxParallel, ctx, &derr)) {
            ctx.fail(derr);
            return false;
        }
    }
    // Run processors headless (modern format). Legacy installers without
    // processors need nothing more. Processor entries look like:
    //   {jar, classpath[], args[], outputs?{...}} with {LIBRARY}, {MINECRAFT_JAR} vars.
    const QJsonArray processors = installProfile.value(QStringLiteral("processors")).toArray();
    if (!processors.isEmpty()) {
        ctx.report(60, 100, QObject::tr("Running the %1 installer steps…").arg(loaderDisplayName(type)));
        // Find a Java to run processors with (same rule as the game).
        QString javaExe;
        {
            // Best-effort: PATH java; the prepare pipeline ensures the right
            // managed JVM separately before launch.
            javaExe = QStandardPaths::findExecutable(QStringLiteral("java"));
#if defined(Q_OS_WIN)
            if (javaExe.isEmpty()) {
                javaExe = QStringLiteral("java.exe");
            }
#endif
        }
        // Processor jars live in the installer payload under maven/ or as libs.
        // Extract the whole installer into the scratch dir already done; run each.
        int step = 0;
        for (const auto &pv : processors) {
            if (ctx.isCancelled()) {
                ctx.fail(QObject::tr("Cancelled"));
                return false;
            }
            const QJsonObject po = pv.toObject();
            const QString jarCoord = po.value(QStringLiteral("jar")).toString();
            const QJsonArray cp = po.value(QStringLiteral("classpath")).toArray();
            QJsonArray args = po.value(QStringLiteral("args")).toArray();
            // Resolve {placeholders}: map the few the installers actually use.
            auto resolveVar = [&](QString s) -> QString {
                s.replace(QStringLiteral("{MINECRAFT_JAR}"), m_paths.clientJar(mcVersion));
                s.replace(QStringLiteral("{MINECRAFT_VERSION}"), mcVersion);
                s.replace(QStringLiteral("{ROOT}"), QDir::currentPath());
                s.replace(QStringLiteral("{INSTALLER}"), installerPath);
                s.replace(QStringLiteral("{BINPATCH}"), scratch.path());
                // {LIBRARY:group:artifact:version[:classifier]} -> abs path
                static const QRegularExpression libRe(QStringLiteral("\\{LIBRARY:([^}]+)\\}"));
                QRegularExpressionMatch m;
                while ((m = libRe.match(s)).hasMatch()) {
                    const QString coord = m.captured(1);
                    s.replace(m.captured(0), QDir(m_paths.librariesDir).filePath(GamePaths::mavenPath(coord)));
                }
                return s;
            };
            QString procJar = resolveVar(jarCoord);
            if (procJar.startsWith(QStringLiteral("[")) && procJar.endsWith(QStringLiteral("]"))) {
                procJar = procJar.mid(1, procJar.size() - 2);
            }
            QStringList cpAbs;
            for (const auto &c : cp) {
                QString s = resolveVar(c.toString());
                if (s.startsWith(QStringLiteral("[")) && s.endsWith(QStringLiteral("]"))) {
                    s = s.mid(1, s.size() - 2);
                }
                cpAbs.append(s);
            }
            // Processor main class: read from the jar manifest (Main-Class).
            QString mainClass;
            {
                // Blend: look for META-INF/MANIFEST.MF in scratch if the jar
                // was a maven/ payload entry; else ask the jar file itself.
                QString jarFile = procJar;
                if (!QFile::exists(jarFile)) {
                    // Try maven layout under scratch.
                    const QString cand = QDir(scratch.path()).filePath(GamePaths::mavenPath(jarCoord));
                    if (QFile::exists(cand)) {
                        jarFile = cand;
                    }
                }
                if (QFile::exists(jarFile)) {
                    QTemporaryDir manTmp;
                    if (manTmp.isValid()) {
                        QString merr;
                        // Extract just the manifest via miniz listing is overkill;
                        // read the zip central directory through our extractor filtered
                        // to META-INF/ is fine (small jars).
                        QDir().mkpath(manTmp.path());
                        if (ZipUtil::extractZipFile(jarFile, manTmp.path(), { QStringLiteral("docs/") }, &merr)) {
                            QFile man(QDir(manTmp.path()).filePath(QStringLiteral("META-INF/MANIFEST.MF")));
                            if (man.open(QIODevice::ReadOnly)) {
                                const QString text = QString::fromUtf8(man.readAll());
                                static const QRegularExpression re(QStringLiteral("Main-Class:\\s*(\\S+)"));
                                const auto mm = re.match(text);
                                if (mm.hasMatch()) {
                                    mainClass = mm.captured(1).trimmed();
                                }
                            }
                        }
                    }
                    procJar = jarFile;
                }
            }
            if (mainClass.isEmpty() || !QFile::exists(procJar)) {
                ctx.fail(QObject::tr("The %1 installer step %2 is missing its files. (%3)")
                             .arg(loaderDisplayName(type))
                             .arg(step + 1)
                             .arg(jarCoord));
                return false;
            }
            QStringList pargs;
            for (const auto &a : args) {
                pargs.append(resolveVar(a.toString()));
            }
#if defined(Q_OS_WIN)
            const QString sep = QStringLiteral(";");
#else
            const QString sep = QStringLiteral(":");
#endif
            const QString cpStr = (QStringList{ procJar } + cpAbs).join(sep);
            QProcess proc;
            proc.setProgram(javaExe);
            QStringList procArgs;
            procArgs.append(QStringLiteral("-cp"));
            procArgs.append(cpStr);
            procArgs.append(mainClass);
            procArgs.append(pargs);
            proc.setArguments(procArgs);
            proc.setWorkingDirectory(scratch.path());
            proc.start();
            if (!proc.waitForFinished(300000)) {
                ctx.fail(QObject::tr("The %1 installer timed out at step %2.").arg(loaderDisplayName(type)).arg(step + 1));
                return false;
            }
            if (proc.exitCode() != 0) {
                const QString out = QString::fromLocal8Bit(proc.readAllStandardError() + proc.readAllStandardOutput())
                                        .trimmed()
                                        .left(800);
                ctx.fail(QObject::tr("The %1 installer failed at step %2: %3")
                             .arg(loaderDisplayName(type))
                             .arg(step + 1)
                             .arg(out.isEmpty() ? proc.errorString() : out));
                return false;
            }
            // Honor declared outputs: fail loudly if an expected file is absent.
            const QJsonObject outputs = po.value(QStringLiteral("outputs")).toObject();
            for (auto it = outputs.begin(); it != outputs.end(); ++it) {
                const QString want = resolveVar(it.value().toString());
                if (!QFile::exists(want) || QFileInfo(want).size() <= 0) {
                    ctx.fail(QObject::tr("The %1 installer didn't produce %2.").arg(loaderDisplayName(type)).arg(want));
                    return false;
                }
            }
            ++step;
            ctx.report(60 + step * 30 / qMax(1, processors.size()), 100,
                       QObject::tr("Running the installer (%1/%2)…").arg(step).arg(processors.size()));
        }
    }
    // Save the overlay for offline launch + later merges.
    overlayObj[QStringLiteral("id")] = QStringLiteral("%1-%2-%3").arg(mcVersion, loaderTypeToString(type), ver);
    overlayObj[QStringLiteral("inheritsFrom")] = mcVersion;
    const QString dest = loaderProfileFile(mcVersion, type, ver);
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        ctx.fail(QObject::tr("Couldn't save the %1 profile.").arg(loaderDisplayName(type)));
        return false;
    }
    f.write(QJsonDocument(overlayObj).toJson(QJsonDocument::Indented));
    f.close();
    // Ensure overlay libraries exist (post-processor outputs included).
    QString perr2;
    ParsedVersion saved = ParsedVersion::fromJson(overlayObj, &perr2);
    if (!perr2.isEmpty()) {
        ctx.fail(QObject::tr("The saved %1 profile is invalid.").arg(loaderDisplayName(type)));
        return false;
    }
    if (!ensureLoaderFilesBlocking(saved, maxParallel, ctx)) {
        return false;
    }
    if (out) {
        out->ok = true;
        out->overlay = saved;
        out->raw = overlayObj;
        out->profileId = saved.id;
    }
    ctx.report(1, 1, QObject::tr("Done"));
    Logger::info(QStringLiteral("Installed %1 %2 for %3").arg(loaderDisplayName(type)).arg(ver).arg(mcVersion));
    return true;
}

#include "ModLoader.moc"
