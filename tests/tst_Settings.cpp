// Tests: language setting round-trip (AppSettings).
#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TstSettings : public QObject {
    Q_OBJECT
private slots:
    void languageRoundTrip();
    void languageDiscovery();
};

void TstSettings::languageRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = QDir(dir.path()).filePath(QStringLiteral("hearthlight.ini"));
    AppSettings s(ini);
    QCOMPARE(s.language(), QStringLiteral("system")); // default follows the OS
    s.setLanguage(QStringLiteral("en"));
    QCOMPARE(s.language(), QStringLiteral("en"));
    AppSettings reloaded(ini); // persists across restarts
    QCOMPARE(reloaded.language(), QStringLiteral("en"));
    reloaded.setLanguage(QStringLiteral("  "));
    QCOMPARE(reloaded.language(), QStringLiteral("system")); // blank resets
}

void TstSettings::languageDiscovery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(AppSettings::availableLanguages(dir.path()).isEmpty()); // nothing yet
    QDir().mkpath(QDir(dir.path()).filePath(QStringLiteral("translations")));
    const auto touch = [&](const QString &name) {
        QFile f(QDir(dir.path()).filePath(QStringLiteral("translations/%1").arg(name)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("qm");
        f.close();
    };
    touch(QStringLiteral("hearthlight_de.qm"));
    touch(QStringLiteral("hearthlight_fr.qm"));
    touch(QStringLiteral("notes.txt")); // ignored: wrong pattern
    touch(QStringLiteral("hearthlight_en.qm")); // ignored: English is built in
    QCOMPARE(AppSettings::availableLanguages(dir.path()),
             QStringList({ QStringLiteral("de"), QStringLiteral("fr") }));
    QCOMPARE(AppSettings::displayNameForLanguage(QStringLiteral("de")), QStringLiteral("Deutsch"));
    QVERIFY(!AppSettings::displayNameForLanguage(QStringLiteral("xx")).isEmpty()); // unknown: code itself
}

QTEST_MAIN(TstSettings)
#include "tst_Settings.moc"
