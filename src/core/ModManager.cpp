#include "ModManager.h"

#include "DownloadManager.h"
#include "Instance.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

ModManager::ModManager(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

QString ModManager::modsDir(const QString &dataDir, const QString &instanceId)
{
    return QDir(QDir(dataDir).filePath(QStringLiteral("instances/%1/game/mods").arg(instanceId))).absolutePath();
}

QString ModManager::sidecarDir(const QString &dataDir, const QString &instanceId)
{
    return QDir(modsDir(dataDir, instanceId)).filePath(QStringLiteral(".hearth"));
}

QString ModManager::sidecarPath(const QString &dataDir, const QString &instanceId, const QString &baseName)
{
    return QDir(sidecarDir(dataDir, instanceId)).filePath(baseName + QStringLiteral(".json"));
}

static InstalledMod readOne(const QString &dataDir, const QString &instanceId, const QString &modsPath,
                            const QString &fileName)
{
    InstalledMod m;
    m.fileName = fileName;
    m.enabled = !fileName.endsWith(QStringLiteral(".disabled"));
    QString base = fileName;
    if (!m.enabled) {
        base.chop(QStringLiteral(".disabled").size());
    }
    if (base.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
        base.chop(4);
    }
    m.baseName = base;
    m.title = base;
    m.manual = true;
    m.size = QFileInfo(QDir(modsPath).filePath(fileName)).size();
    QFile sc(ModManager::sidecarPath(dataDir, instanceId, base));
    if (sc.open(QIODevice::ReadOnly)) {
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(sc.readAll(), &e);
        if (e.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject o = doc.object();
            m.projectId = o.value(QStringLiteral("projectId")).toString();
            m.slug = o.value(QStringLiteral("slug")).toString();
            const QString t = o.value(QStringLiteral("title")).toString();
            if (!t.isEmpty()) {
                m.title = t;
            }
            m.versionId = o.value(QStringLiteral("versionId")).toString();
            m.versionNumber = o.value(QStringLiteral("versionNumber")).toString();
            m.fileUrl = o.value(QStringLiteral("fileUrl")).toString();
            m.sha512 = o.value(QStringLiteral("sha512")).toString();
            if (!m.projectId.isEmpty()) {
                m.manual = false;
            }
        }
    }
    return m;
}

QList<InstalledMod> ModManager::listMods(const QString &instanceId) const
{
    QList<InstalledMod> out;
    const QString dir = modsDir(m_dataDir, instanceId);
    QDir d(dir);
    if (!d.exists()) {
        return out;
    }
    const QStringList jars =
        d.entryList({ QStringLiteral("*.jar"), QStringLiteral("*.jar.disabled") }, QDir::Files, QDir::Name);
    for (const auto &f : jars) {
        out.append(readOne(m_dataDir, instanceId, dir, f));
    }
    return out;
}

bool ModManager::setEnabled(const QString &instanceId, const QString &fileName, bool enabled, QString *error)
{
    const QString dir = modsDir(m_dataDir, instanceId);
    const QString src = QDir(dir).filePath(fileName);
    if (!QFile::exists(src)) {
        if (error) {
            *error = tr("That mod file is gone already.");
        }
        return false;
    }
    const bool isDisabled = fileName.endsWith(QStringLiteral(".disabled"));
    if (enabled == !isDisabled) {
        return true; // already in the desired state
    }
    QString dst;
    if (enabled) {
        dst = src.left(src.size() - QStringLiteral(".disabled").size());
    } else {
        dst = src + QStringLiteral(".disabled");
    }
    if (QFile::exists(dst)) {
        if (error) {
            *error = tr("Can't toggle: “%1” already exists.").arg(QFileInfo(dst).fileName());
        }
        return false;
    }
    if (!QFile::rename(src, dst)) {
        if (error) {
            *error = tr("Couldn't rename the mod file (is the game running?).");
        }
        return false;
    }
    emit modsChanged(instanceId);
    Logger::info(QStringLiteral("Mod %1 in %2").arg(enabled ? QStringLiteral("enabled") : QStringLiteral("disabled")).arg(instanceId));
    return true;
}

bool ModManager::removeMod(const QString &instanceId, const QString &fileName, QString *error)
{
    const QString dir = modsDir(m_dataDir, instanceId);
    const QString path = QDir(dir).filePath(fileName);
    if (!QFile::exists(path)) {
        if (error) {
            *error = tr("That mod file is gone already.");
        }
        return false;
    }
    // Best-effort trash, then delete; never fail the remove over it.
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (!QFile::moveToTrash(path)) {
        QFile::remove(path);
    }
#else
    QFile::remove(path);
#endif
    // Drop the sidecar too (the jar is the source of truth).
    InstalledMod m = readOne(m_dataDir, instanceId, dir, fileName);
    QFile::remove(sidecarPath(m_dataDir, instanceId, m.baseName));
    emit modsChanged(instanceId);
    Logger::info(QStringLiteral("Mod removed from %1: %2").arg(instanceId, fileName));
    return true;
}

bool ModManager::addExternalJar(const QString &instanceId, const QString &jarPath, QString *error,
                                QString *addedName)
{
    QFileInfo src(jarPath);
    if (!src.exists() || !src.isFile() || src.suffix().compare(QStringLiteral("jar"), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = tr("That isn't a .jar file.");
        }
        return false;
    }
    const QString dir = modsDir(m_dataDir, instanceId);
    QDir().mkpath(dir);
    const QString dst = QDir(dir).filePath(src.fileName());
    if (QFile::exists(dst) || QFile::exists(dst + QStringLiteral(".disabled"))) {
        if (error) {
            *error = tr("“%1” is already in this profile's mods.").arg(src.fileName());
        }
        return false;
    }
    if (!QFile::copy(jarPath, dst)) {
        if (error) {
            *error = tr("Couldn't copy that jar into the profile.");
        }
        return false;
    }
    if (addedName) {
        *addedName = src.fileName();
    }
    emit modsChanged(instanceId);
    Logger::info(QStringLiteral("External jar added to %1: %2").arg(instanceId, src.fileName()));
    return true;
}

bool ModManager::installUnitBlocking(const QString &instanceId, const ModInstallPlan &unit, int maxParallel,
                                     Task::Context &ctx)
{
    const QString game = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game").arg(instanceId));
    const QString target = QDir(game).filePath(unit.targetDir.isEmpty() ? QStringLiteral("mods") : unit.targetDir);
    QDir().mkpath(target);
    DownloadRequest req;
    req.url = QUrl(unit.file.url);
    req.destPath = QDir(target).filePath(unit.file.filename);
    req.expectedSha512 = unit.file.sha512.toLatin1();
    req.expectedSha1 = unit.file.sha1.toLatin1();
    req.expectedSize = unit.file.size;
    QString err;
    if (!DownloadManager::downloadManyBlocking({ req }, maxParallel, ctx, &err)) {
        ctx.fail(err);
        return false;
    }
    // Sidecar only for mods (the updatable kind).
    if (unit.targetDir == QStringLiteral("mods") && !unit.project.id.isEmpty()) {
        QString base = unit.file.filename;
        if (base.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
            base.chop(4);
        }
        QDir().mkpath(sidecarDir(m_dataDir, instanceId));
        QJsonObject o;
        o[QStringLiteral("schemaVersion")] = 1;
        o[QStringLiteral("projectId")] = unit.project.id;
        o[QStringLiteral("slug")] = unit.project.slug;
        o[QStringLiteral("title")] = unit.project.title;
        o[QStringLiteral("versionId")] = unit.version.id;
        o[QStringLiteral("versionNumber")] = unit.version.versionNumber;
        o[QStringLiteral("fileUrl")] = unit.file.url;
        o[QStringLiteral("sha512")] = unit.file.sha512;
        o[QStringLiteral("installedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QFile sc(sidecarPath(m_dataDir, instanceId, base));
        if (sc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sc.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        }
    }
    emit modsChanged(instanceId);
    return true;
}

QList<ModUpdate> ModManager::checkUpdatesBlocking(const QString &instanceId, const QString &mcVersion,
                                                  const QString &loader, Task::Context &ctx, ModrinthApi *api)
{
    QList<ModUpdate> out;
    if (!api) {
        return out;
    }
    const QList<InstalledMod> mods = listMods(instanceId);
    int i = 0;
    for (const auto &m : mods) {
        if (ctx.isCancelled()) {
            break;
        }
        ctx.report(i++, qMax(1, (int)mods.size()), QObject::tr("Checking %1…").arg(m.title));
        if (m.manual || m.projectId.isEmpty() || m.versionId.isEmpty()) {
            continue;
        }
        const QList<ModrinthVersion> vers =
            api->versionsBlocking(m.projectId, loader, mcVersion, ctx);
        if (vers.isEmpty()) {
            continue; // per-mod failure already logged; keep checking the rest
        }
        QString ptype = QStringLiteral("mod");
        const int best = ModrinthMeta::pickBestVersion(vers, mcVersion, loader, ptype);
        if (best >= 0 && vers.at(best).id != m.versionId) {
            out.append({ m, vers.at(best) });
        }
    }
    ctx.report(1, 1, QObject::tr("Done"));
    return out;
}

#include "ModManager.moc"
