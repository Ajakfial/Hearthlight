// Tests: content-pack manager (resourcepacks / shaderpacks / datapacks).
#include "InstanceManager.h"
#include "ModManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

static QString makeInstance(InstanceManager &mgr, ModManager &mods, const QString &name)
{
    Q_UNUSED(mods);
    Instance in;
    in.name = name;
    in.versionId = QStringLiteral("1.20.1");
    QString err;
    if (!mgr.create(in, &err)) {
        qWarning("makeInstance failed: %s", qPrintable(err));
        return {};
    }
    return mgr.instances().last().id;
}

static void writeFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake");
    f.close();
}

class TstPacks : public QObject {
    Q_OBJECT
private slots:
    void emptyLists();
    void listZips();
    void toggleZip();
    void removeZip();
    void addExternalPack();
    void unpackedFoldersListed();
};

void TstPacks::emptyLists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    QVERIFY(!id.isEmpty());
    QVERIFY(mods.listContent(id, QStringLiteral("resourcepacks")).isEmpty());
    QVERIFY(mods.listContent(id, QStringLiteral("shaderpacks")).isEmpty());
    QVERIFY(mods.listContent(id, QStringLiteral("datapacks")).isEmpty());
}

void TstPacks::listZips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    writeFile(QDir(ModManager::contentDir(dir.path(), id, QStringLiteral("resourcepacks")))
                  .filePath(QStringLiteral("faithful.zip")));
    const auto list = mods.listContent(id, QStringLiteral("resourcepacks"));
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().title, QStringLiteral("faithful"));
    QVERIFY(list.first().enabled);
    QVERIFY(list.first().manual);
}

void TstPacks::toggleZip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    writeFile(QDir(ModManager::contentDir(dir.path(), id, QStringLiteral("shaderpacks")))
                  .filePath(QStringLiteral("bsl.zip")));
    QString err;
    QVERIFY(mods.setContentEnabled(id, QStringLiteral("shaderpacks"), QStringLiteral("bsl.zip"), false, &err));
    auto list = mods.listContent(id, QStringLiteral("shaderpacks"));
    QCOMPARE(list.size(), 1);
    QVERIFY(!list.first().enabled);
    QCOMPARE(list.first().fileName, QStringLiteral("bsl.zip.disabled"));
    QVERIFY(mods.setContentEnabled(id, QStringLiteral("shaderpacks"), QStringLiteral("bsl.zip.disabled"), true,
                                    &err));
    list = mods.listContent(id, QStringLiteral("shaderpacks"));
    QVERIFY(list.first().enabled);
}

void TstPacks::removeZip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    writeFile(QDir(ModManager::contentDir(dir.path(), id, QStringLiteral("datapacks")))
                  .filePath(QStringLiteral("data.zip")));
    QString err;
    QVERIFY(mods.removeContent(id, QStringLiteral("datapacks"), QStringLiteral("data.zip"), &err));
    QVERIFY(mods.listContent(id, QStringLiteral("datapacks")).isEmpty());
    QVERIFY(!mods.removeContent(id, QStringLiteral("datapacks"), QStringLiteral("data.zip"), &err));
}

void TstPacks::addExternalPack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QTemporaryDir src;
    QVERIFY(src.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    const QString zip = QDir(src.path()).filePath(QStringLiteral("cute.zip"));
    writeFile(zip);
    QString err, added;
    QVERIFY(mods.addExternalPack(id, QStringLiteral("resourcepacks"), zip, { QStringLiteral("*.zip") }, &err,
                                 &added));
    QCOMPARE(added, QStringLiteral("cute.zip"));
    QVERIFY(!mods.addExternalPack(id, QStringLiteral("resourcepacks"), zip, { QStringLiteral("*.zip") }, &err,
                                  nullptr)); // clash
    const QString txt = QDir(src.path()).filePath(QStringLiteral("note.txt"));
    writeFile(txt);
    QVERIFY(!mods.addExternalPack(id, QStringLiteral("resourcepacks"), txt, { QStringLiteral("*.zip") }, &err,
                                  nullptr)); // wrong extension
}

void TstPacks::unpackedFoldersListed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    ModManager mods(dir.path());
    const QString id = makeInstance(mgr, mods, QStringLiteral("Packs"));
    QDir().mkpath(
        QDir(ModManager::contentDir(dir.path(), id, QStringLiteral("resourcepacks"))).filePath(QStringLiteral("unpacked")));
    const auto list = mods.listContent(id, QStringLiteral("resourcepacks"));
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().fileName, QStringLiteral("unpacked"));
    // Unpacked folders cannot be toggled — honest error instead of a fake success.
    QString err;
    QVERIFY(!mods.setContentEnabled(id, QStringLiteral("resourcepacks"), QStringLiteral("unpacked"), false, &err));
    QVERIFY(!err.isEmpty());
}

QTEST_MAIN(TstPacks)
#include "tst_Packs.moc"
