#pragma once

#include <QList>
#include <QString>

// Crash Doctor (spec 10.4): parses the latest crash report / game log and
// explains it in plain English with one-click fixes. Pure parsing, no I/O
// beyond the caller-provided texts — fully unit-tested.
struct CrashFix {
    QString label; // button text, e.g. "Launch without mods once"
    QString action; // safe-mode | raise-memory | open-mods | open-crash |
                    // open-java | resign-microsoft | none
};

struct CrashDiagnosis {
    QString title; // calm headline
    QString explanation; // plain-English cause + what to do
    QList<CrashFix> fixes;
    bool crashed = true;
};

namespace CrashDoctor {
// Diagnose from log text + optional crash-report text. exitCode is the game
// process exit code. Never returns empty: unknown crashes get a generic but
// useful diagnosis (open the report, check the log).
CrashDiagnosis diagnose(const QString &logText, const QString &crashReport, int exitCode);

// Read the newest crash report in <gameDir>/crash-reports (empty if none).
QString latestCrashReport(const QString &gameDir, QString *fileName = nullptr);

// Suggested memory (MB) for the out-of-memory one-click fix.
int suggestedMemoryForOom(int currentMb, qint64 systemRamMb);
} // namespace CrashDoctor
