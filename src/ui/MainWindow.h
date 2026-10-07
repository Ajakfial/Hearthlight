#pragma once

#include <QMainWindow>

class AccountStore;
class AppSettings;
class DownloadManager;
class Embers;
class HearthPage;
class Hearthstones;
class DiscoverPage;
class InstanceManager;
class JavaManager;
class Launcher;
class MicrosoftAccountProvider;
class ModManager;
class ModLoaderInstaller;
class ModrinthApi;
class MojangApi;
class QStackedWidget;
class SecureTokenStore;
class Theme;
class VersionInstaller;
class VersionsPage;
class ProfilesPage;
struct GamePaths;
class PlayBar;
class AccountSwitcher;

// Main window shell: left sidebar (Hearth, Versions, Profiles, Discover,
// Accounts, Settings) + top account switcher + main content + persistent
// bottom Play bar.
//
// Launch pipelines:
//  - vanilla version play (Versions page)
//  - instance play (Profiles page: version + loader + Java, Microsoft silent
//    refresh before launch, Quick Play, pre/post commands, Hearthstone
//    auto-backup, mod safe-mode)
// Imports (.hearthpack/.mrpack/CurseForge) run as Tasks with progress.
// Discover (Modrinth), Hearth dashboard, Kindling first-run, Embers undo,
// Crash Doctor fixes, Campfire log.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AppSettings *settings, AccountStore *store, Theme *theme, const GamePaths &paths, MojangApi *api,
               VersionInstaller *installer, JavaManager *java, Launcher *launcher, DownloadManager *downloads,
               SecureTokenStore *tokens, MicrosoftAccountProvider *microsoft, InstanceManager *instances,
               ModLoaderInstaller *loaders, ModrinthApi *modrinth, ModManager *mods, Embers *embers,
               Hearthstones *stones, const QString &dataDir, QWidget *parent = nullptr);

private slots:
    void onPlay();
    void onPlayRequested(const QString &idOrInstall);
    void onInstancePlay(const QString &instanceId);
    void onVersionSelected(const QString &id);
    void refreshBar();
    void openLog();
    void openMods(const QString &instanceId);
    void openWorlds(const QString &instanceId);
    void runUndo(const QString &instanceId);
    void addJars(const QString &instanceId, const QStringList &jarPaths);
    void onModpackFile(const QString &mrpackPath);
    void maybeKindling();

private:
    void selectPage(int idx);
    void startPlay(const QString &versionId, bool installOnly);
    void startPlayInstance(const QString &instanceId, bool safeMode = false);
    void launchBuilt(const QString &versionId, bool demo);
    void launchInstanceBuilt(const struct Instance &in, const struct Account &account, const QString &mcToken,
                             bool safeMode = false);
    bool resolveInstanceAccount(const struct Instance &in, struct Account *out, QString *error);
    void importPack(const QString &kindAndPath);
    void handleCrash(const QString &gameDir, const QString &logText, int exitCode, const QString &titleSeed,
                     QWidget *logWindow, const struct Instance *inst);
    static void showErrorWithDetails(QWidget *parent, const QString &title, const QString &text,
                                     const QString &details);

    AppSettings *m_settings = nullptr;
    AccountStore *m_store = nullptr;
    Theme *m_theme = nullptr;
    GamePaths *m_paths = nullptr;
    MojangApi *m_api = nullptr;
    VersionInstaller *m_installer = nullptr;
    JavaManager *m_java = nullptr;
    Launcher *m_launcher = nullptr;
    DownloadManager *m_downloads = nullptr;
    SecureTokenStore *m_tokens = nullptr;
    MicrosoftAccountProvider *m_microsoft = nullptr;
    InstanceManager *m_instances = nullptr;
    ModLoaderInstaller *m_loaders = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    ModManager *m_mods = nullptr;
    Embers *m_embers = nullptr;
    Hearthstones *m_stones = nullptr;
    QString m_dataDir;
    QStackedWidget *m_stack = nullptr;
    QList<class QPushButton *> m_nav;
    PlayBar *m_playBar = nullptr;
    AccountSwitcher *m_switcher = nullptr;
    VersionsPage *m_versions = nullptr;
    ProfilesPage *m_profiles = nullptr;
    HearthPage *m_hearth = nullptr;
    DiscoverPage *m_discover = nullptr;
    QString m_currentVersion;
    QString m_currentInstance;
    class Task *m_activeTask = nullptr;
    bool m_kindlingShown = false;
};
