#pragma once

#include <QApplication>
#include <QString>

// Application bootstrap: portable detection, Logger + settings + theme +
// account store wiring, game services (Mojang metadata, installer, Java,
// launcher, Microsoft auth + secure tokens, instances, loaders),
// first-launch-offline safe. Owns all top-level objects.
class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    ~Application() override;

    int run();

    class AppSettings *settings() const { return m_settings; }
    class AccountStore *accounts() const { return m_accounts; }
    class Theme *theme() const { return m_theme; }

private:
    bool m_portable = false;
    QString m_dataDir;
    class AppSettings *m_settings = nullptr;
    class AccountStore *m_accounts = nullptr;
    class SecureTokenStore *m_tokens = nullptr;
    class MicrosoftAccountProvider *m_microsoft = nullptr;
    class Theme *m_theme = nullptr;
    class DownloadManager *m_downloads = nullptr;
    class MojangApi *m_mojang = nullptr;
    class JavaManager *m_java = nullptr;
    class VersionInstaller *m_installer = nullptr;
    class ModLoaderInstaller *m_loaders = nullptr;
    class InstanceManager *m_instances = nullptr;
    class ModrinthApi *m_modrinth = nullptr;
    class ModManager *m_mods = nullptr;
    class Embers *m_embers = nullptr;
    class Hearthstones *m_stones = nullptr;
    class Launcher *m_launcher = nullptr;
    class MainWindow *m_window = nullptr;
};
