// Tests: secure token store round-trip (OS backend or restricted file).
#include "SecureTokenStore.h"

#include <QTemporaryDir>
#include <QTest>

class TstSecureStore : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void isolatesAccounts();
    void backendNamed();
};

void TstSecureStore::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SecureTokenStore store(dir.path());
    QVERIFY(store.setToken(QStringLiteral("acc1"), QStringLiteral("msRefresh"), QStringLiteral("secret-123")));
    QCOMPARE(store.token(QStringLiteral("acc1"), QStringLiteral("msRefresh")), QStringLiteral("secret-123"));
    QVERIFY(store.clearToken(QStringLiteral("acc1"), QStringLiteral("msRefresh")));
    QVERIFY(store.token(QStringLiteral("acc1"), QStringLiteral("msRefresh")).isEmpty());
}

void TstSecureStore::isolatesAccounts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SecureTokenStore store(dir.path());
    QVERIFY(store.setToken(QStringLiteral("a"), QStringLiteral("msRefresh"), QStringLiteral("one")));
    QVERIFY(store.setToken(QStringLiteral("b"), QStringLiteral("msRefresh"), QStringLiteral("two")));
    QCOMPARE(store.token(QStringLiteral("a"), QStringLiteral("msRefresh")), QStringLiteral("one"));
    QVERIFY(store.clearAccount(QStringLiteral("a")));
    QVERIFY(store.token(QStringLiteral("a"), QStringLiteral("msRefresh")).isEmpty());
    QCOMPARE(store.token(QStringLiteral("b"), QStringLiteral("msRefresh")), QStringLiteral("two"));
}

void TstSecureStore::backendNamed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SecureTokenStore store(dir.path());
    QVERIFY(!store.backendName().isEmpty());
}

QTEST_MAIN(TstSecureStore)
#include "tst_SecureStore.moc"
