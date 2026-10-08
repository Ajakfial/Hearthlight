// Tests: CurseForge manifest/file parsing (pure, no network) + .mrpack export.
#include "CurseForgeApi.h"
#include "InstanceManager.h"
#include "ModManager.h"
#include "ZipUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

static const QByteArray kManifest = R"({
  "minecraft": {
    "version": "1.20.1",
    "modLoaders": [ { "id": "forge-47.2.0", "primary": true } ]
  },
  "manifestType": "minecraftModpack",
  "manifestVersion": 1,
  "name": "Cozy Pack",
  "version": "2.0",
  "author": "tester",
  "files": [
    { "projectID": 123, "fileID": 456, "required": true },
    { "projectID": 789, "fileID": 101112, "required": false },
    { "projectID": 0, "fileID": 0, "required": true }
  ],
  "overrides": "overrides"
})";

class TstCurseForge : public QObject {
    Q_OBJECT
private slots:
    void manifestBasics();
    void manifestLoaderFlavors();
    void manifestVersionDefault();
    void parseFileResponse();
    void parseFileResponseGarbage();
    void exportMrpackRoundTrip();
};

void TstCurseForge::manifestBasics()
{
    QCOMPARE(CurseForgeMeta::manifestName(kManifest), QStringLiteral("Cozy Pack"));
    QCOMPARE(CurseForgeMeta::manifestMinecraftVersion(kManifest), QStringLiteral("1.20.1"));
    QCOMPARE(CurseForgeMeta::manifestLoaderType(kManifest), QStringLiteral("forge"));
    const auto files = CurseForgeMeta::manifestFiles(kManifest);
    QCOMPARE(files.size(), 2); // the (0,0) entry is skipped
    QCOMPARE(files.first().first, 123);
    QCOMPARE(files.first().second, (qint64)456);
}

void TstCurseForge::manifestLoaderFlavors()
{
    QCOMPARE(CurseForgeMeta::manifestLoaderType(R"({"minecraft":{"version":"1.20.1","modLoaders":[{"id":"fabric-0.15.0"}]}})"),
             QStringLiteral("fabric"));
    QCOMPARE(CurseForgeMeta::manifestLoaderType(R"({"minecraft":{"version":"1.20.1","modLoaders":[{"id":"quilt-8"}]}})"),
             QStringLiteral("quilt"));
    QCOMPARE(CurseForgeMeta::manifestLoaderType(R"({"minecraft":{"version":"1.20.1","modLoaders":[{"id":"neoforge-20.4"}]}})"),
             QStringLiteral("neoforge"));
    QCOMPARE(CurseForgeMeta::manifestLoaderType(R"({"minecraft":{"version":"1.19.2"}})"),
             QStringLiteral("forge")); // no loader entry: legacy default
    QCOMPARE(CurseForgeMeta::manifestLoaderType(QByteArrayLiteral("not json")),
             QStringLiteral("forge")); // corrupt manifest: honest default
}

void TstCurseForge::manifestVersionDefault()
{
    QCOMPARE(CurseForgeMeta::manifestMinecraftVersion(QByteArrayLiteral("{}")), QStringLiteral("1.20.1"));
    QCOMPARE(CurseForgeMeta::manifestName(QByteArrayLiteral("{}")), QStringLiteral("CurseForge pack"));
    QVERIFY(CurseForgeMeta::manifestFiles(QByteArrayLiteral("garbage")).isEmpty());
}

void TstCurseForge::parseFileResponse()
{
    const QByteArray raw = R"({"data": {
        "id": 456, "modId": 123, "fileName": "jei-1.20.1.jar",
        "downloadUrl": "https://edge.forgecdn.net/files/456/jei.jar",
        "fileLength": 424242, "isServerPack": false }})";
    const CurseForgeFile f = CurseForgeMeta::parseFileResponse(raw, 123, 456);
    QCOMPARE(f.projectId, 123);
    QCOMPARE(f.fileId, (qint64)456);
    QCOMPARE(f.fileName, QStringLiteral("jei-1.20.1.jar"));
    QCOMPARE(f.downloadUrl, QStringLiteral("https://edge.forgecdn.net/files/456/jei.jar"));
    QCOMPARE(f.size, (qint64)424242);
    QVERIFY(!f.serverPack);
}

void TstCurseForge::parseFileResponseGarbage()
{
    const CurseForgeFile f = CurseForgeMeta::parseFileResponse(QByteArrayLiteral("nope"), 1, 2);
    QVERIFY(f.downloadUrl.isEmpty());
    QVERIFY(f.fileName.isEmpty());
}

void TstCurseForge::exportMrpackRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InstanceManager mgr(dir.path());
    Instance in;
    in.name = QStringLiteral("Export me");
    in.versionId = QStringLiteral("1.20.1");
    in.loaderType = QStringLiteral("fabric");
    in.loaderVersion = QStringLiteral("0.15.0");
    QString err;
    QVERIFY(mgr.create(in, &err));
    const QString id = mgr.instances().last().id;

    // A hand-added mod + a resource pack travel in overrides/.
    ModManager mods(dir.path());
    const QString jar = QDir(ModManager::modsDir(dir.path(), id)).filePath(QStringLiteral("handmade.jar"));
    QDir().mkpath(QFileInfo(jar).absolutePath());
    QFile jf(jar);
    QVERIFY(jf.open(QIODevice::WriteOnly));
    jf.write("fake-jar");
    jf.close();
    const QString rp =
        QDir(ModManager::contentDir(dir.path(), id, QStringLiteral("resourcepacks"))).filePath(QStringLiteral("pack.zip"));
    QDir().mkpath(QFileInfo(rp).absolutePath());
    QFile rf(rp);
    QVERIFY(rf.open(QIODevice::WriteOnly));
    rf.write("fake-pack");
    rf.close();

    const QString zip = QDir(dir.path()).filePath(QStringLiteral("out.mrpack"));
    QVERIFY(mgr.exportMrpack(id, zip, &err));
    const QStringList entries = ZipUtil::listZipEntries(zip, &err);
    QVERIFY(entries.contains(QStringLiteral("modrinth.index.json")));
    QVERIFY(entries.contains(QStringLiteral("overrides/mods/handmade.jar")));
    QVERIFY(entries.contains(QStringLiteral("overrides/resourcepacks/pack.zip")));

    // The index is valid JSON with our game + loader pinned.
    // (Extract just the index via a temp dir.)
    QTemporaryDir out;
    QVERIFY(out.isValid());
    QString zerr;
    QVERIFY(ZipUtil::extractZipFile(zip, out.path(), {}, &zerr));
    QFile idx(QDir(out.path()).filePath(QStringLiteral("modrinth.index.json")));
    QVERIFY(idx.open(QIODevice::ReadOnly));
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(idx.readAll(), &e);
    QVERIFY(e.error == QJsonParseError::NoError);
    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("game")).toString(), QStringLiteral("minecraft"));
    const QJsonObject deps = root.value(QStringLiteral("dependencies")).toObject();
    QCOMPARE(deps.value(QStringLiteral("minecraft")).toString(), QStringLiteral("1.20.1"));
    QCOMPARE(deps.value(QStringLiteral("fabric-loader")).toString(), QStringLiteral("0.15.0"));
}

QTEST_MAIN(TstCurseForge)
#include "tst_CurseForge.moc"
