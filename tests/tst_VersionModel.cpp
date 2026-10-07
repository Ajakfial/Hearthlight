// Tests: Mojang version-JSON parsing, rule evaluation, library paths,
// native classifiers, argument building, merging (all offline fixtures).
#include "VersionModel.h"

#include "GamePaths.h"
#include "VersionInstaller.h"

#include <QJsonDocument>
#include <QTest>

static QJsonObject obj(const char *json)
{
    QJsonParseError e{};
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(json), &e);
    Q_ASSERT(e.error == QJsonParseError::NoError);
    return d.object();
}

static OsInfo win64()
{
    return { QStringLiteral("windows"), QStringLiteral("x64"), QStringLiteral("10.0.22631") };
}
static OsInfo osxArm()
{
    return { QStringLiteral("osx"), QStringLiteral("arm64"), QStringLiteral("23.1.0") };
}
static OsInfo linux64()
{
    return { QStringLiteral("linux"), QStringLiteral("x64"), QStringLiteral("6.5.0") };
}

class TstVersionModel : public QObject {
    Q_OBJECT
private slots:
    void rulesEmptyAllows();
    void singleAllowOs();
    void allowThenDisallow();
    void unconditionalAllowThenOsDisallow();
    void featuresGate();
    void archAndVersionRegex();
    void mavenPaths();
    void legacyNativeClassifier();
    void modernVersionParses();
    void legacyArgumentsSplit();
    void placeholderSubstitution();
    void buildArgsRespectsRules();
    void mergeInheritsFrom();
    void activeFeatureSet();
    void modernNativesPlanned();
};

void TstVersionModel::rulesEmptyAllows()
{
    QVERIFY(rulesAllow({}, win64(), {}));
}

void TstVersionModel::singleAllowOs()
{
    const auto rules = QJsonDocument::fromJson(R"([{"action":"allow","os":{"name":"osx"}}])").array();
    QVERIFY(!rulesAllow(rules, win64(), {}));
    QVERIFY(rulesAllow(rules, osxArm(), {}));
    QVERIFY(!rulesAllow(rules, linux64(), {}));
}

void TstVersionModel::allowThenDisallow()
{
    const auto rules = QJsonDocument::fromJson(R"([{"action":"allow"},{"action":"disallow","os":{"name":"osx"}}])").array();
    QVERIFY(rulesAllow(rules, win64(), {}));
    QVERIFY(!rulesAllow(rules, osxArm(), {}));
    QVERIFY(rulesAllow(rules, linux64(), {}));
}

void TstVersionModel::unconditionalAllowThenOsDisallow()
{
    // Exact Mojang pairing shape used around text2speech-style libs.
    const auto rules =
        QJsonDocument::fromJson(R"([{"action":"allow"},{"action":"disallow","os":{"arch":"x86"}}])").array();
    QVERIFY(rulesAllow(rules, win64(), {}));
    OsInfo x86 = win64();
    x86.arch = QStringLiteral("x86");
    QVERIFY(!rulesAllow(rules, x86, {}));
}

void TstVersionModel::featuresGate()
{
    const auto rules =
        QJsonDocument::fromJson(R"([{"action":"allow","features":{"is_demo_user":true}}])").array();
    QVERIFY(!rulesAllow(rules, win64(), {}));
    QVERIFY(rulesAllow(rules, win64(), { QStringLiteral("is_demo_user") }));
}

void TstVersionModel::archAndVersionRegex()
{
    const auto rules =
        QJsonDocument::fromJson(R"([{"action":"allow","os":{"name":"windows","version":"^10\\."}}])").array();
    QVERIFY(rulesAllow(rules, win64(), {}));
    OsInfo old = win64();
    old.osVersion = QStringLiteral("6.3.9600");
    QVERIFY(!rulesAllow(rules, old, {}));
}

void TstVersionModel::mavenPaths()
{
    QCOMPARE(GamePaths::mavenPath(QStringLiteral("org.lwjgl:lwjgl:3.3.1")),
             QStringLiteral("org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1.jar"));
    QCOMPARE(GamePaths::mavenPath(QStringLiteral("org.lwjgl:lwjgl:3.3.1"), QStringLiteral("natives-windows")),
             QStringLiteral("org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar"));
    QCOMPARE(GamePaths::mavenPath(QStringLiteral("com.mojang:patchy:1.3.9:official")),
             QStringLiteral("com/mojang/patchy/1.3.9/patchy-1.3.9-official.jar"));
    QVERIFY(GamePaths::mavenPath(QStringLiteral("bogus")).isEmpty());
}

void TstVersionModel::legacyNativeClassifier()
{
    const Library lib = Library::fromJson(obj(R"({
        "name": "org.lwjgl:lwjgl:2.9.4-nightly-20150209",
        "natives": {"windows": "natives-windows-${arch}", "osx": "natives-macos", "linux": "natives-linux-${arch}"},
        "extract": {"exclude": ["META-INF/"]}
    })"));
    QCOMPARE(lib.nativeClassifierFor(win64()), QStringLiteral("natives-windows-64"));
    QCOMPARE(lib.nativeClassifierFor(osxArm()), QStringLiteral("natives-macos"));
    QCOMPARE(lib.nativeClassifierFor(linux64()), QStringLiteral("natives-linux-64"));
    QCOMPARE(lib.extractExclude, QStringList{ QStringLiteral("META-INF/") });
}

void TstVersionModel::modernVersionParses()
{
    const QJsonObject o = obj(R"({
        "id": "1.20.4", "type": "release", "mainClass": "net.minecraft.client.main.Main",
        "assets": "1.20",
        "arguments": {
            "game": ["--username", "${auth_player_name}", {"value": "--demo", "rules": [{"action": "allow", "features": {"is_demo_user": true}}]}],
            "jvm": [{"value": ["-Djava.library.path=${natives_directory}", "-cp", "${classpath}"], "rules": [{"action": "allow"}]}]
        },
        "libraries": [
            {"name": "com.mojang:logging:1.1.1", "downloads": {"artifact": {"path": "com/mojang/logging/1.1.1/logging-1.1.1.jar", "sha1": "abc", "size": 12, "url": "https://x/y.jar"}}},
            {"name": "org.lwjgl:lwjgl:3.3.2", "rules": [{"action": "allow", "os": {"name": "windows"}}], "downloads": {"artifact": {"path": "p.jar", "url": "https://x/p.jar"}}},
            {"name": "org.lwjgl:lwjgl:3.3.2:natives", "natives": {"windows": "natives-windows"}, "downloads": {"classifiers": {"natives-windows": {"path": "n.jar", "url": "https://x/n.jar"}}}, "extract": {"exclude": ["META-INF/"]}}
        ],
        "assetIndex": {"id": "1.20", "sha1": "h", "size": 10, "totalSize": 100, "url": "https://x/index.json"},
        "downloads": {"client": {"sha1": "c", "size": 20, "url": "https://x/client.jar"}},
        "logging": {"client": {"argument": "-Dlog4j.configurationFile=${path}", "file": {"id": "client-1.12.xml", "sha1": "l", "size": 1, "url": "https://x/log.xml"}}},
        "javaVersion": {"component": "java-runtime-gamma", "majorVersion": 17}
    })");
    QString err;
    const ParsedVersion v = ParsedVersion::fromJson(o, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(v.id, QStringLiteral("1.20.4"));
    QCOMPARE(v.javaMajor, 17);
    QCOMPARE(v.libraries.size(), 3);
    QVERIFY(v.libraries[0].activeFor(linux64(), {}));
    QVERIFY(!v.libraries[1].activeFor(linux64(), {}));
    QVERIFY(v.libraries[1].activeFor(win64(), {}));
    QCOMPARE(v.libraries[2].nativeClassifierFor(win64()), QStringLiteral("natives-windows"));
    QVERIFY(v.hasLogging);

    // Demo-gated game arg.
    QMap<QString, QString> vars{ { QStringLiteral("auth_player_name"), QStringLiteral("River") } };
    const auto plain = buildArgs(v.gameArgs, win64(), {}, vars);
    QVERIFY(!plain.contains(QStringLiteral("--demo")));
    const auto demo = buildArgs(v.gameArgs, win64(), { QStringLiteral("is_demo_user") }, vars);
    QVERIFY(demo.contains(QStringLiteral("--demo")));
    QVERIFY(demo.contains(QStringLiteral("River")));
}

void TstVersionModel::legacyArgumentsSplit()
{
    QCOMPARE(splitLegacyArguments(QStringLiteral("--username ${auth_player_name} --version ${version_name}")),
             QStringList({ QStringLiteral("--username"), QStringLiteral("${auth_player_name}"),
                           QStringLiteral("--version"), QStringLiteral("${version_name}") }));
    QCOMPARE(splitLegacyArguments(QStringLiteral("--msg \"hello world\" --x 1")),
             QStringList({ QStringLiteral("--msg"), QStringLiteral("hello world"), QStringLiteral("--x"),
                           QStringLiteral("1") }));
}

void TstVersionModel::placeholderSubstitution()
{
    QMap<QString, QString> vars{ { QStringLiteral("a"), QStringLiteral("1") } };
    QStringList missing;
    QCOMPARE(substitutePlaceholders(QStringLiteral("x=${a} y=${nope} z"), vars, &missing), QStringLiteral("x=1 y= z"));
    QCOMPARE(missing, QStringList{ QStringLiteral("nope") });
    // Unterminated stays literal.
    QCOMPARE(substitutePlaceholders(QStringLiteral("a${b"), vars), QStringLiteral("a${b"));
}

void TstVersionModel::buildArgsRespectsRules()
{
    QList<ArgItem> items = ArgItem::listFromJson(QJsonDocument::fromJson(R"([
        "--always",
        {"value": "--win", "rules": [{"action": "allow", "os": {"name": "windows"}}]},
        {"value": ["--multi", "1"], "rules": [{"action": "allow"}]}
    ])").array());
    QMap<QString, QString> vars;
    QCOMPARE(buildArgs(items, linux64(), {}, vars), QStringList({ QStringLiteral("--always"), QStringLiteral("--multi"), QStringLiteral("1") }));
    QCOMPARE(buildArgs(items, win64(), {}, vars),
             QStringList({ QStringLiteral("--always"), QStringLiteral("--win"), QStringLiteral("--multi"),
                           QStringLiteral("1") }));
}

void TstVersionModel::mergeInheritsFrom()
{
    ParsedVersion base, over;
    base.id = QStringLiteral("1.20.4");
    base.mainClass = QStringLiteral("base.Main");
    base.javaMajor = 17;
    base.assetIndex.id = QStringLiteral("1.20");
    Library l;
    l.name = QStringLiteral("a:b:1");
    base.libraries.append(l);
    over.id = QStringLiteral("1.20.4-loader");
    over.inheritsFrom = QStringLiteral("1.20.4");
    over.gameArgs = ArgItem::listFromJson(QJsonDocument::fromJson(R"(["--extra"])").array());
    const ParsedVersion m = ParsedVersion::merge(base, over);
    QCOMPARE(m.id, QStringLiteral("1.20.4-loader"));
    QCOMPARE(m.mainClass, QStringLiteral("base.Main"));
    QCOMPARE(m.javaMajor, 17);
    QCOMPARE(m.libraries.size(), 1);
    QCOMPARE(m.gameArgs.size(), 1);
    QCOMPARE(m.assetIndex.id, QStringLiteral("1.20"));
}

void TstVersionModel::modernNativesPlanned()
{
    // Real 26.x shape: 4-part name, OS-gated, artifact download, extract.
    const Library lib = Library::fromJson(obj(R"({
        "name": "com.mojang:jtracy:1.14.38:natives-linux",
        "rules": [{"action": "allow", "os": {"name": "linux"}}],
        "downloads": {"artifact": {"path": "com/mojang/jtracy/1.14.38/jtracy-1.14.38-natives-linux.jar", "sha1": "d24", "size": 1, "url": "https://libraries.minecraft.net/x.jar"}},
        "extract": {"exclude": ["META-INF/"]}
    })"));
    QVERIFY(lib.isNativeJar());
    QCOMPARE(lib.nameClassifier(), QStringLiteral("natives-linux"));
    ParsedVersion v;
    v.id = QStringLiteral("x");
    v.libraries.append(lib);
    const GamePaths paths = GamePaths::fromDataDir(QStringLiteral("dummy"));
    const auto planWin = VersionInstaller::planLibraries(v, win64(), {}, paths);
    QVERIFY(planWin.natives.isEmpty()); // linux-gated: inactive on Windows
    QVERIFY(planWin.jarDownloads.isEmpty());
    const auto planLin = VersionInstaller::planLibraries(v, linux64(), {}, paths);
    QCOMPARE(planLin.natives.size(), 1);
    QCOMPARE(planLin.natives.first().classifier, QStringLiteral("natives-linux"));
    QVERIFY(planLin.jarDownloads.isEmpty()); // never on the classpath
}

void TstVersionModel::activeFeatureSet()
{
    QVERIFY(activeFeatures(false, false).isEmpty());
    QVERIFY(activeFeatures(true, false).contains(QStringLiteral("is_demo_user")));
    QVERIFY(activeFeatures(false, true).contains(QStringLiteral("has_custom_resolution")));
    QVERIFY(!activeFeatures(false, false).contains(QStringLiteral("has_quick_plays_support")));
}

QTEST_MAIN(TstVersionModel)
#include "tst_VersionModel.moc"
