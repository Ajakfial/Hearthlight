// Tests: offline-mode setting and effective-offline behavior.
#include "NetworkStatus.h"

#include <QTest>

class TstOfflineMode : public QObject {
    Q_OBJECT
private slots:
    void strings();
    void effectiveOffline();
};

void TstOfflineMode::strings()
{
    QCOMPARE(offlineModeToString(OfflineMode::Automatic), QStringLiteral("automatic"));
    QCOMPARE(offlineModeToString(OfflineMode::AlwaysOffline), QStringLiteral("always-offline"));
    QCOMPARE(offlineModeToString(OfflineMode::NeverOffline), QStringLiteral("never-offline"));
    QCOMPARE(offlineModeFromString(QStringLiteral("always")), OfflineMode::AlwaysOffline);
    QCOMPARE(offlineModeFromString(QStringLiteral("never")), OfflineMode::NeverOffline);
    QCOMPARE(offlineModeFromString(QStringLiteral("auto")), OfflineMode::Automatic);
    QCOMPARE(offlineModeFromString(QStringLiteral("")), OfflineMode::Automatic);
    bool ok = true;
    QCOMPARE(offlineModeFromString(QStringLiteral("bogus"), &ok), OfflineMode::Automatic);
    QVERIFY(!ok);
}

void TstOfflineMode::effectiveOffline()
{
    NetworkStatus &net = NetworkStatus::instance();
    const OfflineMode saved = net.mode();
    net.setMode(OfflineMode::AlwaysOffline);
    QVERIFY(net.isEffectivelyOffline());
    QVERIFY(!net.isOnline());
    net.setMode(OfflineMode::NeverOffline);
    QVERIFY(!net.isEffectivelyOffline());
    QVERIFY(net.isOnline());
    net.setMode(saved); // leave the global exactly as found
}

QTEST_MAIN(TstOfflineMode)
#include "tst_OfflineMode.moc"
