// Tests: instance model round-trip, id sanitizing, account modes, dirs.
#include "Instance.h"
#include "InstanceManager.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TstInstance : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void sanitize();
    void accountModes();
    void createCloneRemove();
    void duplicateSafeIds();
};

void TstInstance::roundTrip()
{
    Instance in;
    in.id = QStringLiteral("cozy");
    in.name = QStringLiteral("Cozy survival");
    in.versionId = QStringLiteral("1.20.4");
    in.loaderType = QStringLiteral("fabric");
    in.loaderVersion = QStringLiteral("0.16.9");
    in.accountMode = InstanceAccountMode::Specific;
    in.accountId = QStringLiteral("abc");
    in.memoryMb = 4096;
    in.extraJvmArgs = QStringLiteral("-Dfoo=1");
    in.width = 1280;
    in.height = 720;
    in.quickPlayWorld = QStringLiteral("New World");
    in.envVars.insert(QStringLiteral("FOO"), QStringLiteral("bar"));
    in.group = QStringLiteral("Modpacks");
    bool ok = false;
    const Instance back = Instance::fromJson(in.toJson(), &ok);
    QVERIFY(ok);
    QCOMPARE(back.name, in.name);
    QCOMPARE(back.loaderVersion, QStringLiteral("0.16.9"));
    QCOMPARE(back.accountMode, InstanceAccountMode::Specific);
    QCOMPARE(back.quickPlayWorld, QStringLiteral("New World"));
    QCOMPARE(back.envVars.value(QStringLiteral("FOO")), QStringLiteral("bar"));
}

void TstInstance::sanitize()
{
    QCOMPARE(Instance::sanitizeId(QStringLiteral("My Cool World!")), QStringLiteral("my-cool-world"));
    QCOMPARE(Instance::sanitizeId(QStringLiteral("  ")), QStringLiteral("instance"));
    QVERIFY(Instance::sanitizeId(QStringLiteral("A")).size() <= 64);
}

void TstInstance::accountModes()
{
    QCOMPARE(instanceAccountModeToString(InstanceAccountMode::Current), QStringLiteral("current"));
    QCOMPARE(instanceAccountModeFromString(QStringLiteral("ask")), InstanceAccountMode::Ask);
    QCOMPARE(instanceAccountModeFromString(QStringLiteral("specific")), InstanceAccountMode::Specific);
    QCOMPARE(instanceAccountModeFromString(QStringLiteral("bogus")), InstanceAccountMode::Current);
}

void TstInstance::createCloneRemove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Instance in;
    in.name = QStringLiteral("Survival");
    in.versionId = QStringLiteral("1.20.4");
    QString err;
    QVERIFY(mgr.create(in, &err));
    QCOMPARE(mgr.instances().size(), 1);
    const QString id = mgr.instances().first().id;
    QVERIFY(QFile::exists(instanceRootDir(dir.path(), id) + QStringLiteral("/instance.json")));
    mgr.ensureGameDirs(id);
    QVERIFY(QDir(instanceGameDir(dir.path(), mgr.get(id)) + QStringLiteral("/mods")).exists());
    QString newId;
    QVERIFY(mgr.clone(id, QStringLiteral("Copy"), &newId, &err));
    QCOMPARE(mgr.instances().size(), 2);
    QVERIFY(mgr.rename(newId, QStringLiteral("Renamed"), &err));
    QCOMPARE(mgr.get(newId).name, QStringLiteral("Renamed"));
    QVERIFY(mgr.remove(newId, false));
    QCOMPARE(mgr.instances().size(), 1);
}

void TstInstance::duplicateSafeIds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Instance a;
    a.name = QStringLiteral("Same");
    a.versionId = QStringLiteral("1.20.1");
    Instance b = a;
    QString err;
    QVERIFY(mgr.create(a, &err));
    QVERIFY(mgr.create(b, &err)); // same name -> unique id, no clobber
    QVERIFY(mgr.instances().at(0).id != mgr.instances().at(1).id);
}

QTEST_MAIN(TstInstance)
#include "tst_Instance.moc"
