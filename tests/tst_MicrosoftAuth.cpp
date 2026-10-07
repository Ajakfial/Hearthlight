// Tests: Microsoft auth JSON parsers + error mapping (no network).
#include "MicrosoftAuth.h"

#include <QJsonDocument>
#include <QTest>

static QJsonObject obj(const QByteArray &json)
{
    QJsonParseError e{};
    const QJsonDocument d = QJsonDocument::fromJson(json, &e);
    Q_ASSERT(e.error == QJsonParseError::NoError);
    return d.object();
}

class TstMicrosoftAuth : public QObject {
    Q_OBJECT
private slots:
    void deviceCodeParses();
    void tokenParses();
    void xblParses();
    void mcLoginParses();
    void profileParses();
    void xboxErrorMessages();
    void ownershipNeedsPurchase();
};

void TstMicrosoftAuth::deviceCodeParses()
{
    DeviceCodeInfo dc;
    const QByteArray good = "{\"device_code\":\"d\",\"user_code\":\"ABCD-1234\","
                            "\"verification_uri\":\"https://www.microsoft.com/link\","
                            "\"expires_in\":900,\"interval\":5}";
    const bool parsed = MicrosoftAuth::parseDeviceCode(obj(good), &dc);
    QVERIFY(parsed);
    QCOMPARE(dc.userCode, QStringLiteral("ABCD-1234"));
    QVERIFY(dc.ok);
    const QByteArray bad = "{\"error\":\"nope\"}";
    QVERIFY(!MicrosoftAuth::parseDeviceCode(obj(bad), nullptr));
}

void TstMicrosoftAuth::tokenParses()
{
    QString at, rf;
    qint64 exp = 0;
    QString err, det;
    const QByteArray good = "{\"access_token\":\"a\",\"refresh_token\":\"r\",\"expires_in\":3600}";
    QVERIFY(MicrosoftAuth::parseTokenResponse(obj(good), &at, &rf, &exp, &err, &det));
    QCOMPARE(at, QStringLiteral("a"));
    QCOMPARE(rf, QStringLiteral("r"));
    const QByteArray pending = "{\"error\":\"authorization_pending\"}";
    QVERIFY(!MicrosoftAuth::parseTokenResponse(obj(pending), &at, &rf, &exp, &err, &det));
    QCOMPARE(err, QStringLiteral("authorization_pending"));
}

void TstMicrosoftAuth::xblParses()
{
    QString tok, uhs, err, det;
    const QByteArray good = "{\"Token\":\"t\",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"123\"}]}}";
    QVERIFY(MicrosoftAuth::parseXblResponse(obj(good), &tok, &uhs, &err, &det));
    QCOMPARE(tok, QStringLiteral("t"));
    QCOMPARE(uhs, QStringLiteral("123"));
    const QByteArray bad = "{\"XErr\":\"2148916233\"}";
    QVERIFY(!MicrosoftAuth::parseXblResponse(obj(bad), &tok, &uhs, &err, &det));
}

void TstMicrosoftAuth::mcLoginParses()
{
    QString tok, err, det;
    qint64 exp = 0;
    const QByteArray good = "{\"access_token\":\"m\",\"expires_in\":86400}";
    QVERIFY(MicrosoftAuth::parseMinecraftLogin(obj(good), &tok, &exp, &err, &det));
    QCOMPARE(tok, QStringLiteral("m"));
    const QByteArray bad = "{\"error\":\"forbidden\"}";
    QVERIFY(!MicrosoftAuth::parseMinecraftLogin(obj(bad), &tok, &exp, &err, &det));
}

void TstMicrosoftAuth::profileParses()
{
    const QByteArray raw = "{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"Notch\","
                           "\"skins\":[{\"id\":\"s\",\"url\":\"https://x/skin.png\",\"variant\":\"classic\"}],"
                           "\"capes\":[{\"id\":\"c\",\"alias\":\"Minecon\"}]}";
    const MinecraftProfile p = MicrosoftAuth::parseMinecraftProfile(obj(raw));
    QVERIFY(p.ok);
    QCOMPARE(p.username, QStringLiteral("Notch"));
    QVERIFY(p.uuid.contains(QLatin1Char('-'))); // compact -> dashed
    QCOMPARE(p.skins.size(), 1);
    QCOMPARE(p.capes.size(), 1);
}

void TstMicrosoftAuth::xboxErrorMessages()
{
    // No Xbox profile.
    QJsonObject noXbox;
    noXbox[QStringLiteral("XErr")] = QStringLiteral("2148916233");
    const QString m1 = MicrosoftErrors::friendlyFor(QStringLiteral("xsts"), 401, noXbox, {});
    QVERIFY(m1.contains(QStringLiteral("xbox.com"), Qt::CaseInsensitive));
    // Child account.
    QJsonObject child;
    child[QStringLiteral("XErr")] = QStringLiteral("2148916238");
    QVERIFY(MicrosoftErrors::friendlyFor(QStringLiteral("xsts"), 401, child, {}).contains(QStringLiteral("family"),
                                                                                          Qt::CaseInsensitive));
    // Outage.
    QVERIFY(
        MicrosoftErrors::friendlyFor(QStringLiteral("xsts"), 500, {}, {}).contains(QStringLiteral("problems")));
}

void TstMicrosoftAuth::ownershipNeedsPurchase()
{
    const QString m = MicrosoftErrors::friendlyFor(QStringLiteral("ownership"), 404, {}, {});
    QVERIFY(m.contains(QStringLiteral("purchase"), Qt::CaseInsensitive));
}

QTEST_MAIN(TstMicrosoftAuth)
#include "tst_MicrosoftAuth.moc"
