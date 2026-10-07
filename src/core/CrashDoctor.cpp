#include "CrashDoctor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace CrashDoctor {

QString latestCrashReport(const QString &gameDir, QString *fileName)
{
    QDir d(QDir(gameDir).filePath(QStringLiteral("crash-reports")));
    if (!d.exists()) {
        return {};
    }
    QString newest;
    QDateTime newestMt;
    for (const auto &fi : d.entryList({ QStringLiteral("*.txt") }, QDir::Files, QDir::Time)) {
        const QDateTime mt = QFileInfo(d.filePath(fi)).lastModified();
        if (newest.isEmpty() || mt > newestMt) {
            newest = fi;
            newestMt = mt;
        }
    }
    if (newest.isEmpty()) {
        return {};
    }
    if (fileName) {
        *fileName = newest;
    }
    QFile f(d.filePath(newest));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll().left(512 * 1024));
}

int suggestedMemoryForOom(int currentMb, qint64 systemRamMb)
{
    const int quarter = (int)qBound(qint64(1024), systemRamMb / 4, qint64(8192));
    // Step up: current+1GB, at least quarter-of-RAM, capped at 8GB.
    return qBound(quarter, currentMb + 1024, 8192);
}

CrashDiagnosis diagnose(const QString &logText, const QString &crashReport, int exitCode)
{
    const QString hay = (crashReport + QLatin1Char('\n') + logText);
    const QString low = hay.toLower();
    CrashDiagnosis d;

    auto has = [&](const char *needle) { return low.contains(QString::fromLatin1(needle)); };

    // 1. Out of memory.
    if (low.contains(QStringLiteral("outofmemoryerror")) || low.contains(QStringLiteral("out of memory"))
        || low.contains(QStringLiteral("memory allocation failed"))
        || (low.contains(QStringLiteral("java heap space")))) {
        d.title = QObject::tr("The game ran out of memory");
        d.explanation = QObject::tr(
            "Minecraft needed more memory than it was allowed. This is common with mods or large worlds — "
            "nothing is broken. Giving the profile more memory usually fixes it right away.");
        d.fixes = { { QObject::tr("Give it more memory"), QStringLiteral("raise-memory") },
                    { QObject::tr("Launch without mods once"), QStringLiteral("safe-mode") } };
        return d;
    }
    // 2. Mixin / mod loading conflicts.
    if (has("mixin") || has("modloadingexception") || low.contains(QStringLiteral("loader could not be applied"))
        || low.contains(QStringLiteral("refmap")) || has("classnotfoundexception") || has("noclassdeffounderror")
        || low.contains(QStringLiteral("mod resolution failed"))) {
        d.title = QObject::tr("Two mods are fighting each other");
        d.explanation = QObject::tr(
            "The game's mod loader tripped while starting up — usually two mods changing the same thing, or a "
            "mod built for a different Minecraft version. Your worlds are safe; the game never got far enough "
            "to touch them.");
        d.fixes = { { QObject::tr("Launch without mods once"), QStringLiteral("safe-mode") },
                    { QObject::tr("Open the mods folder"), QStringLiteral("open-mods") } };
        return d;
    }
    // 3. Missing dependency.
    if (low.contains(QStringLiteral("missing mods")) || low.contains(QStringLiteral("which is missing"))
        || low.contains(QStringLiteral("dependency")) || has("modnotfound")) {
        d.title = QObject::tr("A mod is missing something it needs");
        d.explanation = QObject::tr(
            "One of your mods needs another mod (a \"library\" mod) that isn't installed. Installing the missing "
            "piece from the Discover page usually fixes it — the log names it near the bottom.");
        d.fixes = { { QObject::tr("Open the mods folder"), QStringLiteral("open-mods") } };
        return d;
    }
    // 4. Wrong Java.
    if (has("unsupportedclassversionerror") || low.contains(QStringLiteral("class file version"))
        || low.contains(QStringLiteral("java.lang.unsupported")) || low.contains(QStringLiteral("bad major version"))) {
        d.title = QObject::tr("The wrong Java ran the game");
        d.explanation = QObject::tr(
            "This Minecraft version needs a newer (or older) Java than the one that launched it. Hearthlight "
            "usually picks this itself — check the profile's Java setting or let it download the suggested runtime.");
        d.fixes = { { QObject::tr("Open Java settings"), QStringLiteral("open-java") } };
        return d;
    }
    // 5. Graphics / drivers.
    if (low.contains(QStringLiteral("pixel format not accelerated")) || low.contains(QStringLiteral("glfw error"))
        || low.contains(QStringLiteral("opengl")) || low.contains(QStringLiteral("failed to create the framebuffer"))
        || has("org.lwjgl.glfw")) {
        d.title = QObject::tr("Your graphics ran into trouble");
        d.explanation = QObject::tr(
            "The game couldn't start its graphics (this looks like a driver or graphics-chip issue, not a broken "
            "world). Updating your graphics drivers helps most of the time; on very old hardware, try smaller "
            "render settings once you're in.");
        d.fixes = {};
        return d;
    }
    // 6. Session / auth.
    if (low.contains(QStringLiteral("failed to verify username")) || low.contains(QStringLiteral("invalid session"))
        || low.contains(QStringLiteral("authentication servers are down"))) {
        d.title = QObject::tr("The sign-in didn't go through");
        d.explanation = QObject::tr(
            "The game couldn't confirm your account with the official servers. If you use a Microsoft account, "
            "refresh it on the Accounts page and try again. Offline profiles can't join authenticated servers — "
            "that's expected, not a bug.");
        d.fixes = { { QObject::tr("Refresh Microsoft sign-in"), QStringLiteral("resign-microsoft") } };
        return d;
    }
    // 7. Native / lwjgl crash (EXCEPTION_ACCESS_VIOLATION etc.).
    if (has("exception_access_violation") || has("#  native frames") || low.contains(QStringLiteral("fatal error"))
        || low.contains(QStringLiteral("problematic frame"))) {
        d.title = QObject::tr("The game crashed deep inside its engine");
        d.explanation = QObject::tr(
            "This crash happened in native code (graphics, sound, or Java itself) rather than in a mod. The crash "
            "report has the exact spot — often it's drivers, an overlay app, or running out of memory. Try safe "
            "mode first to rule mods out.");
        d.fixes = { { QObject::tr("Launch without mods once"), QStringLiteral("safe-mode") },
                    { QObject::tr("Open the crash report"), QStringLiteral("open-crash") } };
        return d;
    }
    // 8. We have a crash report but no pattern matched.
    if (!crashReport.trimmed().isEmpty()) {
        d.title = QObject::tr("The game crashed (exit code %1)").arg(exitCode);
        d.explanation = QObject::tr(
            "Minecraft closed unexpectedly and left a crash report. Hearthlight didn't recognize this exact crash, "
            "but the report's top lines usually name the culprit — open it and look for the first few lines under "
            "\"Description\".");
        d.fixes = { { QObject::tr("Open the crash report"), QStringLiteral("open-crash") },
                    { QObject::tr("Launch without mods once"), QStringLiteral("safe-mode") } };
        return d;
    }
    // 9. No report at all.
    d.title = QObject::tr("The game exited (code %1)").arg(exitCode);
    d.explanation = QObject::tr(
        "Minecraft closed without writing a crash report, so there's no single culprit to point at. The game log "
        "above usually shows the last thing it tried — look at the final lines for red errors.");
    d.fixes = {};
    return d;
}

} // namespace CrashDoctor
