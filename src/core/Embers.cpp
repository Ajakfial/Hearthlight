#include "Embers.h"

#include "Instance.h"
#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QString sanitizeReason(QString r)
{
    r = r.trimmed().toLower();
    QString out;
    for (const QChar c : r) {
        out += (c.isLetterOrNumber() || c == QLatin1Char('-')) ? c : QLatin1Char('-');
    }
    while (out.contains(QStringLiteral("--"))) {
        out.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    while (out.startsWith(QLatin1Char('-'))) {
        out.remove(0, 1);
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    return out.isEmpty() ? QStringLiteral("change") : out.left(32);
}
} // namespace

Embers::Embers(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

QString Embers::embersDir(const QString &dataDir, const QString &instanceId)
{
    return QDir(dataDir).filePath(QStringLiteral("instances/%1/.embers").arg(instanceId));
}

QList<EmberSnapshot> Embers::snapshots(const QString &instanceId) const
{
    QList<EmberSnapshot> out;
    const QDir d(embersDir(m_dataDir, instanceId));
    if (!d.exists()) {
        return out;
    }
    for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (sub.startsWith(QStringLiteral("pre-undo-")) || sub.contains(QLatin1Char('-'))) {
            EmberSnapshot s;
            s.name = sub;
            s.sizeBytes = dirSize(d.filePath(sub));
            QFile mf(QDir(d.filePath(sub)).filePath(QStringLiteral("ember.json")));
            if (mf.open(QIODevice::ReadOnly)) {
                QJsonParseError e{};
                const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &e);
                if (e.error == QJsonParseError::NoError && doc.isObject()) {
                    s.reason = doc.object().value(QStringLiteral("reason")).toString();
                    s.created = doc.object().value(QStringLiteral("created")).toString();
                }
            }
            out.append(s);
        }
    }
    // Newest first (names start with yyyyMMdd-hhmmss).
    std::sort(out.begin(), out.end(), [](const EmberSnapshot &a, const EmberSnapshot &b) {
        return a.name > b.name;
    });
    return out;
}

void Embers::touchLast(const QString &instanceId, const QString &snapshotName) const
{
    QDir().mkpath(QDir(m_dataDir).filePath(QStringLiteral("meta")));
    QJsonObject o;
    o[QStringLiteral("instanceId")] = instanceId;
    o[QStringLiteral("snapshot")] = snapshotName;
    o[QStringLiteral("at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSaveFile f(QDir(m_dataDir).filePath(QStringLiteral("meta/embers-last.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString Embers::snapshot(const QString &instanceId, const QString &reason, bool includeSaves, QString *error)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    QString name = QStringLiteral("%1-%2").arg(stamp, sanitizeReason(reason));
    {
        QString cand = name;
        int n = 2;
        while (QDir(QDir(embersDir(m_dataDir, instanceId)).filePath(cand)).exists()) {
            cand = QStringLiteral("%1-%2").arg(name).arg(n++);
        }
        name = cand;
    }
    const QString dest = QDir(embersDir(m_dataDir, instanceId)).filePath(name);
    const QString game = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game").arg(instanceId));
    QStringList subs = { QStringLiteral("mods"), QStringLiteral("config") };
    if (includeSaves) {
        subs.append(QStringLiteral("saves"));
        subs.append(QStringLiteral("resourcepacks"));
    }
    for (const auto &sub : subs) {
        const QString src = QDir(game).filePath(sub);
        if (QDir(src).exists() && !copyDir(src, QDir(dest).filePath(sub), error)) {
            return {};
        }
    }
    const QString instJson = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/instance.json").arg(instanceId));
    if (QFile::exists(instJson) && !copyFile(instJson, QDir(dest).filePath(QStringLiteral("instance.json")), error)) {
        return {};
    }
    QJsonObject meta;
    meta[QStringLiteral("reason")] = reason;
    meta[QStringLiteral("created")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    meta[QStringLiteral("includeSaves")] = includeSaves;
    QSaveFile mf(QDir(dest).filePath(QStringLiteral("ember.json")));
    if (mf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mf.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
        mf.commit();
    }
    // Prune oldest beyond the cap.
    auto all = snapshots(instanceId);
    while (all.size() > maxSnapshots()) {
        const QString oldest = all.takeLast().name;
        QDir(QDir(embersDir(m_dataDir, instanceId)).filePath(oldest)).removeRecursively();
        Logger::info(QStringLiteral("Embers pruned %1/%2").arg(instanceId, oldest));
    }
    touchLast(instanceId, name);
    emit changed(instanceId);
    Logger::info(QStringLiteral("Embers snapshot for %1: %2").arg(instanceId, name));
    return dest;
}

bool Embers::restore(const QString &instanceId, const QString &snapshotName, QString *error)
{
    const QString src = QDir(embersDir(m_dataDir, instanceId)).filePath(snapshotName);
    if (!QDir(src).exists()) {
        if (error) {
            *error = tr("That snapshot is gone.");
        }
        return false;
    }
    // Preserve current state first — nothing is ever lost.
    QString preErr;
    if (snapshot(instanceId, QStringLiteral("pre-undo"), true, &preErr).isEmpty()) {
        if (error) {
            *error = tr("Couldn't preserve the current state first (%1).").arg(preErr);
        }
        return false;
    }
    const QString game = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/game").arg(instanceId));
    for (const auto &sub :
         { QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("saves"), QStringLiteral("resourcepacks") }) {
        const QString from = QDir(src).filePath(sub);
        if (!QDir(from).exists()) {
            continue;
        }
        const QString to = QDir(game).filePath(sub);
        // Remove the live dir first so files added after the snapshot (e.g.
        // a mod installed later) truly disappear on undo. Safe: a pre-undo
        // snapshot was just taken above.
        QDir(to).removeRecursively();
        if (!copyDir(from, to, error)) {
            return false;
        }
    }
    const QString fromJson = QDir(src).filePath(QStringLiteral("instance.json"));
    if (QFile::exists(fromJson)) {
        const QString toJson = QDir(m_dataDir).filePath(QStringLiteral("instances/%1/instance.json").arg(instanceId));
        // Keep the live id/order (identity), restore everything else.
        QFile f(toJson);
        QString liveId;
        int liveOrder = 0;
        if (f.open(QIODevice::ReadOnly)) {
            QJsonParseError e{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
            if (e.error == QJsonParseError::NoError && doc.isObject()) {
                liveId = doc.object().value(QStringLiteral("id")).toString();
                liveOrder = doc.object().value(QStringLiteral("order")).toInt(0);
            }
        }
        QFile sf(fromJson);
        if (sf.open(QIODevice::ReadOnly)) {
            QJsonParseError e{};
            QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &e);
            if (e.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject o = doc.object();
                if (!liveId.isEmpty()) {
                    o[QStringLiteral("id")] = liveId;
                }
                o[QStringLiteral("order")] = liveOrder;
                QSaveFile out(toJson);
                if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
                    out.commit();
                }
            }
        }
    }
    emit changed(instanceId);
    Logger::info(QStringLiteral("Embers restored %1 from %2").arg(instanceId, snapshotName));
    return true;
}

bool Embers::undoLast(const QString &instanceId, QString *error)
{
    auto all = snapshots(instanceId);
    // Skip pre-undo snapshots when choosing what to undo *to* — but if the
    // only snapshots are pre-undos, the newest one is still a valid target.
    QString target;
    for (const auto &s : all) {
        if (!s.name.contains(QStringLiteral("pre-undo"))) {
            target = s.name;
            break;
        }
    }
    if (target.isEmpty() && !all.isEmpty()) {
        target = all.first().name;
    }
    if (target.isEmpty()) {
        if (error) {
            *error = tr("No snapshots yet for this profile. Snapshots are taken automatically before changes.");
        }
        return false;
    }
    return restore(instanceId, target, error);
}

QString Embers::lastTouchedInstance() const
{
    QFile f(QDir(m_dataDir).filePath(QStringLiteral("meta/embers-last.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    return (e.error == QJsonParseError::NoError && doc.isObject())
        ? doc.object().value(QStringLiteral("instanceId")).toString()
        : QString();
}

QString Embers::lastSnapshotName() const
{
    QFile f(QDir(m_dataDir).filePath(QStringLiteral("meta/embers-last.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    return (e.error == QJsonParseError::NoError && doc.isObject())
        ? doc.object().value(QStringLiteral("snapshot")).toString()
        : QString();
}

#include "Embers.moc"
