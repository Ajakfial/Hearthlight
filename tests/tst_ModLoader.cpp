// Tests: loader metadata parsing (offline fixtures, no network).
#include "ModLoader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

static QJsonArray arr(const char *json)
{
    QJsonParseError e{};
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(json), &e);
    Q_ASSERT(e.error == QJsonParseError::NoError);
    return d.array();
}
static QJsonObject obj(const char *json)
{
    QJsonParseError e{};
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(json), &e);
    Q_ASSERT(e.error == QJsonParseError::NoError);
    return d.object();
}

class TstModLoader : public QObject {
    Q_OBJECT
private slots:
    void typeStrings();
    void fabricParses();
    void quiltParses();
    void forgePromos();
    void mavenXml();
    void latestStable();
    void effectiveMerge();
};

void TstModLoader::typeStrings()
{
    QCOMPARE(loaderTypeToString(LoaderType::Fabric), QStringLiteral("fabric"));
    QCOMPARE(loaderTypeFromString(QStringLiteral("NeoForge")), LoaderType::NeoForge);
    QCOMPARE(loaderTypeFromString(QStringLiteral("bogus")), LoaderType::Vanilla);
    QVERIFY(!loaderNeedsInstall(LoaderType::Vanilla));
    QVERIFY(loaderNeedsInstall(LoaderType::Forge));
}

void TstModLoader::fabricParses()
{
    const auto all = LoaderMeta::parseFabricLoaders(
        arr(R"([{"loader":{"version":"0.16.9","stable":true}},{"loader":{"version":"0.17.0-beta.1","stable":false}}])"),
        QStringLiteral("1.20.4"));
    QCOMPARE(all.size(), 2);
    QVERIFY(all.first().stable);
    QVERIFY(!all.last().stable);
}

void TstModLoader::quiltParses()
{
    QJsonObject wrap;
    wrap[QStringLiteral("loader")] =
        arr(R"([{"version":"0.25.0"},{"version":"0.26.0-beta.1"}])");
    const auto all = LoaderMeta::parseQuiltLoaders(wrap, QStringLiteral("1.20.4"));
    QCOMPARE(all.size(), 2);
    QVERIFY(all.first().stable);
}

void TstModLoader::forgePromos()
{
    const auto all = LoaderMeta::parseForgePromotions(
        obj(R"({"1.20.1-latest":"47.2.0","1.20.1-recommended":"47.1.3","1.19.4-latest":"45.2.0"})"),
        QStringLiteral("1.20.1"));
    QCOMPARE(all.size(), 2); // only the 1.20.1 line
    bool hasRec = false;
    for (const auto &lv : all) {
        if (lv.version == QStringLiteral("47.1.3") && lv.stable) {
            hasRec = true;
        }
    }
    QVERIFY(hasRec);
}

void TstModLoader::mavenXml()
{
    const QByteArray xml = R"(<metadata><versioning><versions>
        <version>20.4.0-beta</version><version>20.4.1</version>
    </versions></versioning></metadata>)";
    const auto all = LoaderMeta::parseMavenMetadataXml(xml);
    QCOMPARE(all.size(), 2);
    QVERIFY(!all.first().stable);
    QVERIFY(all.last().stable);
}

void TstModLoader::latestStable()
{
    QList<LoaderVersion> all = { { QStringLiteral("0.17.0-beta"), {}, false, {} },
                                 { QStringLiteral("0.16.9"), {}, true, {} } };
    QCOMPARE(LoaderMeta::latestStable(all), QStringLiteral("0.16.9"));
    QVERIFY(LoaderMeta::latestStable({}).isEmpty());
}

void TstModLoader::effectiveMerge()
{
    ParsedVersion vanilla;
    vanilla.id = QStringLiteral("1.20.4");
    vanilla.mainClass = QStringLiteral("net.minecraft.client.main.Main");
    Library l;
    l.name = QStringLiteral("a:b:1");
    vanilla.libraries.append(l);
    LoaderProfile prof;
    prof.ok = true;
    prof.overlay.id = QStringLiteral("1.20.4-fabric");
    prof.overlay.mainClass = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
    Library l2;
    l2.name = QStringLiteral("net.fabricmc:loader:0.16.9");
    prof.overlay.libraries.append(l2);
    const ParsedVersion eff = ModLoaderInstaller::effectiveVersion(vanilla, prof);
    QCOMPARE(eff.id, QStringLiteral("1.20.4")); // folders stay vanilla-id keyed
    QCOMPARE(eff.libraries.size(), 2);
    QVERIFY(eff.mainClass.contains(QStringLiteral("Knot")));
}

QTEST_MAIN(TstModLoader)
#include "tst_ModLoader.moc"
