#include "VersionInstaller.h"

#include "DownloadManager.h"
#include "Logger.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "ZipUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

VersionInstaller::VersionInstaller(const GamePaths &paths, MojangApi *api, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_api(api)
    , m_downloads(downloads)
{
}

Task *VersionInstaller::installTask(const QString &versionId, int maxParallel, QObject *owner)
{
    auto *t = new LambdaTask(
        tr("Install Minecraft %1").arg(versionId),
        [this, versionId, maxParallel](Task::Context &ctx) { return installBlocking(versionId, maxParallel, ctx); },
        owner ? owner : this);
    connect(t, &Task::finished, this, [this, versionId](bool ok) { emit installFinished(versionId, ok); });
    return t;
}

VersionInstaller::WorkPlan VersionInstaller::planLibraries(const ParsedVersion &merged, const OsInfo &os,
                                                          const QSet<QString> &features, const GamePaths &paths)
{
    WorkPlan plan;
    for (const auto &lib : merged.libraries) {
        if (!lib.activeFor(os, features)) {
            continue;
        }
        plan.activeLibs.append(lib);
        const QString classifier = lib.nativeClassifierFor(os);
        if (!classifier.isEmpty()) {
            // Legacy native library: download the classifier jar, extract later.
            WorkPlan::NativeJob job;
            job.coord = lib.name;
            job.classifier = classifier;
            job.extractExclude = lib.extractExclude;
            job.zipPath = QDir(paths.librariesDir).filePath(GamePaths::mavenPath(lib.name, classifier));
            const auto it = lib.classifiers.find(classifier);
            if (it != lib.classifiers.end()) {
                job.url = it->url;
                job.sha1 = it->sha1;
                job.size = it->size;
            }
            // No classifier metadata: path is derived, no URL to fetch
            // (ancient files always ship classifiers; skipped at extract).
            plan.natives.append(job);
            continue;
        }
        if (lib.isNativeJar()) {
            // Modern native entry (classifier is the 4th maven part, OS-gated
            // by rules): download the artifact jar, extract it, keep it off
            // the classpath.
            WorkPlan::NativeJob job;
            job.coord = lib.name;
            job.classifier = lib.nameClassifier();
            job.extractExclude = lib.extractExclude;
            job.zipPath = QDir(paths.librariesDir).filePath(
                lib.artifact.path.isEmpty() ? GamePaths::mavenPath(lib.name) : lib.artifact.path);
            if (lib.hasArtifact) {
                job.url = lib.artifact.url;
                job.sha1 = lib.artifact.sha1;
                job.size = lib.artifact.size;
            }
            plan.natives.append(job);
            continue;
        }
        if (lib.hasArtifact && !lib.artifact.path.isEmpty() && !lib.artifact.url.isEmpty()) {
            DownloadRequest req;
            req.url = QUrl(lib.artifact.url);
            req.destPath = QDir(paths.librariesDir).filePath(lib.artifact.path);
            req.expectedSha1 = lib.artifact.sha1.toLatin1();
            req.expectedSize = lib.artifact.size;
            plan.jarDownloads.append(req);
        }
    }
    return plan;
}

bool VersionInstaller::isInstalled(const QString &versionId, QString *missing) const
{
    bool ok = false;
    QString err;
    const ParsedVersion merged = m_api->loadMergedVersion(versionId, &ok, &err);
    if (!ok) {
        if (missing) {
            *missing = err;
        }
        return false;
    }
    return isInstalled(merged, missing);
}

bool VersionInstaller::quickIsInstalled(const QString &versionId) const
{
    const QString jar = m_paths.clientJar(versionId);
    return QFile::exists(jar) && QFileInfo(jar).size() > 0 && QFile::exists(m_paths.readyMarker(versionId));
}

bool VersionInstaller::isInstalled(const ParsedVersion &merged, QString *missing) const
{
    const OsInfo os = currentOsInfo();
    const QSet<QString> features; // install-time: no demo/resolution features
    // Client jar.
    const QString jar = m_paths.clientJar(merged.id);
    if (!QFile::exists(jar) || QFileInfo(jar).size() <= 0) {
        if (missing) {
            *missing = tr("Client jar is missing.");
        }
        return false;
    }
    // Libraries.
    const WorkPlan plan = planLibraries(merged, os, features, m_paths);
    for (const auto &lib : plan.activeLibs) {
        if (!lib.nativeClassifierFor(os).isEmpty() || lib.isNativeJar()) {
            continue; // natives: covered by the marker + zip checks below
        }
        const QString p = lib.artifactAbsPath(m_paths.librariesDir);
        if (p.isEmpty() || !QFile::exists(p) || QFileInfo(p).size() <= 0) {
            if (missing) {
                *missing = tr("Library is missing: %1").arg(lib.name);
            }
            return false;
        }
    }
    for (const auto &job : plan.natives) {
        const QString p = QDir(m_paths.librariesDir).filePath(GamePaths::mavenPath(job.coord, job.classifier));
        if (!QFile::exists(p) || QFileInfo(p).size() <= 0) {
            if (missing) {
                *missing = tr("Native is missing: %1").arg(job.coord);
            }
            return false;
        }
    }
    if (!plan.natives.isEmpty() && !QFile::exists(m_paths.nativesMarker(merged.id))) {
        if (missing) {
            *missing = tr("Natives are not extracted yet.");
        }
        return false;
    }
    // Asset index + objects.
    if (!merged.assetIndex.id.isEmpty()) {
        if (!QFile::exists(m_paths.assetIndexFile(merged.assetIndex.id))) {
            if (missing) {
                *missing = tr("Asset index is missing.");
            }
            return false;
        }
        QFile idx(m_paths.assetIndexFile(merged.assetIndex.id));
        if (idx.open(QIODevice::ReadOnly)) {
            QJsonParseError perr{};
            const QJsonDocument doc = QJsonDocument::fromJson(idx.readAll(), &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject objs = doc.object().value(QStringLiteral("objects")).toObject();
                for (auto it = objs.begin(); it != objs.end(); ++it) {
                    const QString hash = it.value().toObject().value(QStringLiteral("hash")).toString();
                    const QString p = m_paths.assetObjectFile(hash);
                    if (!QFile::exists(p) || QFileInfo(p).size() <= 0) {
                        if (missing) {
                            *missing = tr("Some asset files are missing.");
                        }
                        return false;
                    }
                }
            }
        }
    }
    // Logging config.
    if (merged.hasLogging && !merged.logging.fileId.isEmpty()) {
        if (!QFile::exists(m_paths.logConfigFile(merged.logging.fileId))) {
            if (missing) {
                *missing = tr("Logging config is missing.");
            }
            return false;
        }
    }
    return true;
}

bool VersionInstaller::installBlocking(const QString &versionId, int maxParallel, Task::Context &ctx)
{
    m_paths.ensureBaseDirs();
    m_paths.ensureVersionDirs(versionId);
    const OsInfo os = currentOsInfo();
    const QSet<QString> features; // install-time: no optional features

    // --- 1. Version JSON (+ inheritsFrom chain) ---
    ctx.report(0, 1000, tr("Reading version info…"));
    bool ok = false;
    QString err;
    ParsedVersion merged = m_api->loadMergedVersion(versionId, &ok, &err);
    if (!ok) {
        // Try fetching while online, then re-read.
        if (NetworkStatus::instance().isEffectivelyOffline()) {
            ctx.fail(err);
            return false;
        }
        ctx.report(10, 1000, tr("Downloading version info…"));
        // Find the manifest URL for this id (and any missing base along the way).
        VersionManifest manifest = m_api->cachedManifest(&ok);
        if (!ok) {
            ctx.fail(tr("Version list isn't downloaded yet. Check your connection and refresh."));
            return false;
        }
        auto urlFor = [&](const QString &id) -> QString {
            for (const auto &e : manifest.versions) {
                if (e.id == id) {
                    return e.url;
                }
            }
            return {};
        };
        QStringList chain{ versionId };
        // Walk inheritsFrom using whatever is cached, fetching as needed.
        for (int depth = 0; depth < 5; ++depth) {
            const QString cur = chain.last();
            QString jsonPath = m_paths.versionJsonFile(cur);
            if (!QFile::exists(jsonPath)) {
                const QString u = urlFor(cur);
                if (u.isEmpty()) {
                    // Might be a loader-provided id or an unknown id.
                    ctx.fail(tr("Unknown version “%1”.").arg(cur));
                    return false;
                }
                DownloadRequest req{ QUrl(u), jsonPath };
                req.resume = false;
                if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
                    ctx.fail(err);
                    return false;
                }
            }
            QFile f(jsonPath);
            if (!f.open(QIODevice::ReadOnly)) {
                ctx.fail(tr("Couldn't read version info."));
                return false;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            const QString parent = doc.object().value(QStringLiteral("inheritsFrom")).toString();
            if (parent.isEmpty()) {
                break;
            }
            if (chain.contains(parent)) {
                ctx.fail(tr("Circular inheritsFrom chain."));
                return false;
            }
            chain.append(parent);
        }
        merged = m_api->loadMergedVersion(versionId, &ok, &err);
        if (!ok) {
            ctx.fail(err);
            return false;
        }
    }
    if (merged.mainClass.isEmpty() || merged.client.url.isEmpty()) {
        ctx.fail(tr("Version info for “%1” is incomplete.").arg(versionId));
        return false;
    }

    // --- 2. Assemble the download list ---
    ctx.report(60, 1000, tr("Planning downloads…"));
    QList<DownloadRequest> files;

    DownloadRequest clientReq{ QUrl(merged.client.url), m_paths.clientJar(versionId) };
    clientReq.expectedSha1 = merged.client.sha1.toLatin1();
    clientReq.expectedSize = merged.client.size;
    files.append(clientReq);

    const WorkPlan plan = planLibraries(merged, os, features, m_paths);
    files.append(plan.jarDownloads);
    QList<VersionInstaller::WorkPlan::NativeJob> nativeJobs = plan.natives;
    for (const auto &job : nativeJobs) {
        if (job.url.isEmpty()) {
            continue; // nothing to fetch; extract step skips with a warning
        }
        DownloadRequest req;
        req.url = QUrl(job.url);
        req.destPath = job.zipPath;
        req.expectedSha1 = job.sha1.toLatin1();
        req.expectedSize = job.size;
        files.append(req);
    }

    // Asset index (needed to enumerate objects).
    const bool needIndex = !merged.assetIndex.id.isEmpty() && !merged.assetIndex.url.isEmpty();
    if (needIndex) {
        DownloadRequest req{ QUrl(merged.assetIndex.url), m_paths.assetIndexFile(merged.assetIndex.id) };
        req.expectedSha1 = merged.assetIndex.sha1.toLatin1();
        req.expectedSize = merged.assetIndex.size;
        req.resume = false;
        files.append(req);
    }
    if (merged.hasLogging && !merged.logging.url.isEmpty()) {
        DownloadRequest req{ QUrl(merged.logging.url), m_paths.logConfigFile(merged.logging.fileId) };
        req.expectedSha1 = merged.logging.sha1.toLatin1();
        req.expectedSize = merged.logging.size;
        req.resume = false;
        files.append(req);
    }

    // --- 3. Download phase 1 (everything enumerable now) ---
    ctx.report(80, 1000, tr("Downloading game files…"));
    struct RemapCtx : public Task::Context {
        Task::Context &outer;
        int from, to;
        explicit RemapCtx(Task::Context &o, int f, int t)
            : outer(o)
            , from(f)
            , to(t)
        {
        }
        void report(qint64 r, qint64 t, const QString &msg = {}) override
        {
            const double frac = (t > 0 && r >= 0) ? qBound(0.0, double(r) / double(t), 1.0) : 0.0;
            outer.report(from + qint64(frac * (to - from)), 1000, msg);
        }
        bool isCancelled() const override { return outer.isCancelled(); }
        void fail(const QString &m) override { outer.fail(m); }
    };
    {
        RemapCtx sub(ctx, 80, 850);
        if (!DownloadManager::downloadManyBlocking(files, maxParallel, sub, &err)) {
            ctx.fail(err);
            return false;
        }
    }

    // --- 4. Asset objects (index now on disk) ---
    if (needIndex) {
        QFile idx(m_paths.assetIndexFile(merged.assetIndex.id));
        if (!idx.open(QIODevice::ReadOnly)) {
            ctx.fail(tr("Asset index is unreadable."));
            return false;
        }
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(idx.readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            ctx.fail(tr("Asset index is corrupt."));
            return false;
        }
        const QJsonObject objs = doc.object().value(QStringLiteral("objects")).toObject();
        QList<DownloadRequest> objReqs;
        objReqs.reserve(objs.size());
        for (auto it = objs.begin(); it != objs.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            const QString hash = o.value(QStringLiteral("hash")).toString();
            const qint64 size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
            if (hash.isEmpty()) {
                continue;
            }
            DownloadRequest req{ QUrl(QStringLiteral("https://resources.download.minecraft.net/%1/%2")
                                           .arg(hash.left(2))
                                           .arg(hash)),
                                m_paths.assetObjectFile(hash) };
            req.expectedSha1 = hash.toLatin1(); // object name IS the sha1
            req.expectedSize = size;
            objReqs.append(req);
        }
        // Legacy/virtual materialization happens after objects land.
        RemapCtx sub(ctx, 850, 950);
        if (!DownloadManager::downloadManyBlocking(objReqs, maxParallel, sub, &err)) {
            ctx.fail(err);
            return false;
        }
        // Old versions: mirror objects into resources/ or virtual dirs.
        const QString assetsKind = merged.assetsRef; // e.g. "legacy", "pre-1.6", or modern id
        const bool isLegacy = (assetsKind == QStringLiteral("legacy"));
        const bool isVirtual = assetsKind.startsWith(QStringLiteral("pre-"));
        if ((isLegacy || isVirtual) && !objs.isEmpty()) {
            ctx.report(960, 1000, tr("Preparing old-style resources…"));
            const QString gameDir = m_paths.gameDir(versionId);
            for (auto it = objs.begin(); it != objs.end(); ++it) {
                if (ctx.isCancelled()) {
                    ctx.fail(tr("Cancelled"));
                    return false;
                }
                const QString objName = it.key(); // e.g. "sounds/mob/pig/say1.ogg"
                const QString hash = it.value().toObject().value(QStringLiteral("hash")).toString();
                const QString src = m_paths.assetObjectFile(hash);
                QString dst;
                if (isLegacy) {
                    dst = QDir(gameDir).filePath(QStringLiteral("resources/%1").arg(objName));
                } else {
                    dst = QDir(m_paths.assetsDir)
                              .filePath(QStringLiteral("virtual/%1/%2").arg(merged.assetIndex.id).arg(objName));
                }
                if (QFile::exists(dst) && QFileInfo(dst).size() == QFileInfo(src).size()) {
                    continue;
                }
                QDir().mkpath(QFileInfo(dst).absolutePath());
                QFile::remove(dst);
                if (!QFile::copy(src, dst)) {
                    ctx.fail(tr("Couldn't prepare resources for old versions."));
                    return false;
                }
            }
        }
    }

    // --- 5. Native extraction ---
    if (!nativeJobs.isEmpty()) {
        ctx.report(965, 1000, tr("Extracting natives…"));
        QDir(m_paths.nativesDir(versionId)).removeRecursively();
        QDir().mkpath(m_paths.nativesDir(versionId));
        for (const auto &job : nativeJobs) {
            if (ctx.isCancelled()) {
                ctx.fail(tr("Cancelled"));
                return false;
            }
            const QString zip = job.zipPath;
            if (!QFile::exists(zip)) {
                // No URL was ever available for this native (ancient file):
                // skip rather than fail the whole install.
                Logger::warning(QStringLiteral("Native jar has no download, skipping extract: %1").arg(job.coord));
                continue;
            }
            int n = 0;
            if (!ZipUtil::extractZipFile(zip, m_paths.nativesDir(versionId), job.extractExclude, &err, &n)) {
                ctx.fail(tr("Couldn't extract natives (%1).").arg(err));
                return false;
            }
        }
        QFile marker(m_paths.nativesMarker(versionId));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
            marker.write(QByteArray::number(nativeJobs.size()));
            marker.close();
        }
    }

    // --- 6. Ready marker ---
    {
        QFile ready(m_paths.readyMarker(versionId));
        if (ready.open(QIODevice::WriteOnly | QIODevice::Text)) {
            ready.write(merged.mainClass.toUtf8());
            ready.close();
        }
    }
    ctx.report(1000, 1000, tr("Done"));
    Logger::info(QStringLiteral("Installed vanilla %1").arg(versionId));
    return true;
}
