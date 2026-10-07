// Tests: Embers snapshots (snapshot/restore/undo/prune, corrupted recovery).
#include "Embers.h"
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

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

class TstEmbers : public QObject {
    Q_OBJECT
private slots:
    void snapshotRestore();
    void undo();
    void prune();
    void lastTouched();
};

void TstEmbers::snapshotRestore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Embers embers(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString game = QDir(dir.path()).filePath(QStringLiteral("instances/%1/game").arg(in.id));
    writeFile(QDir(game).filePath(QStringLiteral("mods/a.jar")), "v1");
    writeFile(QDir(game).filePath(QStringLiteral("config/c.cfg")), "v1");
    QString err;
    const QString snap = embers.snapshot(in.id, QStringLiteral("test"), false, &err);
    QVERIFY2(!snap.isEmpty(), qPrintable(err));
    QCOMPARE(embers.snapshots(in.id).size(), 1);
    // Change live state, then restore.
    writeFile(QDir(game).filePath(QStringLiteral("mods/a.jar")), "v2");
    writeFile(QDir(game).filePath(QStringLiteral("mods/b.jar")), "new");
    QVERIFY(embers.restore(in.id, embers.snapshots(in.id).first().name, &err));
    QCOMPARE(readFile(QDir(game).filePath(QStringLiteral("mods/a.jar"))), QByteArray("v1"));
    QVERIFY(!QFile::exists(QDir(game).filePath(QStringLiteral("mods/b.jar"))));
    // A pre-undo snapshot was preserved.
    bool hasPreUndo = false;
    for (const auto &s : embers.snapshots(in.id)) {
        if (s.name.contains(QStringLiteral("pre-undo"))) {
            hasPreUndo = true;
        }
    }
    QVERIFY(hasPreUndo);
}

void TstEmbers::undo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Embers embers(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QString err;
    QVERIFY(!embers.undoLast(in.id, &err)); // nothing yet
    const QString game = QDir(dir.path()).filePath(QStringLiteral("instances/%1/game").arg(in.id));
    writeFile(QDir(game).filePath(QStringLiteral("mods/a.jar")), "v1");
    QVERIFY(!embers.snapshot(in.id, QStringLiteral("before"), false, &err).isEmpty());
    writeFile(QDir(game).filePath(QStringLiteral("mods/a.jar")), "v2");
    QVERIFY(embers.undoLast(in.id, &err));
    QCOMPARE(readFile(QDir(game).filePath(QStringLiteral("mods/a.jar"))), QByteArray("v1"));
}

void TstEmbers::prune()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Embers embers(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QString err;
    for (int i = 0; i < Embers::maxSnapshots() + 3; ++i) {
        // Distinct reasons => distinct names even within the same second.
        QVERIFY(!embers.snapshot(in.id, QStringLiteral("test-%1").arg(i), false, &err).isEmpty());
    }
    QCOMPARE(embers.snapshots(in.id).size(), Embers::maxSnapshots());
}

void TstEmbers::lastTouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Embers embers(dir.path());
    QVERIFY(embers.lastTouchedInstance().isEmpty());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QString err;
    QVERIFY(!embers.snapshot(in.id, QStringLiteral("test"), false, &err).isEmpty());
    QCOMPARE(embers.lastTouchedInstance(), in.id);
    QVERIFY(!embers.lastSnapshotName().isEmpty());
}

QTEST_MAIN(TstEmbers)
#include "tst_Embers.moc"
