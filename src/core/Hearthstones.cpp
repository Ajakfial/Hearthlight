#include "Hearthstones.h"

#include "Instance.h"
#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
bool copyFile(const QString &src, const QString &dst, QString *error)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        if (error) {
            *error = QObject::tr("Couldn't copy %1.").arg(QFileInfo(src).fileName());
        }
        return false;
    }
    return true;
}

bool copyDir(const QString &src, const QString &dst, QString *error)
{
    QDir().mkpath(dst);
    QDir s(src);
    if (!s.exists()) {
        return true;
    }
    for (const auto &e : s.entryList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name)) {
        const QString sp = s.filePath(e);
        const QString dp = QDir(dst).filePath(e);
        if (QFileInfo(sp).isDir()) {
            if (!copyDir(sp, dp, error)) {
                return false;
            }
        } else if (!copyFile(sp, dp, error)) {
            return false;
        }
    }
    return true;
}

qint64 dirSize(const QString &path)
{
    qint64 total = 0;
    const QDir s(path);
    if (!s.exists()) {
        return 0;
    }
    for (const auto &e : s.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        const QFileInfo fi(s.filePath(e));
        total += fi.isDir() ? dirSize(s.filePath(e)) : fi.size();
    }
    return total;
}

qint64 dirMtimeMs(const QString &path)
{
    qint64 newest = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const QDir s(path);
    if (!s.exists()) {
        return newest;
    }
    for (const auto &e : s.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        const QString p = s.filePath(e);
        if (QFileInfo(p).isDir()) {
            newest = qMax(newest, dirMtimeMs(p));
        } else {
            newest = qMax(newest, QFileInfo(p).lastModified().toMSecsSinceEpoch());
        }
    }
    return newest;
}

QString sanitizeName(QString n)
{
    n = n.trimmed();
    QString out;
    for (const QChar c : n) {
        out += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char(' '))
            ? c
            : QLatin1Char('-');
    }
    out = out.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    while (out.contains(QStringLiteral("--"))) {
        out.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    return out.isEmpty() ? QStringLiteral("backup") : out.left(64);
}
} // namespace

Hearthstones::Hearthstones(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

QString Hearthstones::hearthstonesDir(const QString &dataDir, const QString &instanceId)
{
    return QDir(dataDir).filePath(QStringLiteral("instances/%1/hearthstones").arg(instanceId));
}

QList<WorldInfo> Hearthstones::listWorlds(const QString &instanceId) const
{
    QList<WorldInfo> out;
    const QDir saves(QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game/saves").arg(instanceId)));
    if (!saves.exists()) {
        return out;
    }
    for (const auto &sub : saves.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        // Skip stale session locks, keep real world folders.
        if (!QFile::exists(saves.filePath(sub + QStringLiteral("/level.dat")))
            && !QFile::exists(saves.filePath(sub + QStringLiteral("/level.dat_old")))) {
            continue;
        }
        WorldInfo w;
        w.folder = sub;
        w.lastModifiedMs = dirMtimeMs(saves.filePath(sub));
        w.sizeBytes = dirSize(saves.filePath(sub));
        out.append(w);
    }
    std::sort(out.begin(), out.end(), [](const WorldInfo &a, const WorldInfo &b) {
        return a.lastModifiedMs > b.lastModifiedMs;
    });
    return out;
}

QList<HearthstoneInfo> Hearthstones::list(const QString &instanceId) const
{
    QList<HearthstoneInfo> out;
    const QDir d(hearthstonesDir(m_dataDir, instanceId));
    if (!d.exists()) {
        return out;
    }
    for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        HearthstoneInfo h;
        h.name = sub;
        h.sizeBytes = dirSize(d.filePath(sub));
        QFile mf(QDir(d.filePath(sub)).filePath(QStringLiteral("manifest.json")));
        if (mf.open(QIODevice::ReadOnly)) {
            QJsonParseError e{};
            const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &e);
            if (e.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject o = doc.object();
                h.created = o.value(QStringLiteral("created")).toString();
                h.gameVersion = o.value(QStringLiteral("gameVersion")).toString();
                h.loader = o.value(QStringLiteral("loader")).toString();
                for (const auto &v : o.value(QStringLiteral("worlds")).toArray()) {
                    h.worlds.append(v.toString());
                }
                h.includesConfig = o.value(QStringLiteral("includesConfig")).toBool(false);
                h.automatic = o.value(QStringLiteral("automatic")).toBool(false);
            }
        }
        out.append(h);
    }
    std::sort(out.begin(), out.end(), [](const HearthstoneInfo &a, const HearthstoneInfo &b) {
        return a.name > b.name;
    });
    return out;
}

QString Hearthstones::create(const QString &instanceId, const QString &name, const QStringList &worlds,
                             bool includeConfig, bool automatic, QString *error)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    QString dirName = automatic ? QStringLiteral("auto-%1").arg(stamp)
                                : QStringLiteral("%1-%2").arg(stamp, sanitizeName(name));
    // Same-second repeats get a numeric suffix instead of clobbering.
    {
        QString cand = dirName;
        int n = 2;
        while (QDir(QDir(hearthstonesDir(m_dataDir, instanceId)).filePath(cand)).exists()) {
            cand = QStringLiteral("%1-%2").arg(dirName).arg(n++);
        }
        dirName = cand;
    }
    const QString dest = QDir(hearthstonesDir(m_dataDir, instanceId)).filePath(dirName);
    const QString game = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game").arg(instanceId));

    QStringList targets = worlds;
    if (targets.isEmpty()) {
        for (const auto &w : listWorlds(instanceId)) {
            targets.append(w.folder);
        }
    }
    if (targets.isEmpty() && !includeConfig) {
        if (error) {
            *error = tr("Nothing to back up: no worlds found and config not selected.");
        }
        return {};
    }
    for (const auto &w : targets) {
        const QString src = QDir(game).filePath(QStringLiteral("saves/%1").arg(w));
        if (!QDir(src).exists()) {
            continue;
        }
        if (!copyDir(src, QDir(dest).filePath(QStringLiteral("saves/%1").arg(w)), error)) {
            QDir(dest).removeRecursively();
            return {};
        }
    }
    if (includeConfig) {
        if (QDir(QDir(game).filePath(QStringLiteral("config"))).exists()
            && !copyDir(QDir(game).filePath(QStringLiteral("config")), QDir(dest).filePath(QStringLiteral("config")),
                        error)) {
            QDir(dest).removeRecursively();
            return {};
        }
        if (QFile::exists(QDir(game).filePath(QStringLiteral("options.txt")))
            && !copyFile(QDir(game).filePath(QStringLiteral("options.txt")),
                         QDir(dest).filePath(QStringLiteral("options.txt")), error)) {
            QDir(dest).removeRecursively();
            return {};
        }
    }
    QJsonObject meta;
    meta[QStringLiteral("schemaVersion")] = 1;
    meta[QStringLiteral("name")] = dirName;
    meta[QStringLiteral("created")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    {
        QFile ij(QDir(m_dataDir).filePath(QStringLiteral("instances/%1/instance.json").arg(instanceId)));
        if (ij.open(QIODevice::ReadOnly)) {
            QJsonParseError e{};
            const QJsonDocument doc = QJsonDocument::fromJson(ij.readAll(), &e);
            if (e.error == QJsonParseError::NoError && doc.isObject()) {
                meta[QStringLiteral("gameVersion")] = doc.object().value(QStringLiteral("versionId")).toString();
                meta[QStringLiteral("loader")] = doc.object().value(QStringLiteral("loaderType")).toString();
            }
        }
    }
    QJsonArray wa;
    for (const auto &w : targets) {
        wa.append(w);
    }
    meta[QStringLiteral("worlds")] = wa;
    meta[QStringLiteral("includesConfig")] = includeConfig;
    meta[QStringLiteral("automatic")] = automatic;
    QSaveFile mf(QDir(dest).filePath(QStringLiteral("manifest.json")));
    if (mf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mf.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
        mf.commit();
    }
    // Prune old automatic snapshots only; manual ones are never touched.
    if (automatic) {
        auto all = list(instanceId);
        QList<HearthstoneInfo> autos;
        for (const auto &h : all) {
            if (h.automatic) {
                autos.append(h);
            }
        }
        while (autos.size() > maxAutoSnapshots()) {
            const QString oldest = autos.takeLast().name;
            QDir(QDir(hearthstonesDir(m_dataDir, instanceId)).filePath(oldest)).removeRecursively();
        }
    }
    emit changed(instanceId);
    Logger::info(QStringLiteral("Hearthstone for %1: %2").arg(instanceId, dirName));
    return dirName;
}

bool Hearthstones::restore(const QString &instanceId, const QString &name, QString *error)
{
    const QString src = QDir(hearthstonesDir(m_dataDir, instanceId)).filePath(name);
    if (!QDir(src).exists()) {
        if (error) {
            *error = tr("That backup is gone.");
        }
        return false;
    }
    const QString game = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game").arg(instanceId));
    // Safety: snapshot current worlds+config first so a restore never destroys.
    QString serr;
    if (create(instanceId, QStringLiteral("pre-restore"), {}, true, true, &serr).isEmpty()) {
        if (error) {
            *error = tr("Couldn't preserve the current worlds first (%1).").arg(serr);
        }
        return false;
    }
    for (const auto &sub : { QStringLiteral("saves"), QStringLiteral("config") }) {
        const QString from = QDir(src).filePath(sub);
        if (!QDir(from).exists()) {
            continue;
        }
        const QString to = QDir(game).filePath(sub);
        QDir(to).removeRecursively();
        if (!copyDir(from, to, error)) {
            return false;
        }
    }
    const QString fromOpts = QDir(src).filePath(QStringLiteral("options.txt"));
    if (QFile::exists(fromOpts) && !copyFile(fromOpts, QDir(game).filePath(QStringLiteral("options.txt")), error)) {
        return false;
    }
    emit changed(instanceId);
    Logger::info(QStringLiteral("Hearthstone restored %1 from %2").arg(instanceId, name));
    return true;
}

bool Hearthstones::remove(const QString &instanceId, const QString &name, QString *error)
{
    const QString path = QDir(hearthstonesDir(m_dataDir, instanceId)).filePath(name);
    if (!QDir(path).exists()) {
        if (error) {
            *error = tr("That backup is gone already.");
        }
        return false;
    }
    QDir(path).removeRecursively();
    emit changed(instanceId);
    return true;
}

QString Hearthstones::autoSnapshotIfNeeded(const QString &instanceId)
{
    const auto worlds = listWorlds(instanceId);
    if (worlds.isEmpty()) {
        return {}; // nothing to protect yet
    }
    qint64 newestWorld = 0;
    for (const auto &w : worlds) {
        newestWorld = qMax(newestWorld, w.lastModifiedMs);
    }
    qint64 newestSnap = 0;
    const QDir d(hearthstonesDir(m_dataDir, instanceId));
    if (d.exists()) {
        for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            newestSnap = qMax(newestSnap, QFileInfo(d.filePath(sub)).lastModified().toMSecsSinceEpoch());
        }
    }
    if (newestSnap > 0 && newestWorld <= newestSnap) {
        return {}; // worlds unchanged since the last backup
    }
    QString err;
    const QString name = create(instanceId, {}, {}, true, true, &err);
    if (name.isEmpty()) {
        Logger::warning(QStringLiteral("Auto world backup failed for %1: %2").arg(instanceId, err));
    }
    return name;
}

#include "Hearthstones.moc"
