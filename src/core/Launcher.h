#pragma once

#include "Account.h"
#include "GamePaths.h"
#include "VersionModel.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

class JavaManager;

struct LaunchOptions {
    Account account;
    QString versionId;
    QString javaPath; // custom java (empty = auto-detect/managed)
    int memoryMb = 0; // 0 = suggested for this machine
    QString extraJvmArgs; // raw string, quote-aware split
    bool demo = false; // official --demo flag (limited title-screen demo)
    int width = 0;
    int height = 0; // 0 = game default
    bool fullscreen = false;
    QString gameDirOverride; // empty = per-instance or gamedir/<versionId>
    QString clientId; // persistent launcher id for ${clientid}
    QString minecraftToken; // Microsoft accounts: fresh token from provider (memory only)
    // Quick Play: jump straight into a world/server/realm.
    QString quickPlayWorld; // single-player world name
    QString quickPlayServer; // server address
    QString quickPlayRealm; // realm id
    QStringList preLaunchCommand; // already split argv, run before game
    QStringList postExitCommand; // already split argv, run after game exits
    QMap<QString, QString> extraEnv; // extra environment variables
};

struct BuiltLaunch {
    QString javaExe;
    QStringList jvmArgs; // everything before the main class
    QString mainClass;
    QStringList gameArgs; // everything after the main class
    QString workDir;
    QString classpath;
    QString versionName;
};

// Vanilla (+ loader-merged) launch pipeline (spec section 13).
// Building is pure validation + string work (offline-safe, no network);
// spawning happens on the GUI thread via QProcess with live log streaming
// owned by the caller (GameLogDialog).
class Launcher : public QObject {
    Q_OBJECT
public:
    using MicrosoftTokenResolver = std::function<QString(const Account &, QString *error, QString *details)>;

    Launcher(const GamePaths &paths, JavaManager *java, QObject *parent = nullptr);

    void setMicrosoftTokenResolver(MicrosoftTokenResolver r) { m_msResolver = std::move(r); }

    // Offline accounts -> local "0" placeholder (never a Microsoft token).
    // Microsoft accounts -> resolver token when set, else honest error.
    // Kept static for tests: without a resolver, Microsoft is honestly blocked.
    static QString resolveAccessToken(const Account &account, QString *error);

    QString resolveToken(const Account &account, const LaunchOptions &opts, QString *error,
                         QString *details = nullptr) const;

    // Validate cached files and build the full command. No network.
    bool build(const ParsedVersion &merged, const LaunchOptions &opts, BuiltLaunch *out, QString *error) const;

    // Spawn the game. Returns a running QProcess (parented here) or nullptr.
    QProcess *spawn(const BuiltLaunch &built, const LaunchOptions &opts, QString *error);

    // Redacted one-line command for logs (tokens never printed).
    static QString redactedCommand(const BuiltLaunch &built);

    void recordPlaytime(const QString &versionId, qint64 seconds) const;
    static qint64 playtimeSeconds(const GamePaths &paths, const QString &versionId);

private:
    GamePaths m_paths;
    JavaManager *m_java = nullptr;
    MicrosoftTokenResolver m_msResolver;
};
