// Tests: offline UUID generation (deterministic, Java-compatible v3),
// username validation, and launch-identity helpers.
#include "Account.h"

#include <QCryptographicHash>
#include <QTest>

class TstOfflineUuid : public QObject {
    Q_OBJECT
private slots:
    void deterministic();
    void versionAndVariantBits();
    void differentNamesDiffer();
    void caseSensitive();
    void knownAlgorithm();
    void usernameValidation();
    void launchIdentity();
};

void TstOfflineUuid::deterministic()
{
    QCOMPARE(offlineUuidForUsername(QStringLiteral("River")), offlineUuidForUsername(QStringLiteral("River")));
}

void TstOfflineUuid::versionAndVariantBits()
{
    for (const auto &n : { QStringLiteral("Steve"), QStringLiteral("Alex"), QStringLiteral("River_123") }) {
        const QUuid u = offlineUuidForUsername(n);
        QVERIFY(!u.isNull());
        // Version nibble (byte 6 high bits) must be 3 (MD5 name-based).
        const QByteArray r = u.toRfc4122();
        QCOMPARE((r[6] >> 4) & 0x0f, 3);
        // Variant bits (byte 8 top two bits) must be 0b10 (RFC 4122).
        QCOMPARE((r[8] >> 6) & 0x03, 2);
    }
}

void TstOfflineUuid::differentNamesDiffer()
{
    QVERIFY(offlineUuidForUsername(QStringLiteral("Steve")) != offlineUuidForUsername(QStringLiteral("Alex")));
}

void TstOfflineUuid::caseSensitive()
{
    // Game usernames are case-sensitive for UUID purposes; only the store's
    // duplicate check is case-insensitive.
    QVERIFY(offlineUuidForUsername(QStringLiteral("Steve")) != offlineUuidForUsername(QStringLiteral("steve")));
}

void TstOfflineUuid::knownAlgorithm()
{
    // Recompute the documented algorithm independently: MD5("OfflinePlayer:"+name)
    // with version/variant bits forced, and compare.
    const QString name = QStringLiteral("Notch");
    const QByteArray raw = QCryptographicHash::hash(
        (QStringLiteral("OfflinePlayer:") + name).toUtf8(), QCryptographicHash::Md5);
    QByteArray fixed = raw;
    fixed[6] = static_cast<char>((fixed[6] & 0x0f) | 0x30);
    fixed[8] = static_cast<char>((fixed[8] & 0x3f) | 0x80);
    QCOMPARE(offlineUuidForUsername(name), QUuid::fromRfc4122(fixed));
}

void TstOfflineUuid::usernameValidation()
{
    QVERIFY(isValidMinecraftUsername(QStringLiteral("Steve")));
    QVERIFY(isValidMinecraftUsername(QStringLiteral("a_1")));
    QVERIFY(isValidMinecraftUsername(QStringLiteral("1234567890123456")));
    QVERIFY(!isValidMinecraftUsername(QStringLiteral("ab"))); // too short
    QVERIFY(!isValidMinecraftUsername(QStringLiteral("12345678901234567"))); // too long
    QVERIFY(!isValidMinecraftUsername(QStringLiteral("has space")));
    QVERIFY(!isValidMinecraftUsername(QStringLiteral("hy-phen")));
    QVERIFY(!isValidMinecraftUsername(QString()));
}

void TstOfflineUuid::launchIdentity()
{
    Account a;
    a.type = AccountType::Offline;
    a.username = QStringLiteral("River");
    a.uuid = offlineUuidForUsername(a.username);
    QCOMPARE(a.userTypeString(), QStringLiteral("legacy"));
    QCOMPARE(a.uuidCompact().size(), 32);
    QVERIFY(!a.uuidCompact().contains(QLatin1Char('-')));
    // Placeholder token must obviously not look like a Microsoft JWT.
    QCOMPARE(a.offlineAccessTokenPlaceholder(), QStringLiteral("0"));

    Account m;
    m.type = AccountType::Microsoft;
    QCOMPARE(m.userTypeString(), QStringLiteral("msa"));
}

QTEST_MAIN(TstOfflineUuid)
#include "tst_OfflineUuid.moc"
