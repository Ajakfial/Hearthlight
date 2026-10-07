// Tests: Kindling starter packs (built-ins + custom schema).
#include "Kindling.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TstKindling : public QObject {
    Q_OBJECT
private slots:
    void builtins();
    void schemaValid();
    void schemaInvalid();
    void customFiles();
};

void TstKindling::builtins()
{
    const auto packs = Kindling::builtinPacks();
    QVERIFY(packs.size() >= 4);
    bool plain = false, perf = false;
    for (const auto &p : packs) {
        QVERIFY(!p.id.isEmpty());
        QVERIFY(!p.title.isEmpty());
        QVERIFY(!p.loader.isEmpty());
        if (p.id == QStringLiteral("plain")) {
            plain = true;
            QVERIFY(p.slugs.isEmpty());
            QCOMPARE(p.loader, QStringLiteral("vanilla"));
        }
        if (p.id == QStringLiteral("performance")) {
            perf = true;
            QVERIFY(p.slugs.contains(QStringLiteral("sodium")));
        }
    }
    QVERIFY(plain && perf);
}

void TstKindling::schemaValid()
{
    StarterPack p;
    QString err;
    QJsonObject o;
    o[QStringLiteral("id")] = QStringLiteral("cozy");
    o[QStringLiteral("title")] = QStringLiteral("Cozy");
    o[QStringLiteral("summary")] = QStringLiteral("Warm.");
    o[QStringLiteral("loader")] = QStringLiteral("Fabric");
    QJsonArray slugs;
    slugs.append(QStringLiteral("sodium"));
    o[QStringLiteral("slugs")] = slugs;
    QVERIFY(Kindling::parsePackObject(o, &p, &err));
    QCOMPARE(p.loader, QStringLiteral("fabric")); // normalized
    QCOMPARE(p.slugs, QStringList{ QStringLiteral("sodium") });
}

void TstKindling::schemaInvalid()
{
    StarterPack p;
    QString err;
    QVERIFY(!Kindling::parsePackObject(QJsonObject{}, &p, &err));
    QVERIFY(!err.isEmpty());
    QJsonObject bad;
    bad[QStringLiteral("id")] = QStringLiteral("x");
    bad[QStringLiteral("title")] = QStringLiteral("X");
    bad[QStringLiteral("loader")] = QStringLiteral("bedrock");
    QVERIFY(!Kindling::parsePackObject(bad, &p, &err));
}

void TstKindling::customFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList skipped;
    QVERIFY(Kindling::loadCustomPacks(dir.path(), &skipped).isEmpty()); // no dir: fine
    QDir().mkpath(Kindling::kindlingDir(dir.path()));
    QFile good(QDir(Kindling::kindlingDir(dir.path())).filePath(QStringLiteral("mine.json")));
    QVERIFY(good.open(QIODevice::WriteOnly));
    good.write("{\"id\":\"mine\",\"title\":\"Mine\",\"loader\":\"forge\",\"slugs\":[\"jei\"]}");
    good.close();
    QFile bad(QDir(Kindling::kindlingDir(dir.path())).filePath(QStringLiteral("broken.json")));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{ not json");
    bad.close();
    const auto packs = Kindling::loadCustomPacks(dir.path(), &skipped);
    QCOMPARE(packs.size(), 1);
    QCOMPARE(packs.first().id, QStringLiteral("mine"));
    QCOMPARE(skipped.size(), 1);
}

QTEST_MAIN(TstKindling)
#include "tst_Kindling.moc"
