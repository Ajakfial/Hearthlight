// Tests: Hearthstones world/config backups (create/list/restore/prune).
#include "Hearthstones.h"
#include "InstanceManager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static Instance makeInstance(InstanceManager &mgr, const QString &name)
{
    Instance in;
    in.name = name;
    in.versionId = QStringLiteral("1.20.1");
    QString err;
    if (!mgr.create(in, &err)) {
        qWarning("makeInstance failed: %s", qPrintable(err));
        return {};
    }
    return mgr.instances().last();
}

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(data);
    f.close();
}

class TstHearthstones : public QObject {
    Q_OBJECT
private slots:
    void worldsListed();
    void createRestore();
    void autoPrune();
    void emptyHonest();
};

void TstHearthstones::worldsListed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Hearthstones stones(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QVERIFY(stones.listWorlds(in.id).isEmpty());
    const QString game = QDir(dir.path()).filePath(QStringLiteral("instances/%1/game").arg(in.id));
    writeFile(QDir(game).filePath(QStringLiteral("saves/My World/level.dat")), "nbt");
    writeFile(QDir(game).filePath(QStringLiteral("saves/Empty-ish/notes.txt")), "x"); // no level.dat: skipped
    const auto worlds = stones.listWorlds(in.id);
    QCOMPARE(worlds.size(), 1);
    QCOMPARE(worlds.first().folder, QStringLiteral("My World"));
}

void TstHearthstones::createRestore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Hearthstones stones(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString game = QDir(dir.path()).filePath(QStringLiteral("instances/%1/game").arg(in.id));
    writeFile(QDir(game).filePath(QStringLiteral("saves/My World/level.dat")), "v1");
    writeFile(QDir(game).filePath(QStringLiteral("config/o.cfg")), "v1");
    QString err;
    const QString name = stones.create(in.id, QStringLiteral("before-nether"), {}, true, false, &err);
    QVERIFY2(!name.isEmpty(), qPrintable(err));
    QCOMPARE(stones.list(in.id).size(), 1);
    QVERIFY(!stones.list(in.id).first().automatic);
    // Play on, then restore.
    writeFile(QDir(game).filePath(QStringLiteral("saves/My World/level.dat")), "v2-blown-up");
    QVERIFY(stones.restore(in.id, name, &err));
    QFile f(QDir(game).filePath(QStringLiteral("saves/My World/level.dat")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("v1"));
}

void TstHearthstones::autoPrune()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Hearthstones stones(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString game = QDir(dir.path()).filePath(QStringLiteral("instances/%1/game").arg(in.id));
    writeFile(QDir(game).filePath(QStringLiteral("saves/W/level.dat")), "x");
    QString err;
    for (int i = 0; i < Hearthstones::maxAutoSnapshots() + 2; ++i) {
        QVERIFY(!stones.create(in.id, {}, {}, true, true, &err).isEmpty());
    }
    int autos = 0;
    for (const auto &h : stones.list(in.id)) {
        if (h.automatic) {
            ++autos;
        }
    }
    QCOMPARE(autos, Hearthstones::maxAutoSnapshots());
    // Manual snapshots are never pruned.
    QVERIFY(!stones.create(in.id, QStringLiteral("keep-forever-%1").arg(autos), {}, true, false, &err).isEmpty());
    int manuals = 0;
    for (const auto &h : stones.list(in.id)) {
        if (!h.automatic) {
            ++manuals;
        }
    }
    QCOMPARE(manuals, 1);
}

void TstHearthstones::emptyHonest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Hearthstones stones(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QString err;
    QVERIFY(stones.create(in.id, {}, {}, false, false, &err).isEmpty()); // nothing to back up
    QVERIFY(!err.isEmpty());
    QVERIFY(stones.autoSnapshotIfNeeded(in.id).isEmpty()); // no worlds
}

QTEST_MAIN(TstHearthstones)
#include "tst_Hearthstones.moc"
