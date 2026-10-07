// Tests: Modrinth metadata parsing, facets, compat picking, dep collection
// (all offline fixtures — no network).
#include "ModrinthApi.h"

#include <QJsonDocument>
#include <QTest>

static QJsonObject obj(const QByteArray &json)
{
    QJsonParseError e{};
    const QJsonDocument d = QJsonDocument::fromJson(json, &e);
    Q_ASSERT(e.error == QJsonParseError::NoError);
    return d.object();
}

class TstModrinth : public QObject {
    Q_OBJECT
private slots:
    void facets();
    void searchPath();
    void parseSearch();
    void parseProject();
    void parseVersions();
    void compatible();
    void pickBest();
    void requiredDeps();
    void targetDirs();
    void prettyCounts();
};

void TstModrinth::facets()
{
    ModrinthFilters f;
    QCOMPARE(ModrinthMeta::buildFacets(f), QStringLiteral("[]"));
    f.projectType = QStringLiteral("mod");
    f.loader = QStringLiteral("fabric");
    f.gameVersion = QStringLiteral("1.20.1");
    f.category = QStringLiteral("adventure");
    const QString facets = ModrinthMeta::buildFacets(f);
    QVERIFY(facets.contains(QStringLiteral("project_type:mod")));
    QVERIFY(facets.contains(QStringLiteral("categories:fabric")));
    QVERIFY(facets.contains(QStringLiteral("versions:1.20.1")));
    QVERIFY(facets.contains(QStringLiteral("categories:adventure")));
}

void TstModrinth::searchPath()
{
    ModrinthFilters f;
    f.query = QStringLiteral("sodium");
    f.sort = QStringLiteral("downloads");
    f.limit = 20;
    const QString p = ModrinthMeta::searchPath(f);
    QVERIFY(p.startsWith(QStringLiteral("/v2/search?")));
    QVERIFY(p.contains(QStringLiteral("query=sodium")));
    QVERIFY(p.contains(QStringLiteral("index=downloads")));
    ModrinthFilters bad = f;
    bad.sort = QStringLiteral("bogus");
    QVERIFY(ModrinthMeta::searchPath(bad).contains(QStringLiteral("index=relevance")));
}

void TstModrinth::parseSearch()
{
    const QByteArray raw = "{\"hits\":[{\"project_id\":\"AANobbMI\",\"slug\":\"sodium\",\"title\":\"Sodium\","
                           "\"author\":\"jellysquid3\",\"description\":\"Speed!\",\"icon_url\":\"https://x/i.png\","
                           "\"categories\":[\"fabric\",\"optimization\"],\"versions\":[\"1.20.1\"],"
                           "\"downloads\":1234567,\"follows\":100,\"project_type\":\"mod\"}],"
                           "\"offset\":0,\"limit\":20,\"total_hits\":1}";
    const ModrinthSearchPage page = ModrinthMeta::parseSearch(obj(raw));
    QCOMPARE(page.totalHits, 1);
    QCOMPARE(page.hits.size(), 1);
    QCOMPARE(page.hits.first().slug, QStringLiteral("sodium"));
    QCOMPARE(page.hits.first().author, QStringLiteral("jellysquid3"));
    QVERIFY(page.hits.first().categories.contains(QStringLiteral("fabric")));
}

void TstModrinth::parseProject()
{
    const QByteArray raw = "{\"id\":\"AANobbMI\",\"slug\":\"sodium\",\"title\":\"Sodium\","
                           "\"description\":\"Speed!\",\"body\":\"# Hi\","
                           "\"icon_url\":\"https://x/i.png\",\"downloads\":10,\"followers\":5,"
                           "\"categories\":[\"fabric\"],\"loaders\":[\"fabric\"],"
                           "\"game_versions\":[\"1.20.1\"],\"project_type\":\"mod\","
                           "\"client_side\":\"required\",\"server_side\":\"unsupported\","
                           "\"license\":{\"id\":\"lgpl-3.0\",\"name\":\"LGPL-3.0\"},"
                           "\"source_url\":\"https://github.com/x\","
                           "\"gallery\":[{\"url\":\"https://x/g.png\",\"title\":\"G\"}]}";
    const ModrinthProject p = ModrinthMeta::parseProject(obj(raw), QStringLiteral("jellysquid3"));
    QCOMPARE(p.title, QStringLiteral("Sodium"));
    QCOMPARE(p.author, QStringLiteral("jellysquid3"));
    QCOMPARE(p.licenseId, QStringLiteral("lgpl-3.0"));
    QCOMPARE(p.gallery.size(), 1);
    QCOMPARE(p.clientSide, QStringLiteral("required"));
}

void TstModrinth::parseVersions()
{
    const QByteArray raw = "[{\"id\":\"v1\",\"project_id\":\"AANobbMI\",\"name\":\"Sodium 1.0\","
                           "\"version_number\":\"1.0\",\"version_type\":\"release\","
                           "\"loaders\":[\"fabric\"],\"game_versions\":[\"1.20.1\"],"
                           "\"changelog\":\"Fixed.\",\"featured\":true,"
                           "\"files\":[{\"url\":\"https://cdn/x.jar\",\"filename\":\"sodium-1.0.jar\","
                           "\"hashes\":{\"sha512\":\"abc\",\"sha1\":\"def\"},\"size\":100,\"primary\":true}],"
                           "\"dependencies\":[{\"project_id\":\"P7dR8mSH\",\"dependency_type\":\"required\"},"
                           "{\"dependency_type\":\"embedded\"}]}]";
    const QList<ModrinthVersion> vers = ModrinthMeta::parseVersionArray(raw);
    QCOMPARE(vers.size(), 1);
    QCOMPARE(vers.first().bestFile().filename, QStringLiteral("sodium-1.0.jar"));
    QCOMPARE(vers.first().bestFile().sha512, QStringLiteral("abc"));
    QCOMPARE(ModrinthMeta::requiredDeps(vers.first()), QStringList{ QStringLiteral("P7dR8mSH") });
    // Corrupt input -> empty, never crash.
    QVERIFY(ModrinthMeta::parseVersionArray(QByteArray("nope")).isEmpty());
}

void TstModrinth::compatible()
{
    ModrinthVersion v;
    v.files.append(ModrinthFile{ QStringLiteral("https://x"), QStringLiteral("a.jar") });
    v.gameVersions = { QStringLiteral("1.20.1") };
    v.loaders = { QStringLiteral("fabric") };
    QVERIFY(ModrinthMeta::versionCompatible(v, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                            QStringLiteral("mod")));
    QVERIFY(!ModrinthMeta::versionCompatible(v, QStringLiteral("1.19.4"), QStringLiteral("fabric"),
                                             QStringLiteral("mod")));
    QVERIFY(!ModrinthMeta::versionCompatible(v, QStringLiteral("1.20.1"), QStringLiteral("forge"),
                                             QStringLiteral("mod")));
    // Shaders target "minecraft", not a loader.
    ModrinthVersion s = v;
    s.loaders = { QStringLiteral("minecraft") };
    QVERIFY(ModrinthMeta::versionCompatible(s, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                            QStringLiteral("shader")));
    QVERIFY(!ModrinthMeta::versionCompatible(s, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                             QStringLiteral("mod")));
    // Empty file list is never installable.
    ModrinthVersion e;
    QVERIFY(!ModrinthMeta::versionCompatible(e, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                             QStringLiteral("mod")));
}

void TstModrinth::pickBest()
{
    auto mk = [](const QString &num, const QString &type) {
        ModrinthVersion v;
        v.id = num;
        v.versionNumber = num;
        v.versionType = type;
        v.files.append(ModrinthFile{ QStringLiteral("https://x"), num + QStringLiteral(".jar") });
        v.gameVersions = { QStringLiteral("1.20.1") };
        v.loaders = { QStringLiteral("fabric") };
        return v;
    };
    // Newest-first input; release preferred over newer-channel? No: newest
    // wins unless its channel is worse.
    QList<ModrinthVersion> all = { mk(QStringLiteral("2.0-beta"), QStringLiteral("beta")),
                                  mk(QStringLiteral("1.0"), QStringLiteral("release")) };
    QCOMPARE(ModrinthMeta::pickBestVersion(all, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                           QStringLiteral("mod")),
             1); // release wins over newer beta
    QList<ModrinthVersion> rel = { mk(QStringLiteral("2.0"), QStringLiteral("release")),
                                   mk(QStringLiteral("1.0"), QStringLiteral("release")) };
    QCOMPARE(ModrinthMeta::pickBestVersion(rel, QStringLiteral("1.20.1"), QStringLiteral("fabric"),
                                           QStringLiteral("mod")),
             0); // newest release wins
    QList<ModrinthVersion> none = { mk(QStringLiteral("1.0"), QStringLiteral("release")) };
    QCOMPARE(ModrinthMeta::pickBestVersion(none, QStringLiteral("1.19.4"), QStringLiteral("fabric"),
                                           QStringLiteral("mod")),
             -1);
}

void TstModrinth::requiredDeps()
{
    ModrinthVersion v;
    ModrinthDependency r1{ QStringLiteral("aaa"), {}, QStringLiteral("required") };
    ModrinthDependency r2{ QStringLiteral("aaa"), {}, QStringLiteral("required") };
    ModrinthDependency o{ QStringLiteral("bbb"), {}, QStringLiteral("optional") };
    ModrinthDependency e{ {}, {}, QStringLiteral("embedded") };
    v.dependencies = { r1, r2, o, e };
    QCOMPARE(ModrinthMeta::requiredDeps(v), QStringList{ QStringLiteral("aaa") });
}

void TstModrinth::targetDirs()
{
    QCOMPARE(ModrinthMeta::targetDirForType(QStringLiteral("mod")), QStringLiteral("mods"));
    QCOMPARE(ModrinthMeta::targetDirForType(QStringLiteral("resourcepack")), QStringLiteral("resourcepacks"));
    QCOMPARE(ModrinthMeta::targetDirForType(QStringLiteral("shader")), QStringLiteral("shaderpacks"));
    QCOMPARE(ModrinthMeta::targetDirForType(QStringLiteral("datapack")), QStringLiteral("datapacks"));
    QCOMPARE(ModrinthMeta::targetDirForType(QStringLiteral("modpack")), QStringLiteral("mods"));
}

void TstModrinth::prettyCounts()
{
    QCOMPARE(ModrinthMeta::prettyCount(999), QStringLiteral("999"));
    QCOMPARE(ModrinthMeta::prettyCount(1500), QStringLiteral("1.5k"));
    QCOMPARE(ModrinthMeta::prettyCount(2000000), QStringLiteral("2.0M"));
}

QTEST_MAIN(TstModrinth)
#include "tst_Modrinth.moc"
