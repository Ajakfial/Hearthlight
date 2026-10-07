// Tests: Java version parsing/mapping, memory suggestion, OS tokens,
// and launch-token rules (offline "0", Microsoft honestly blocked).
#include "JavaManager.h"
#include "Launcher.h"

#include <QTest>

class TstJavaManager : public QObject {
    Q_OBJECT
private slots:
    void parseMajor();
    void requiredMajor();
    void memorySuggestion();
    void osArchTokens();
    void offlineToken();
    void microsoftBlocked();
};

void TstJavaManager::parseMajor()
{
    QCOMPARE(JavaManager::parseMajor("openjdk version \"17.0.9\" 2023-10-17"), 17);
    QCOMPARE(JavaManager::parseMajor("openjdk version \"21\" 2023-09-19"), 21);
    QCOMPARE(JavaManager::parseMajor("java version \"1.8.0_392\""), 8);
    QCOMPARE(JavaManager::parseMajor("openjdk version \"21-ea\" 2023-09-19"), 21);
    QCOMPARE(JavaManager::parseMajor("garbage"), 0);
}

void TstJavaManager::requiredMajor()
{
    QCOMPARE(JavaManager::requiredMajorFor(17), 17);
    QCOMPARE(JavaManager::requiredMajorFor(21), 21);
    QCOMPARE(JavaManager::requiredMajorFor(0), 8); // legacy versions
}

void TstJavaManager::memorySuggestion()
{
    // Vanilla on 8GB -> 2048; quarter of RAM clamped + mod allowance.
    QCOMPARE(JavaManager::suggestMemoryMb(8192, 0), 2048);
    QCOMPARE(JavaManager::suggestMemoryMb(2048, 0), 1024); // floor
    QCOMPARE(JavaManager::suggestMemoryMb(65536, 0), 8192); // ceiling
    QVERIFY(JavaManager::suggestMemoryMb(8192, 100) > 2048); // mods add some
    QVERIFY(JavaManager::isRecommendedMemory(2048, 8192, 0));
    QVERIFY(!JavaManager::isRecommendedMemory(512, 8192, 0));
    // Friendly 256MB steps.
    QCOMPARE(JavaManager::suggestMemoryMb(8192, 0) % 256, 0);
}

void TstJavaManager::osArchTokens()
{
    const QString os = JavaManager::adoptiumOs();
    QVERIFY(os == QStringLiteral("windows") || os == QStringLiteral("linux") || os == QStringLiteral("mac"));
    const QString arch = JavaManager::adoptiumArch();
    QVERIFY(arch == QStringLiteral("x64") || arch == QStringLiteral("aarch64") || arch == QStringLiteral("x86"));
}

void TstJavaManager::offlineToken()
{
    Account a;
    a.type = AccountType::Offline;
    a.username = QStringLiteral("River");
    a.uuid = offlineUuidForUsername(a.username);
    a.id = a.uuid.toString(QUuid::WithoutBraces).toLower();
    QString err;
    QCOMPARE(Launcher::resolveAccessToken(a, &err), QStringLiteral("0"));
    QCOMPARE(a.userTypeString(), QStringLiteral("legacy"));
}

void TstJavaManager::microsoftBlocked()
{
    Account m;
    m.type = AccountType::Microsoft;
    m.username = QStringLiteral("PlayerName");
    m.uuid = QUuid::createUuid();
    m.id = m.uuid.toString(QUuid::WithoutBraces).toLower();
    QString err;
    QVERIFY(Launcher::resolveAccessToken(m, &err).isEmpty());
    QVERIFY(!err.isEmpty()); // honest message, never a fake token
}

QTEST_MAIN(TstJavaManager)
#include "tst_JavaManager.moc"
