#include "InstanceManager.h"

#include "CurseForgeApi.h"
#include "DownloadManager.h"
#include "Logger.h"
#include "ModManager.h"
#include "Task.h"
#include "ZipUtil.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include <functional>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

InstanceManager::InstanceManager(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    QDir().mkpath(instancesDir());
}

QString InstanceManager::instancesDir() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("instances"));
}

bool InstanceManager::load()
{
    m_instances.clear();
    QDir d(instancesDir());
    for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile f(QDir(d.filePath(sub)).filePath(QStringLiteral("instance.json")));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
        if (e.error != QJsonParseError::NoError || !doc.isObject()) {
            Logger::warning(QStringLiteral("Skipping corrupt instance.json in %1").arg(sub));
            continue;
        }
        bool ok = false;
        Instance in = Instance::fromJson(doc.object(), &ok);
        if (!ok) {
            Logger::warning(QStringLiteral("Skipping invalid instance in %1").arg(sub));
            continue;
        }
        m_instances.append(in);
    }
    // Order file (drag-reorder persistence).
    QFile of(QDir(instancesDir()).filePath(QStringLiteral("order.json")));
    if (of.open(QIODevice::ReadOnly)) {
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(of.readAll(), &e);
        if (e.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray arr = doc.object().value(QStringLiteral("order")).toArray();
            QMap<QString, int> pos;
            int i = 0;
            for (const auto &v : arr) {
                pos.insert(v.toString(), i++);
            }
            std::sort(m_instances.begin(), m_instances.end(), [&](const Instance &a, const Instance &b) {
                const int pa = pos.value(a.id, 1 << 30);
                const int pb = pos.value(b.id, 1 << 30);
                if (pa != pb) {
                    return pa < pb;
                }
                return a.order < b.order;
            });
        }
    }
    for (int i = 0; i < m_instances.size(); ++i) {
        m_instances[i].order = i;
    }
    emit loaded();
    Logger::info(QStringLiteral("Loaded %1 instance(s)").arg(m_instances.size()));
    return true;
}

bool InstanceManager::writeOne(const Instance &in) const
{
    const QString root = instanceRootDir(m_dataDir, in.id);
    QDir().mkpath(root);
    QSaveFile f(QDir(root).filePath(QStringLiteral("instance.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    f.write(QJsonDocument(in.toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool InstanceManager::saveAll()
{
    for (const auto &in : m_instances) {
        if (!writeOne(in)) {
            return false;
        }
    }
    QJsonArray arr;
    for (const auto &in : m_instances) {
        arr.append(in.id);
    }
    QJsonObject root;
    root[QStringLiteral("order")] = arr;
    QSaveFile f(QDir(instancesDir()).filePath(QStringLiteral("order.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        return false;
    }
    emit changed();
    return true;
}

QList<Instance> InstanceManager::instances() const
{
    return m_instances;
}

Instance InstanceManager::get(const QString &id) const
{
    for (const auto &in : m_instances) {
        if (in.id.compare(id, Qt::CaseInsensitive) == 0) {
            return in;
        }
    }
    return {};
}

bool InstanceManager::has(const QString &id) const
{
    return get(id).isValid();
}

QStringList InstanceManager::groups() const
{
    QStringList out;
    for (const auto &in : m_instances) {
        if (!in.group.trimmed().isEmpty() && !out.contains(in.group)) {
            out.append(in.group);
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QString InstanceManager::uniqueIdFor(const QString &base) const
{
    QString cand = Instance::sanitizeId(base);
    if (!has(cand)) {
        return cand;
    }
    for (int i = 2; i < 1000; ++i) {
        const QString t = QStringLiteral("%1-%2").arg(cand).arg(i);
        if (!has(t)) {
            return t;
        }
    }
    return cand + QStringLiteral("-") + QString::number(QDateTime::currentSecsSinceEpoch());
}

void InstanceManager::ensureGameDirs(const QString &id) const
{
    const Instance in = get(id);
    if (!in.isValid()) {
        return;
    }
    const QString game = instanceGameDir(m_dataDir, in);
    for (const auto &sub :
         { QStringLiteral(""), QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("saves"),
           QStringLiteral("resourcepacks"), QStringLiteral("shaderpacks"), QStringLiteral("datapacks"),
           QStringLiteral("screenshots") }) {
        QDir().mkpath(sub.isEmpty() ? game : QDir(game).filePath(sub));
    }
}

bool InstanceManager::create(Instance in, QString *error)
{
    if (in.name.trimmed().isEmpty()) {
        if (error) {
            *error = tr("Give the profile a name first.");
        }
        return false;
    }
    if (in.versionId.trimmed().isEmpty()) {
        if (error) {
            *error = tr("Pick a Minecraft version first.");
        }
        return false;
    }
    in.id = uniqueIdFor(in.name);
    in.order = m_instances.size();
    if (in.loaderType.isEmpty()) {
        in.loaderType = QStringLiteral("vanilla");
    }
    m_instances.append(in);
    ensureGameDirs(in.id);
    if (!writeOne(in)) {
        m_instances.removeLast();
        if (error) {
            *error = tr("Couldn't write the new profile to disk.");
        }
        return false;
    }
    saveAll();
    emit instanceAdded(in.id);
    emit changed();
    Logger::info(QStringLiteral("Instance created: %1 (%2)").arg(in.name).arg(in.id));
    return true;
}

bool InstanceManager::update(const Instance &in, QString *error)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].id.compare(in.id, Qt::CaseInsensitive) == 0) {
            if (!in.isValid()) {
                if (error) {
                    *error = tr("That profile is incomplete.");
                }
                return false;
            }
            m_instances[i] = in;
            if (!writeOne(in)) {
                if (error) {
                    *error = tr("Couldn't save the profile.");
                }
                return false;
            }
            emit changed();
            return true;
        }
    }
    if (error) {
        *error = tr("Profile not found.");
    }
    return false;
}

bool InstanceManager::rename(const QString &id, const QString &newName, QString *error)
{
    Instance in = get(id);
    if (!in.isValid()) {
        if (error) {
            *error = tr("Profile not found.");
        }
        return false;
    }
    if (newName.trimmed().isEmpty()) {
        if (error) {
            *error = tr("Give the profile a name first.");
        }
        return false;
    }
    in.name = newName.trimmed();
    return update(in, error);
}

static bool copyDirRecursive(const QString &src, const QString &dst, QString *error)
{
    QDir().mkpath(dst);
    QDir s(src);
    if (!s.exists()) {
        return true; // nothing to copy
    }
    for (const auto &e : s.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        const QString sp = s.filePath(e);
        const QString dp = QDir(dst).filePath(e);
        QFileInfo fi(sp);
        if (fi.isDir()) {
            if (!copyDirRecursive(sp, dp, error)) {
                return false;
            }
        } else {
            QDir().mkpath(QFileInfo(dp).absolutePath());
            QFile::remove(dp);
            if (!QFile::copy(sp, dp)) {
                if (error) {
                    *error = QObject::tr("Couldn't copy %1.").arg(e);
                }
                return false;
            }
        }
    }
    return true;
}

bool InstanceManager::clone(const QString &id, const QString &newName, QString *newIdOut, QString *error)
{
    const Instance src = get(id);
    if (!src.isValid()) {
        if (error) {
            *error = tr("Profile not found.");
        }
        return false;
    }
    Instance c = src;
    c.name = newName.trimmed().isEmpty() ? (src.name + tr(" copy")) : newName.trimmed();
    c.id.clear();
    c.playtimeSecs = 0;
    c.lastPlayed.clear();
    c.lastAccount.clear();
    if (!create(c, error)) {
        return false;
    }
    const QString newId = m_instances.last().id;
    // Copy the game content (mods/config/saves/…).
    const QString srcGame = instanceGameDir(m_dataDir, src);
    const QString dstGame = instanceGameDir(m_dataDir, m_instances.last());
    QString cerr;
    if (!copyDirRecursive(srcGame, dstGame, &cerr)) {
        if (error) {
            *error = cerr;
        }
        return false;
    }
    if (newIdOut) {
        *newIdOut = newId;
    }
    return true;
}

bool InstanceManager::remove(const QString &id, bool toTrash)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].id.compare(id, Qt::CaseInsensitive) == 0) {
            const QString root = instanceRootDir(m_dataDir, m_instances[i].id);
            m_instances.removeAt(i);
            if (QDir(root).exists()) {
                if (toTrash) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                    QFile::moveToTrash(root);
#else
                    QDir(root).removeRecursively();
#endif
                } else {
                    QDir(root).removeRecursively();
                }
            }
            saveAll();
            emit instanceRemoved(id);
            emit changed();
            Logger::info(QStringLiteral("Instance removed: %1").arg(id));
            return true;
        }
    }
    return false;
}

bool InstanceManager::move(const QString &id, int newIndex)
{
    int from = -1;
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].id.compare(id, Qt::CaseInsensitive) == 0) {
            from = i;
            break;
        }
    }
    if (from < 0) {
        return false;
    }
    newIndex = qBound(0, newIndex, m_instances.size() - 1);
    if (newIndex == from) {
        return true;
    }
    m_instances.move(from, newIndex);
    for (int i = 0; i < m_instances.size(); ++i) {
        m_instances[i].order = i;
    }
    saveAll();
    return true;
}

bool InstanceManager::setGroup(const QString &id, const QString &group)
{
    Instance in = get(id);
    if (!in.isValid()) {
        return false;
    }
    in.group = group.trimmed();
    QString err;
    return update(in, &err);
}

void InstanceManager::recordPlay(const QString &id, qint64 seconds, const QString &accountName)
{
    if (seconds <= 0) {
        return;
    }
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].id.compare(id, Qt::CaseInsensitive) == 0) {
            m_instances[i].playtimeSecs += seconds;
            m_instances[i].lastPlayed = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            if (!accountName.isEmpty()) {
                m_instances[i].lastAccount = accountName;
            }
            writeOne(m_instances[i]);
            emit changed();
            return;
        }
    }
}

QString InstanceManager::backupForSwitch(const QString &id, QString *error)
{
    const Instance in = get(id);
    if (!in.isValid()) {
        if (error) {
            *error = tr("Profile not found.");
        }
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString backup = QDir(instanceRootDir(m_dataDir, id)).filePath(QStringLiteral(".backup-%1").arg(stamp));
    QDir().mkpath(backup);
    const QString game = instanceGameDir(m_dataDir, in);
    for (const auto &sub :
         { QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("saves"), QStringLiteral("resourcepacks") }) {
        const QString src = QDir(game).filePath(sub);
        if (QDir(src).exists()) {
            QString cerr;
            if (!copyDirRecursive(src, QDir(backup).filePath(sub), &cerr)) {
                if (error) {
                    *error = cerr;
                }
                return {};
            }
        }
    }
    Logger::info(QStringLiteral("Instance backup for %1: %2").arg(id).arg(backup));
    return backup;
}

bool InstanceManager::exportHearthpack(const QString &id, const QString &zipPath, QString *error)
{
    const Instance in = get(id);
    if (!in.isValid()) {
        if (error) {
            *error = tr("Profile not found.");
        }
        return false;
    }
    // .hearthpack = zip with instance.json + overrides/ (game content minus
    // large caches). Written with miniz so no extra dependency.
    QList<QPair<QString, QByteArray>> entries;
    entries.append({ QStringLiteral("instance.json"), QJsonDocument(in.toJson()).toJson(QJsonDocument::Indented) });
    const QString game = instanceGameDir(m_dataDir, in);
    const QStringList roots = { QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("resourcepacks"),
                               QStringLiteral("shaderpacks"), QStringLiteral("saves") };
    for (const auto &root : roots) {
        QDir d(QDir(game).filePath(root));
        if (!d.exists()) {
            continue;
        }
        QDirIterator it(d.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = d.relativeFilePath(it.filePath());
            // Skip absurd sizes (>200MB single file) rather than OOM.
            if (QFileInfo(it.filePath()).size() > 200 * 1024 * 1024) {
                continue;
            }
            QFile f(it.filePath());
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            entries.append({ QStringLiteral("overrides/%1/%2").arg(root, rel), f.readAll() });
        }
    }
    return ZipUtil::createZipFromEntries(zipPath, entries, error);
}

bool InstanceManager::importHearthpack(const QString &zipPath, QString *newIdOut, QString *error)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error) {
            *error = tr("Couldn't create a temporary folder.");
        }
        return false;
    }
    QString zerr;
    if (!ZipUtil::extractZipFile(zipPath, tmp.path(), {}, &zerr)) {
        if (error) {
            *error = zerr;
        }
        return false;
    }
    QFile ij(QDir(tmp.path()).filePath(QStringLiteral("instance.json")));
    if (!ij.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = tr("That file isn't a Hearthlight pack (no instance.json).");
        }
        return false;
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(ij.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = tr("That pack's instance.json is corrupt.");
        }
        return false;
    }
    bool ok = false;
    Instance in = Instance::fromJson(doc.object(), &ok);
    if (!ok) {
        if (error) {
            *error = tr("That pack's instance.json is invalid.");
        }
        return false;
    }
    if (!create(in, error)) {
        // create() assigns a fresh unique id even on name clashes.
        Instance retry = in;
        retry.name = in.name + tr(" (imported)");
        if (!create(retry, error)) {
            return false;
        }
    }
    const QString newId = m_instances.last().id;
    const QString dstGame = instanceGameDir(m_dataDir, m_instances.last());
    const QString srcOver = QDir(tmp.path()).filePath(QStringLiteral("overrides"));
    if (QDir(srcOver).exists()) {
        QString cerr;
        if (!copyDirRecursive(srcOver, dstGame, &cerr)) {
            if (error) {
                *error = cerr;
            }
            return false;
        }
    }
    if (newIdOut) {
        *newIdOut = newId;
    }
    return true;
}

bool InstanceManager::importMrpackBlocking(const QString &zipPath, const QString &newName, Task::Context &ctx,
                                           QString *newIdOut)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        ctx.fail(tr("Couldn't create a temporary folder."));
        return false;
    }
    QString zerr;
    if (!ZipUtil::extractZipFile(zipPath, tmp.path(), {}, &zerr)) {
        ctx.fail(zerr);
        return false;
    }
    QFile mf(QDir(tmp.path()).filePath(QStringLiteral("modrinth.index.json")));
    if (!mf.open(QIODevice::ReadOnly)) {
        ctx.fail(tr("That file isn't a Modrinth pack (no modrinth.index.json)."));
        return false;
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        ctx.fail(tr("That pack's Modrinth index is corrupt."));
        return false;
    }
    const QJsonObject root = doc.object();
    const QString mcVersion = root.value(QStringLiteral("game")).toString();
    const QString loaderHint = root.value(QStringLiteral("loaders")).toArray().isEmpty()
        ? QString()
        : root.value(QStringLiteral("loaders")).toArray().first().toString();
    Instance in;
    in.name = newName.trimmed().isEmpty() ? root.value(QStringLiteral("name")).toString(QStringLiteral("Modpack")) : newName.trimmed();
    in.versionId = mcVersion.isEmpty() ? QStringLiteral("1.20.4") : mcVersion;
    in.loaderType = loaderHint.isEmpty() ? QStringLiteral("fabric") : loaderHint.toLower();
    QString cerr;
    if (!create(in, &cerr)) {
        ctx.fail(cerr);
        return false;
    }
    const QString newId = m_instances.last().id;
    const QString game = instanceGameDir(m_dataDir, m_instances.last());
    // Overrides from the pack.
    const QString srcOver = QDir(tmp.path()).filePath(QStringLiteral("overrides"));
    if (QDir(srcOver).exists() && !copyDirRecursive(srcOver, game, &cerr)) {
        ctx.fail(cerr);
        return false;
    }
    // Files: download each by URL into game/<env path>.
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QList<DownloadRequest> reqs;
    for (const auto &v : files) {
        const QJsonObject fo = v.toObject();
        const QString path = fo.value(QStringLiteral("path")).toString();
        const QJsonArray dls = fo.value(QStringLiteral("downloads")).toArray();
        if (path.isEmpty() || dls.isEmpty()) {
            continue;
        }
        DownloadRequest req;
        req.url = QUrl(dls.first().toString());
        req.destPath = QDir(game).filePath(path);
        const QJsonObject hashes = fo.value(QStringLiteral("hashes")).toObject();
        req.expectedSha512 = hashes.value(QStringLiteral("sha512")).toString().toLatin1();
        req.expectedSha1 = hashes.value(QStringLiteral("sha1")).toString().toLatin1();
        req.expectedSize = (qint64)fo.value(QStringLiteral("fileSize")).toDouble(-1);
        reqs.append(req);
    }
    if (!reqs.isEmpty()) {
        QString derr;
        if (!DownloadManager::downloadManyBlocking(reqs, 8, ctx, &derr)) {
            ctx.fail(derr);
            return false;
        }
    }
    if (newIdOut) {
        *newIdOut = newId;
    }
    ctx.report(1, 1, tr("Done"));
    return true;
}

bool InstanceManager::exportMrpack(const QString &id, const QString &zipPath, QString *error)
{
    const Instance in = get(id);
    if (!in.isValid()) {
        if (error) {
            *error = tr("Profile not found.");
        }
        return false;
    }
    // Files that can be re-downloaded (Modrinth sidecars) become index
    // entries; everything else rides along in overrides/.
    ModManager mods(m_dataDir);
    QSet<QString> downloadableMods;
    QJsonArray files;
    for (const auto &m : mods.listMods(id)) {
        if (m.manual || m.fileUrl.isEmpty()) {
            continue;
        }
        downloadableMods.insert(m.fileName);
        // A disabled twin downloads enabled (the toggle is local state).
        QString live = m.fileName;
        if (live.endsWith(QStringLiteral(".disabled"))) {
            live.chop(QStringLiteral(".disabled").size());
        }
        QJsonObject fo;
        fo[QStringLiteral("path")] = QStringLiteral("mods/%1").arg(live);
        QJsonObject hashes;
        if (!m.sha512.isEmpty()) {
            hashes[QStringLiteral("sha512")] = m.sha512;
        }
        fo[QStringLiteral("hashes")] = hashes;
        QJsonObject env;
        env[QStringLiteral("client")] = QStringLiteral("required");
        env[QStringLiteral("server")] = QStringLiteral("required");
        fo[QStringLiteral("env")] = env;
        fo[QStringLiteral("downloads")] = QJsonArray{ m.fileUrl };
        if (m.size > 0) {
            fo[QStringLiteral("fileSize")] = (double)m.size;
        }
        files.append(fo);
    }
    QJsonObject deps;
    deps[QStringLiteral("minecraft")] = in.versionId;
    const QString lt = in.loaderType.toLower();
    if (lt == QStringLiteral("fabric")) {
        deps[QStringLiteral("fabric-loader")] =
            in.loaderVersion.isEmpty() ? QStringLiteral("*") : in.loaderVersion;
    } else if (lt == QStringLiteral("quilt")) {
        deps[QStringLiteral("quilt-loader")] =
            in.loaderVersion.isEmpty() ? QStringLiteral("*") : in.loaderVersion;
    } else if (lt == QStringLiteral("forge")) {
        deps[QStringLiteral("forge")] = in.loaderVersion.isEmpty() ? QStringLiteral("*") : in.loaderVersion;
    } else if (lt == QStringLiteral("neoforge")) {
        deps[QStringLiteral("neoforge")] = in.loaderVersion.isEmpty() ? QStringLiteral("*") : in.loaderVersion;
    }
    QJsonObject index;
    index[QStringLiteral("formatVersion")] = 1;
    index[QStringLiteral("game")] = QStringLiteral("minecraft");
    index[QStringLiteral("versionId")] = QStringLiteral("1.0.0");
    index[QStringLiteral("name")] = in.name;
    index[QStringLiteral("summary")] = tr("Exported from Hearthlight.");
    index[QStringLiteral("dependencies")] = deps;
    index[QStringLiteral("files")] = files;

    QList<QPair<QString, QByteArray>> entries;
    entries.append({ QStringLiteral("modrinth.index.json"),
                     QJsonDocument(index).toJson(QJsonDocument::Indented) });
    const QString game = instanceGameDir(m_dataDir, in);
    const QStringList roots = { QStringLiteral("config"), QStringLiteral("resourcepacks"),
                                QStringLiteral("shaderpacks"), QStringLiteral("datapacks") };
    for (const auto &root : roots) {
        QDir d(QDir(game).filePath(root));
        if (!d.exists()) {
            continue;
        }
        QDirIterator it(d.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (QFileInfo(it.filePath()).size() > 200 * 1024 * 1024) {
                continue;
            }
            QFile f(it.filePath());
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            entries.append({ QStringLiteral("overrides/%1/%2").arg(root, d.relativeFilePath(it.filePath())),
                             f.readAll() });
        }
    }
    // Manual (hand-added) mods travel in overrides so the pack still works.
    QDir md(QDir(game).filePath(QStringLiteral("mods")));
    if (md.exists()) {
        QDirIterator it(md.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = md.relativeFilePath(it.filePath());
            if (rel.startsWith(QStringLiteral(".hearth/"))) {
                continue; // sidecars are launcher state, not pack content
            }
            const QString name = QFileInfo(it.filePath()).fileName();
            if (downloadableMods.contains(name) || downloadableMods.contains(rel)) {
                continue; // re-downloaded from files[] instead
            }
            if (QFileInfo(it.filePath()).size() > 200 * 1024 * 1024) {
                continue;
            }
            QFile f(it.filePath());
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            entries.append({ QStringLiteral("overrides/mods/%1").arg(rel), f.readAll() });
        }
    }
    return ZipUtil::createZipFromEntries(zipPath, entries, error);
}

bool InstanceManager::importCurseforgeZip(const QString &zipPath, const QString &newName, QString *newIdOut,
                                          QString *error, QStringList *skippedRemote)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error) {
            *error = tr("Couldn't create a temporary folder.");
        }
        return false;
    }
    QString zerr;
    if (!ZipUtil::extractZipFile(zipPath, tmp.path(), {}, &zerr)) {
        if (error) {
            *error = zerr;
        }
        return false;
    }
    QFile mf(QDir(tmp.path()).filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = tr("That file isn't a CurseForge-style pack (no manifest.json).");
        }
        return false;
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = tr("That pack's manifest.json is corrupt.");
        }
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject mc = root.value(QStringLiteral("minecraft")).toObject();
    Instance in;
    in.name = newName.trimmed().isEmpty() ? root.value(QStringLiteral("name")).toString(QStringLiteral("CurseForge pack")) : newName.trimmed();
    in.versionId = mc.value(QStringLiteral("version")).toString(QStringLiteral("1.20.1"));
    const QJsonArray loaders = mc.value(QStringLiteral("modLoaders")).toArray();
    in.loaderType = QStringLiteral("forge");
    if (!loaders.isEmpty()) {
        const QString lid = loaders.first().toObject().value(QStringLiteral("id")).toString().toLower();
        if (lid.startsWith(QStringLiteral("fabric"))) {
            in.loaderType = QStringLiteral("fabric");
        } else if (lid.startsWith(QStringLiteral("quilt"))) {
            in.loaderType = QStringLiteral("quilt");
        } else if (lid.startsWith(QStringLiteral("neoforge"))) {
            in.loaderType = QStringLiteral("neoforge");
        }
    }
    QString cerr;
    if (!create(in, &cerr)) {
        if (error) {
            *error = cerr;
        }
        return false;
    }
    const QString game = instanceGameDir(m_dataDir, m_instances.last());
    // Local overrides directory (case-insensitive search).
    QString overDir;
    for (const auto &cand : { QStringLiteral("overrides"), QStringLiteral("Overrides"), QStringLiteral("overridefiles") }) {
        if (QDir(QDir(tmp.path()).filePath(cand)).exists()) {
            overDir = QDir(tmp.path()).filePath(cand);
            break;
        }
    }
    if (!overDir.isEmpty() && !copyDirRecursive(overDir, game, &cerr)) {
        if (error) {
            *error = cerr;
        }
        return false;
    }
    // Remote file list: best-effort notice (CF CDN needs an API key).
    QStringList skipped;
    for (const auto &v : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject fo = v.toObject();
        skipped.append(QStringLiteral("project %1 file %2 (needs CurseForge API key; add the jar manually)")
                           .arg(fo.value(QStringLiteral("projectID")).toInt())
                           .arg(fo.value(QStringLiteral("fileID")).toInt()));
    }
    if (skippedRemote) {
        *skippedRemote = skipped;
    }
    if (newIdOut) {
        *newIdOut = m_instances.last().id;
    }
    return true;
}

bool InstanceManager::importCurseforgeZipBlocking(const QString &zipPath, const QString &newName,
                                                    const QString &apiKey, Task::Context &ctx, QString *newIdOut,
                                                    QStringList *skippedOut)
{
    if (apiKey.trimmed().isEmpty()) {
        // No key: honest best-effort fallback (local files only).
        QString err;
        QStringList skipped;
        QString newId;
        if (!importCurseforgeZip(zipPath, newName, &newId, &err, &skipped)) {
            ctx.fail(err);
            return false;
        }
        if (skippedOut) {
            *skippedOut = skipped;
        }
        if (newIdOut) {
            *newIdOut = newId;
        }
        ctx.report(1, 1, tr("Done"));
        return true;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        ctx.fail(tr("Couldn't create a temporary folder."));
        return false;
    }
    QString zerr;
    if (!ZipUtil::extractZipFile(zipPath, tmp.path(), {}, &zerr)) {
        ctx.fail(zerr);
        return false;
    }
    QFile mf(QDir(tmp.path()).filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::ReadOnly)) {
        ctx.fail(tr("That file isn't a CurseForge-style pack (no manifest.json)."));
        return false;
    }
    const QByteArray manifest = mf.readAll();
    Instance in;
    const QString fallbackName = CurseForgeMeta::manifestName(manifest);
    in.name = newName.trimmed().isEmpty() ? fallbackName : newName.trimmed();
    in.versionId = CurseForgeMeta::manifestMinecraftVersion(manifest);
    in.loaderType = CurseForgeMeta::manifestLoaderType(manifest);
    QString cerr;
    if (!create(in, &cerr)) {
        ctx.fail(cerr);
        return false;
    }
    const QString newId = m_instances.last().id;
    const QString game = instanceGameDir(m_dataDir, m_instances.last());
    QString overDir;
    for (const auto &cand : { QStringLiteral("overrides"), QStringLiteral("Overrides"), QStringLiteral("overridefiles") }) {
        if (QDir(QDir(tmp.path()).filePath(cand)).exists()) {
            overDir = QDir(tmp.path()).filePath(cand);
            break;
        }
    }
    // Local helper (mirrors copyDirRecursive above without exposing it).
    std::function<bool(const QString &, const QString &, QString *)> copyTree =
        [&](const QString &src, const QString &dst, QString *error) -> bool {
        QDir().mkpath(dst);
        QDir s(src);
        if (!s.exists()) {
            return true;
        }
        for (const auto &e : s.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
            const QString sp = s.filePath(e);
            const QString dp = QDir(dst).filePath(e);
            QFileInfo fi(sp);
            if (fi.isDir()) {
                if (!copyTree(sp, dp, error)) {
                    return false;
                }
            } else {
                QDir().mkpath(QFileInfo(dp).absolutePath());
                QFile::remove(dp);
                if (!QFile::copy(sp, dp)) {
                    if (error) {
                        *error = QObject::tr("Couldn't copy %1.").arg(e);
                    }
                    return false;
                }
            }
        }
        return true;
    };
    if (!overDir.isEmpty() && !copyTree(overDir, game, &cerr)) {
        ctx.fail(cerr);
        return false;
    }
    // Resolve + download every manifest file via the CurseForge API.
    // Per-file failures are collected as skipped entries (one bad file never
    // kills a 200-file pack); only fatal problems fail the whole import.
    const QList<QPair<int, qint64>> wanted = CurseForgeMeta::manifestFiles(manifest);
    CurseForgeApi cf(m_dataDir, nullptr);
    struct OneFileContext : public Task::Context {
        Task::Context &outer;
        QString fileError;
        explicit OneFileContext(Task::Context &o)
            : outer(o)
        {
        }
        void report(qint64 r, qint64 t, const QString &m = {}) override { outer.report(r, t, m); }
        bool isCancelled() const override { return outer.isCancelled(); }
        void fail(const QString &m) override { fileError = m; }
    };
    QList<DownloadRequest> reqs;
    QStringList skipped;
    int done = 0;
    for (const auto &pair : wanted) {
        if (ctx.isCancelled()) {
            ctx.fail(tr("Cancelled."));
            return false;
        }
        ++done;
        ctx.report(done, qMax(1, (int)wanted.size()),
                   tr("Resolving CurseForge files… (%1/%2)").arg(done).arg(wanted.size()));
        OneFileContext sub(ctx);
        CurseForgeFile cfFile = cf.fileBlocking(pair.first, pair.second, apiKey, sub);
        if (cfFile.downloadUrl.isEmpty() || cfFile.fileName.isEmpty()) {
            const QString why = !sub.fileError.isEmpty() ? sub.fileError
                                                         : tr("no download available");
            skipped.append(tr("project %1 file %2 (%3)").arg(pair.first).arg(pair.second, why));
            continue;
        }
        DownloadRequest req;
        req.url = QUrl(cfFile.downloadUrl);
        req.destPath = QDir(game).filePath(QStringLiteral("mods/%1").arg(cfFile.fileName));
        req.expectedSize = cfFile.size;
        reqs.append(req);
    }
    if (!reqs.isEmpty()) {
        QString derr;
        if (!DownloadManager::downloadManyBlocking(reqs, 8, ctx, &derr)) {
            ctx.fail(derr);
            return false;
        }
    }
    if (skippedOut) {
        *skippedOut = skipped;
    }
    if (newIdOut) {
        *newIdOut = newId;
    }
    ctx.report(1, 1, tr("Done"));
    return true;
}

#include "InstanceManager.moc"
