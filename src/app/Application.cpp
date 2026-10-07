#include "Application.h"

#include "AccountProvider.h"
#include "AccountStore.h"
#include "AppSettings.h"
#include "DownloadManager.h"
#include "Embers.h"
#include "GamePaths.h"
#include "Hearthstones.h"
#include "InstanceManager.h"
#include "JavaManager.h"
#include "Launcher.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ModLoader.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "SecureTokenStore.h"
#include "Task.h"
#include "Theme.h"
#include "VersionInstaller.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTranslator>

namespace {
void installLanguage(const QString &dataDir, const QString &code)
{
    QString lang = code.trimmed().toLower();
    if (lang.isEmpty() || lang == QStringLiteral("system")) {
        lang = QLocale::system().name().toLower(); // e.g. "de_de"
    }
    if (lang.startsWith(QStringLiteral("en"))) {
        return; // built-in English: nothing to load
    }
    const QStringList tries = { lang, lang.left(lang.indexOf(QLatin1Char('_'))) };
    auto *translator = new QTranslator(qApp);
    for (const auto &l : tries) {
        if (l.isEmpty()) {
            continue;
        }
        const QString base = QStringLiteral("hearthlight_%1").arg(l);
        if (translator->load(base, QStringLiteral(":/i18n"))
            || translator->load(base, QDir(dataDir).filePath(QStringLiteral("translations")))) {
            qApp->installTranslator(translator);
            Logger::info(QStringLiteral("Loaded translation: %1").arg(base));
            return;
        }
    }
    Logger::info(QStringLiteral("No translation for “%1”; using built-in English.").arg(lang));
    translator->deleteLater();
}
} // namespace

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral("Hearthlight"));
    setOrganizationName(QStringLiteral("Hearthlight"));
    setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Hearthlight — Your home for every world."));
    QCommandLineOption portableOpt(QStringLiteral("portable"), QStringLiteral("Use portable data folder."));
    cli.addOption(portableOpt);
    QCommandLineOption dataOpt(QStringLiteral("data-dir"), QStringLiteral("Data folder to use."), QStringLiteral("dir"));
    cli.addOption(dataOpt);
    cli.process(*this);

    // Portable when flag is passed OR portable.txt sits next to the binary.
    m_portable = cli.isSet(portableOpt)
        || QFile::exists(QDir(applicationDirPath()).filePath(QStringLiteral("portable.txt")));
    if (cli.isSet(dataOpt)) {
        m_dataDir = cli.value(dataOpt);
    } else {
        // Check for a previously saved data dir before falling back.
        const QString probePortable = AppSettings::defaultDataDir(true);
        const QString probeNormal = AppSettings::defaultDataDir(false);
        const QString portableIni = AppSettings::settingsFilePath(probePortable);
        const QString normalIni = AppSettings::settingsFilePath(probeNormal);
        if (m_portable) {
            m_dataDir = probePortable;
        } else if (QFile::exists(normalIni)) {
            m_dataDir = probeNormal;
        } else if (QFile::exists(portableIni)) {
            m_dataDir = probePortable;
            m_portable = true;
        } else {
            m_dataDir = probeNormal;
        }
    }
    QDir().mkpath(m_dataDir);

    Logger::init(QDir(m_dataDir).filePath(QStringLiteral("logs")));
    Logger::info(QStringLiteral("Data dir: %1 (portable=%2)").arg(m_dataDir).arg(m_portable));

    m_settings = new AppSettings(AppSettings::settingsFilePath(m_dataDir), this);
    if (!cli.isSet(dataOpt) && !m_settings->dataDir().isEmpty() && m_settings->dataDir() != m_dataDir
        && QDir(m_settings->dataDir()).isAbsolute()) {
        // Respect a relocated data folder from an earlier run.
        m_dataDir = m_settings->dataDir();
        QDir().mkpath(m_dataDir);
    }

    NetworkStatus::instance().setMode(m_settings->offlineMode());
    installLanguage(m_dataDir, m_settings->language());
    QObject::connect(this, &QApplication::aboutToQuit, this, [this] { m_settings->sync(); });

    m_accounts = new AccountStore(m_dataDir, this);
    m_accounts->load();
    if (m_accounts->activeAccount().isValid()) {
        m_settings->setDefaultAccountId(m_accounts->activeAccountId());
    } else {
        const QString def = m_settings->defaultAccountId();
        if (!def.isEmpty() && m_accounts->hasAccount(def)) {
            m_accounts->setActiveAccountId(def);
        }
    }

    // Secure credential storage + Microsoft provider. Offline
    // accounts never touch the token store.
    m_tokens = new SecureTokenStore(m_dataDir, this);
    m_microsoft = new MicrosoftAccountProvider(m_dataDir, m_tokens, this);
    Logger::info(QStringLiteral("Credential backend: %1").arg(m_tokens->backendName()));

    m_theme = new Theme(this);
    m_theme->setAccent(QColor(m_settings->accentColor()));
    m_theme->setUiScale(m_settings->uiScale());
    m_theme->apply();
    QObject::connect(m_theme, &Theme::changed, m_theme, [this] { m_theme->apply(); });

    // Game services (all offline-safe at construction; network only inside tasks).
    const GamePaths paths = GamePaths::fromDataDir(m_dataDir);
    paths.ensureBaseDirs();
    m_downloads = new DownloadManager(paths.cacheDir, this);
    m_downloads->setMaxParallel(m_settings->downloadParallelism());
    m_mojang = new MojangApi(paths, m_downloads, this);
    m_java = new JavaManager(paths, m_downloads, this);
    m_installer = new VersionInstaller(paths, m_mojang, m_downloads, this);
    m_loaders = new ModLoaderInstaller(paths, m_downloads, this);
    m_instances = new InstanceManager(m_dataDir, this);
    m_instances->load();
    // No starter profile here: MainWindow runs Kindling (guided setup) on
    // first launch when no profiles exist, with a plain starter fallback.
    m_modrinth = new ModrinthApi(m_dataDir, m_downloads, this);
    m_mods = new ModManager(m_dataDir, this);
    m_embers = new Embers(m_dataDir, this);
    m_stones = new Hearthstones(m_dataDir, this);
    m_launcher = new Launcher(paths, m_java, this);
}

Application::~Application() = default;

int Application::run()
{
    const GamePaths paths = GamePaths::fromDataDir(m_dataDir);
    m_window = new MainWindow(m_settings, m_accounts, m_theme, paths, m_mojang, m_installer, m_java, m_launcher,
                              m_downloads, m_tokens, m_microsoft, m_instances, m_loaders, m_modrinth, m_mods,
                              m_embers, m_stones, m_dataDir);
    m_window->show();
    Logger::info(QStringLiteral("Hearthlight started (offline=%1)")
                     .arg(NetworkStatus::instance().isEffectivelyOffline()));
    // Background manifest refresh (cached list works meanwhile; silent offline).
    if (!NetworkStatus::instance().isEffectivelyOffline()) {
        Task *t = m_mojang->refreshManifestTask(this);
        // Fire and forget: failures just stay cached (logged by the task).
        t->start();
    }
    return exec();
}
