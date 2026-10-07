// Tests: Crash Doctor pattern matching (offline fixtures).
#include "CrashDoctor.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TstCrashDoctor : public QObject {
    Q_OBJECT
private slots:
    void outOfMemory();
    void modConflict();
    void missingDep();
    void wrongJava();
    void graphics();
    void session();
    void nativeCrash();
    void unknownWithReport();
    void unknownWithoutReport();
    void latestReport();
    void oomSuggestion();
};

void TstCrashDoctor::outOfMemory()
{
    const auto d = CrashDoctor::diagnose(QStringLiteral("java.lang.OutOfMemoryError: Java heap space"), {}, 1);
    QVERIFY(d.title.contains(QStringLiteral("memory"), Qt::CaseInsensitive));
    QVERIFY(!d.fixes.isEmpty());
    QCOMPARE(d.fixes.first().action, QStringLiteral("raise-memory"));
}

void TstCrashDoctor::modConflict()
{
    const auto d = CrashDoctor::diagnose(
        QStringLiteral("net.fabricmc.loader.impl.FormattedException: Some exception was thrown! Caused by Mixin apply failed"), {}, 1);
    QVERIFY(d.title.contains(QStringLiteral("mods"), Qt::CaseInsensitive));
    bool safe = false;
    for (const auto &f : d.fixes) {
        safe = safe || f.action == QStringLiteral("safe-mode");
    }
    QVERIFY(safe);
}

void TstCrashDoctor::missingDep()
{
    const auto d = CrashDoctor::diagnose(QStringLiteral("Missing Mods: architectury which is missing!"), {}, 1);
    QVERIFY(d.explanation.contains(QStringLiteral("needs"), Qt::CaseInsensitive)
            || d.title.contains(QStringLiteral("missing"), Qt::CaseInsensitive));
}

void TstCrashDoctor::wrongJava()
{
    const auto d = CrashDoctor::diagnose(
        QStringLiteral("java.lang.UnsupportedClassVersionError: net/minecraft/client/main/Main has been compiled by a more recent version"), {}, 1);
    QVERIFY(d.title.contains(QStringLiteral("Java")));
    QCOMPARE(d.fixes.first().action, QStringLiteral("open-java"));
}

void TstCrashDoctor::graphics()
{
    const auto d =
        CrashDoctor::diagnose(QStringLiteral("GLFW error 65542: WGL: The driver does not appear to support OpenGL"), {}, 1);
    QVERIFY(d.explanation.contains(QStringLiteral("driver"), Qt::CaseInsensitive));
}

void TstCrashDoctor::session()
{
    const auto d =
        CrashDoctor::diagnose(QStringLiteral("Failed to verify username! Invalid session (Try restarting your game)"), {}, 1);
    QVERIFY(!d.fixes.isEmpty());
    QCOMPARE(d.fixes.first().action, QStringLiteral("resign-microsoft"));
}

void TstCrashDoctor::nativeCrash()
{
    const auto d = CrashDoctor::diagnose(QStringLiteral("# EXCEPTION_ACCESS_VIOLATION (0xc0000005) at pc=0x123\n# Problematic frame:\n#  native frames here"), {}, -1073741819);
    QVERIFY(d.title.contains(QStringLiteral("engine"), Qt::CaseInsensitive)
            || d.explanation.contains(QStringLiteral("native"), Qt::CaseInsensitive));
}

void TstCrashDoctor::unknownWithReport()
{
    const auto d = CrashDoctor::diagnose(QStringLiteral("something odd"), QStringLiteral("-- Head --\nDescription: Rendering overlay"), 1);
    QVERIFY(!d.explanation.isEmpty());
    bool open = false;
    for (const auto &f : d.fixes) {
        open = open || f.action == QStringLiteral("open-crash");
    }
    QVERIFY(open);
}

void TstCrashDoctor::unknownWithoutReport()
{
    const auto d = CrashDoctor::diagnose(QStringLiteral("bye"), {}, 42);
    QVERIFY(d.title.contains(QStringLiteral("42")));
    // Never empty: always something useful.
    QVERIFY(!d.explanation.isEmpty());
}

void TstCrashDoctor::latestReport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(CrashDoctor::latestCrashReport(dir.path()).isEmpty()); // no dir
    QDir().mkpath(QDir(dir.path()).filePath(QStringLiteral("crash-reports")));
    QString name;
    QVERIFY(CrashDoctor::latestCrashReport(dir.path(), &name).isEmpty()); // no files
    QFile f(QDir(dir.path()).filePath(QStringLiteral("crash-reports/crash-1.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("report-body");
    f.close();
    QCOMPARE(CrashDoctor::latestCrashReport(dir.path(), &name), QStringLiteral("report-body"));
    QCOMPARE(name, QStringLiteral("crash-1.txt"));
}

void TstCrashDoctor::oomSuggestion()
{
    // Steps up from current, floors at quarter-RAM, caps at 8GB.
    QCOMPARE(CrashDoctor::suggestedMemoryForOom(2048, 16384), 4096);
    QVERIFY(CrashDoctor::suggestedMemoryForOom(512, 16384) >= 1024);
    QCOMPARE(CrashDoctor::suggestedMemoryForOom(8000, 65536), 8192);
    // Already generous: still steps up by 1GB.
    QCOMPARE(CrashDoctor::suggestedMemoryForOom(4096, 16384), 5120);
}

QTEST_MAIN(TstCrashDoctor)
#include "tst_CrashDoctor.moc"
