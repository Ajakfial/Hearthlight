// Tests: installed-mods manager (sidecars, toggle, remove, manual detect).
#include "InstanceManager.h"
#include "ModManager.h"

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

class TstMods : public QObject {
    Q_OBJECT
private slots:
    void emptyList();
    void manualDetect();
    void toggle();
    void remove();
    void addExternalJar();
    void disabledSuffix();
};

void TstMods::emptyList()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    QVERIFY(mods.listMods(in.id).isEmpty());
}

void TstMods::manualDetect()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString jar = QDir(ModManager::modsDir(dir.path(), in.id)).filePath(QStringLiteral("cool-1.0.jar"));
    QDir().mkpath(QFileInfo(jar).absolutePath());
    QFile f(jar);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
    const auto list = mods.listMods(in.id);
    QCOMPARE(list.size(), 1);
    QVERIFY(list.first().manual);
    QVERIFY(list.first().enabled);
    QCOMPARE(list.first().title, QStringLiteral("cool-1.0"));
}

void TstMods::toggle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString jar = QDir(ModManager::modsDir(dir.path(), in.id)).filePath(QStringLiteral("cool-1.0.jar"));
    QDir().mkpath(QFileInfo(jar).absolutePath());
    QFile f(jar);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
    QString err;
    QVERIFY(mods.setEnabled(in.id, QStringLiteral("cool-1.0.jar"), false, &err));
    auto list = mods.listMods(in.id);
    QCOMPARE(list.size(), 1);
    QVERIFY(!list.first().enabled);
    QCOMPARE(list.first().fileName, QStringLiteral("cool-1.0.jar.disabled"));
    QVERIFY(mods.setEnabled(in.id, QStringLiteral("cool-1.0.jar.disabled"), true, &err));
    list = mods.listMods(in.id);
    QVERIFY(list.first().enabled);
}

void TstMods::remove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString jar = QDir(ModManager::modsDir(dir.path(), in.id)).filePath(QStringLiteral("cool-1.0.jar"));
    QDir().mkpath(QFileInfo(jar).absolutePath());
    QFile f(jar);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
    QString err;
    QVERIFY(mods.removeMod(in.id, QStringLiteral("cool-1.0.jar"), &err));
    QVERIFY(mods.listMods(in.id).isEmpty());
    QVERIFY(!mods.removeMod(in.id, QStringLiteral("cool-1.0.jar"), &err)); // gone already
}

void TstMods::addExternalJar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QTemporaryDir src;
    QVERIFY(src.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString jar = QDir(src.path()).filePath(QStringLiteral("handmade.jar"));
    QFile f(jar);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
    QString err, added;
    QVERIFY(mods.addExternalJar(in.id, jar, &err, &added));
    QCOMPARE(added, QStringLiteral("handmade.jar"));
    QVERIFY(!mods.addExternalJar(in.id, jar, &err, nullptr)); // clash, no overwrite
    const QString txt = QDir(src.path()).filePath(QStringLiteral("note.txt"));
    QFile t(txt);
    QVERIFY(t.open(QIODevice::WriteOnly));
    t.write("x");
    t.close();
    QVERIFY(!mods.addExternalJar(in.id, txt, &err, nullptr)); // not a jar
}

void TstMods::disabledSuffix()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const Instance in = makeInstance(mgr, QStringLiteral("Pack"));
    const QString jar =
        QDir(ModManager::modsDir(dir.path(), in.id)).filePath(QStringLiteral("cool-1.0.jar.disabled"));
    QDir().mkpath(QFileInfo(jar).absolutePath());
    QFile f(jar);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
    const auto list = mods.listMods(in.id);
    QCOMPARE(list.size(), 1);
    QVERIFY(!list.first().enabled);
    QCOMPARE(list.first().baseName, QStringLiteral("cool-1.0"));
}

QTEST_MAIN(TstMods)
#include "tst_Mods.moc"
