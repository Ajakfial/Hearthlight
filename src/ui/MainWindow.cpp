#include "MainWindow.h"

#include "AccountsPage.h"
#include "AppSettings.h"
#include "CrashDoctor.h"
#include "DiscoverPage.h"
#include "Embers.h"
#include "GameLogDialog.h"
#include "GamePaths.h"
#include "HearthPage.h"
#include "Hearthstones.h"
#include "IconProvider.h"
#include "InstanceManager.h"
#include "JavaManager.h"
#include "Kindling.h"
#include "Launcher.h"
#include "LogViewerDialog.h"
#include "MicrosoftAuth.h"
#include "ModLoader.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "ProfilesPage.h"
#include "SecureTokenStore.h"
#include "SettingsPage.h"
#include "Task.h"
#include "Theme.h"
#include "VersionInstaller.h"
#include "VersionsPage.h"
#include "dialogs/InstalledModsDialog.h"
#include "dialogs/KindlingDialog.h"
#include "dialogs/TaskProgressDialog.h"
#include "dialogs/WorldsDialog.h"
#include "widgets/AccountSwitcher.h"
#include "widgets/PlayBar.h"

#include "AccountProvider.h"
#include "AccountStore.h"
#include "Constants.h"
#include "DownloadManager.h"
#include "Logger.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(AppSettings *settings, AccountStore *store, Theme *theme, const GamePaths &paths,
                       MojangApi *api, VersionInstaller *installer, JavaManager *java, Launcher *launcher,
                       DownloadManager *downloads, SecureTokenStore *tokens, MicrosoftAccountProvider *microsoft,
                       InstanceManager *instances, ModLoaderInstaller *loaders, ModrinthApi *modrinth,
                       ModManager *mods, Embers *embers, Hearthstones *stones, const QString &dataDir,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_store(store)
    , m_theme(theme)
    , m_paths(new GamePaths(paths))
    , m_api(api)
    , m_installer(installer)
    , m_java(java)
    , m_launcher(launcher)
    , m_downloads(downloads)
    , m_tokens(tokens)
    , m_microsoft(microsoft)
    , m_instances(instances)
    , m_loaders(loaders)
    , m_modrinth(modrinth)
    , m_mods(mods)
    , m_embers(embers)
    , m_stones(stones)
    , m_dataDir(dataDir)
{
    setWindowTitle(tr("Hearthlight — Your home for every world"));
    resize(1180, 760);
    setMinimumSize(900, 600);

    // Microsoft tokens resolve from the in-memory session cache; the prepare
    // pipeline refreshes them on a worker thread before building the launch.
    m_launcher->setMicrosoftTokenResolver([this](const Account &a, QString *err, QString *det) -> QString {
        if (!m_microsoft) {
            return Launcher::resolveAccessToken(a, err);
        }
        const QString cached = m_microsoft->cachedMinecraftToken(a.id);
        if (!cached.isEmpty()) {
            return cached;
        }
        // GUI thread: cannot block on network here. The prepare Task refreshes
        // first; reaching here means the refresh didn't happen (offline with
        // expired session, or never signed in).
        if (err) {
            *err = tr("This Microsoft account needs a fresh sign-in. Press Play again while online, "
                      "or use Accounts → Refresh.");
        }
        if (det) {
            *det = {};
        }
        return {};
    });

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralPage"));
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- top bar ----
    auto *top = new QWidget(central);
    top->setObjectName(QStringLiteral("topbar"));
    auto *topLay = new QHBoxLayout(top);
    topLay->setContentsMargins(16, 8, 16, 8);
    auto *logo = new QLabel(top);
    logo->setPixmap(IconProvider::instance().pixmap(QStringLiteral("logo"), 28, theme->accent()));
    topLay->addWidget(logo);
    auto *name = new QLabel(tr("Hearthlight"), top);
    QFont nf = name->font();
    nf.setBold(true);
    nf.setPointSize(13);
    name->setFont(nf);
    topLay->addWidget(name);
    topLay->addStretch(1);
    m_switcher = new AccountSwitcher(store, dataDir, top);
    topLay->addWidget(m_switcher);
    auto *logBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("log")), tr("Log"), top);
    logBtn->setToolTip(tr("Open the in-app log viewer"));
    connect(logBtn, &QPushButton::clicked, this, &MainWindow::openLog);
    topLay->addWidget(logBtn);
    root->addWidget(top);

    // ---- middle: sidebar + pages ----
    auto *mid = new QWidget(central);
    auto *midLay = new QHBoxLayout(mid);
    midLay->setContentsMargins(0, 0, 0, 0);
    midLay->setSpacing(0);

    auto *side = new QWidget(mid);
    side->setObjectName(QStringLiteral("sidebar"));
    side->setFixedWidth(200);
    auto *sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(10, 12, 10, 12);
    sideLay->setSpacing(4);

    struct Nav {
        const char *icon;
        const char *label;
        int page;
    };
    const Nav navs[] = { { "home", "Hearth", 0 },         { "download", "Versions", 1 },
                         { "profile", "Profiles", 2 },    { "search", "Discover", 3 },
                         { "account", "Accounts", 4 },    { "settings", "Settings", 5 } };
    for (const auto &n : navs) {
        auto *b = new QPushButton(IconProvider::instance().icon(QString::fromLatin1(n.icon)),
                                  QString::fromLatin1(n.label), side);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        const int page = n.page;
        connect(b, &QPushButton::clicked, this, [this, page] { selectPage(page); });
        sideLay->addWidget(b);
        m_nav.append(b);
    }
    sideLay->addStretch(1);
    auto *kbd = new QLabel(tr("Tip: Tab moves focus,\nEnter activates."), side);
    kbd->setObjectName(QStringLiteral("secondary"));
    kbd->setWordWrap(true);
    sideLay->addWidget(kbd);
    midLay->addWidget(side);

    m_stack = new QStackedWidget(mid);
    m_hearth = new HearthPage(store, instances, stones, modrinth, mods, embers, dataDir, m_stack);
    m_stack->addWidget(m_hearth);
    m_versions = new VersionsPage(settings, store, api, installer, java, launcher, *m_paths, m_stack);
    m_stack->addWidget(m_versions);
    m_profiles = new ProfilesPage(store, instances, api, *m_paths, m_stack);
    m_profiles->setModLoaderInstaller(loaders);
    m_stack->addWidget(m_profiles);
    m_discover = new DiscoverPage(api, instances, modrinth, mods, embers, dataDir, m_stack);
    m_stack->addWidget(m_discover);
    m_stack->addWidget(new AccountsPage(store, dataDir, tokens, microsoft, m_stack));
    auto *settingsPage = new SettingsPage(settings, store, theme, m_stack);
    settingsPage->setJavaManager(java);
    m_stack->addWidget(settingsPage);
    connect(m_versions, &VersionsPage::versionSelected, this, &MainWindow::onVersionSelected);
    connect(m_versions, &VersionsPage::playRequested, this, &MainWindow::onPlayRequested);
    connect(m_profiles, &ProfilesPage::playRequested, this, &MainWindow::onPlayRequested);
    connect(m_profiles, &ProfilesPage::modsRequested, this, &MainWindow::openMods);
    connect(m_profiles, &ProfilesPage::worldsRequested, this, &MainWindow::openWorlds);
    connect(m_profiles, &ProfilesPage::undoRequested, this, &MainWindow::runUndo);
    connect(m_profiles, &ProfilesPage::jarsDropped, this, &MainWindow::addJars);
    connect(m_hearth, &HearthPage::playRequested, this, &MainWindow::onPlayRequested);
    connect(m_hearth, &HearthPage::reviewMods, this, &MainWindow::openMods);
    connect(m_hearth, &HearthPage::undoRequested, this, &MainWindow::runUndo);
    connect(m_hearth, &HearthPage::browseProfiles, this, [this] { selectPage(2); });
    connect(m_discover, &DiscoverPage::installModpackFile, this, &MainWindow::onModpackFile);
    connect(m_discover, &DiscoverPage::installedTo, this, [this](const QString &) { m_profiles->rebuild(); });
    connect(m_mods, &ModManager::modsChanged, this, [this](const QString &) { m_profiles->rebuild(); });
    midLay->addWidget(m_stack, 1);
    root->addWidget(mid, 1);

    // ---- bottom play bar ----
    m_playBar = new PlayBar(central);
    connect(m_playBar, &PlayBar::playPressed, this, &MainWindow::onPlay);
    root->addWidget(m_playBar);

    connect(m_store, &AccountStore::changed, this, &MainWindow::refreshBar);
    connect(m_store, &AccountStore::activeAccountChanged, this, &MainWindow::refreshBar);
    connect(m_instances, &InstanceManager::changed, this, &MainWindow::refreshBar);
    const QString last = m_settings->lastVersionId();
    if (!last.isEmpty()) {
        m_currentVersion = last;
    }
    // Prefer the first instance when profiles exist.
    if (!m_instances->instances().isEmpty()) {
        m_currentInstance = m_instances->instances().first().id;
    }
    selectPage(0);
    refreshBar();
    QTimer::singleShot(0, this, &MainWindow::maybeKindling);
}

void MainWindow::selectPage(int idx)
{
    m_stack->setCurrentIndex(idx);
    for (int i = 0; i < m_nav.size(); ++i) {
        m_nav[i]->setChecked(i == idx);
    }
}

void MainWindow::onVersionSelected(const QString &id)
{
    m_currentVersion = id;
    m_currentInstance.clear(); // explicit version choice wins over profile
    m_settings->setLastVersionId(id);
    refreshBar();
}

void MainWindow::refreshBar()
{
    const Account a = m_store->activeAccount();
    if (a.isValid()) {
        m_playBar->setAccountText(QStringLiteral("%1  [%2]").arg(a.username).arg(accountTypeBadge(a.type)));
    } else if (m_versions && m_versions->demoRequested()) {
        m_playBar->setAccountText(tr("Demo mode (no account)"));
    } else {
        m_playBar->setAccountText(tr("No account — create one in Accounts"));
    }
    if (!m_currentInstance.isEmpty() && m_instances->has(m_currentInstance)) {
        const Instance in = m_instances->get(m_currentInstance);
        const QString loader = in.loaderType == QStringLiteral("vanilla")
            ? tr("Vanilla")
            : QStringLiteral("%1 %2").arg(loaderDisplayName(loaderTypeFromString(in.loaderType)), in.loaderVersion);
        m_playBar->setProfileText(tr("%1 — Minecraft %2 (%3)").arg(in.name, in.versionId, loader));
    } else if (!m_currentVersion.isEmpty()) {
        const bool installed = m_installer->quickIsInstalled(m_currentVersion);
        m_playBar->setProfileText(tr("Vanilla %1%2").arg(m_currentVersion).arg(installed ? QString() : tr(" (not installed)")));
    } else if (!m_instances->instances().isEmpty()) {
        m_currentInstance = m_instances->instances().first().id;
        refreshBar();
        return;
    } else {
        m_playBar->setProfileText(tr("Pick a version to play"));
    }
    const bool canPlay = (!m_currentInstance.isEmpty() && m_instances->has(m_currentInstance))
        || !m_currentVersion.isEmpty();
    const bool hasAccount = a.isValid() || (m_versions && m_versions->demoRequested());
    m_playBar->playButton()->setEnabled(canPlay && hasAccount && !m_activeTask);
    m_playBar->playButton()->setToolTip(canPlay ? tr("Install (if needed) and play") : tr("Pick a profile or version first."));
}

void MainWindow::onPlay()
{
    if (!m_currentInstance.isEmpty() && m_instances->has(m_currentInstance)) {
        startPlayInstance(m_currentInstance);
        return;
    }
    if (m_currentVersion.isEmpty()) {
        selectPage(1);
        return;
    }
    startPlay(m_currentVersion, false);
}

void MainWindow::onPlayRequested(const QString &idOrInstall)
{
    if (idOrInstall.startsWith(QStringLiteral("install:"))) {
        startPlay(idOrInstall.mid(8), true);
        return;
    }
    if (idOrInstall.startsWith(QStringLiteral("safe-mode:"))) {
        const QString id = idOrInstall.mid(QStringLiteral("safe-mode:").size());
        if (m_instances->has(id)) {
            m_currentInstance = id;
            refreshBar();
            startPlayInstance(id, true);
        }
        return;
    }
    if (idOrInstall.startsWith(QStringLiteral("import-hearthpack:"))
        || idOrInstall.startsWith(QStringLiteral("import-mrpack:"))
        || idOrInstall.startsWith(QStringLiteral("import-curseforge:"))) {
        importPack(idOrInstall);
        return;
    }
    // ProfilesPage emits raw instance ids; VersionsPage emits version ids.
    if (m_instances->has(idOrInstall)) {
        m_currentInstance = idOrInstall;
        refreshBar();
        startPlayInstance(idOrInstall);
        return;
    }
    m_currentVersion = idOrInstall;
    m_currentInstance.clear();
    startPlay(idOrInstall, false);
}

void MainWindow::onInstancePlay(const QString &instanceId)
{
    onPlayRequested(instanceId);
}

void MainWindow::showErrorWithDetails(QWidget *parent, const QString &title, const QString &text,
                                      const QString &details)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(text);
    if (!details.isEmpty()) {
        box.setDetailedText(details);
    }
    box.addButton(QMessageBox::Ok);
    box.exec();
}

bool MainWindow::resolveInstanceAccount(const Instance &in, Account *out, QString *error)
{
    Account acc;
    if (in.accountMode == InstanceAccountMode::Specific) {
        acc = m_store->accountById(in.accountId);
        if (!acc.isValid()) {
            if (error) {
                *error = tr("This profile wants a specific account that no longer exists. "
                            "Open the profile settings and pick another account.");
            }
            return false;
        }
    } else if (in.accountMode == InstanceAccountMode::Ask) {
        QStringList names;
        const auto all = m_store->accounts();
        for (const auto &a : all) {
            names.append(QStringLiteral("%1  [%2]").arg(a.username).arg(accountTypeBadge(a.type)));
        }
        if (names.isEmpty()) {
            if (error) {
                *error = tr("Create an account in Accounts first.");
            }
            return false;
        }
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, tr("Who's playing?"), tr("Account for “%1”:").arg(in.name),
                                                  names, 0, false, &ok);
        if (!ok) {
            return false;
        }
        const int idx = names.indexOf(pick);
        if (idx < 0 || idx >= all.size()) {
            return false;
        }
        acc = all.at(idx);
    } else {
        acc = m_store->activeAccount();
        const bool demo = m_versions && m_versions->demoRequested();
        if (!acc.isValid() && !demo) {
            if (error) {
                *error = tr("Select an account in Accounts first — or tick Demo mode on the "
                            "Versions page to try the official demo with no account.");
            }
            selectPage(4);
            return false;
        }
    }
    if (out) {
        *out = acc;
    }
    return true;
}

void MainWindow::startPlay(const QString &versionId, bool installOnly)
{
    if (m_activeTask) {
        return;
    }
    const Account acc = m_store->activeAccount();
    const bool demo = m_versions && m_versions->demoRequested();
    if (!demo && !acc.isValid()) {
        showErrorWithDetails(this, tr("Pick an account"),
                             tr("Select an account in Accounts first — or tick Demo mode on the "
                                "Versions page to try the official demo with no account."),
                             {});
        selectPage(4);
        return;
    }
    // Offline accounts and demo play immediately; Microsoft accounts refresh
    // silently inside the prepare task (worker thread, no GUI block).

    const int parallel = m_settings->downloadParallelism();
    const QString customJava = m_settings->javaPath();
    const bool isMicrosoft = !demo && acc.isValid() && acc.type == AccountType::Microsoft;
    auto *prep = new LambdaTask(
        installOnly ? tr("Install Minecraft %1").arg(versionId) : tr("Prepare Minecraft %1").arg(versionId),
        [this, versionId, installOnly, parallel, customJava, acc, demo, isMicrosoft](Task::Context &ctx) {
            struct Remap : public Task::Context {
                Task::Context &o;
                int from, to;
                Remap(Task::Context &outer, int f, int t)
                    : o(outer)
                    , from(f)
                    , to(t)
                {
                }
                void report(qint64 r, qint64 t, const QString &m = {}) override
                {
                    const double f = (t > 0 && r >= 0) ? qBound(0.0, double(r) / double(t), 1.0) : 0.0;
                    o.report(from + qint64(f * (to - from)), 1000, m);
                }
                bool isCancelled() const override { return o.isCancelled(); }
                void fail(const QString &m) override { o.fail(m); }
            };
            {
                Remap sub(ctx, 0, isMicrosoft ? 700 : 800);
                if (!m_installer->installBlocking(versionId, parallel, sub)) {
                    return false;
                }
            }
            // Microsoft silent refresh before any Java/download continuation.
            if (isMicrosoft && m_microsoft) {
                Remap sub(ctx, 700, 800);
                sub.report(0, 1, tr("Checking your Microsoft sign-in…"));
                Account tmp = acc;
                QString mcTok = m_microsoft->refreshBlocking(acc.id, &tmp, sub);
                if (mcTok.isEmpty()) {
                    return false;
                }
                sub.report(1, 1, tr("Done"));
            }
            if (installOnly) {
                ctx.report(1000, 1000, tr("Done"));
                return true;
            }
            QString err;
            bool vok = false;
            const ParsedVersion merged = m_api->loadMergedVersion(versionId, &vok, &err);
            if (!vok) {
                ctx.fail(err);
                return false;
            }
            {
                Remap sub(ctx, 800, 1000);
                if (!m_java->ensureBlocking(JavaManager::requiredMajorFor(merged.javaMajor), customJava, sub)) {
                    return false;
                }
            }
            ctx.report(1000, 1000, tr("Done"));
            return true;
        },
        this);
    m_activeTask = prep;
    m_playBar->bindTask(prep);
    refreshBar();
    connect(prep, &Task::finished, this, [this, prep, versionId, installOnly, demo](bool ok) {
        m_activeTask = nullptr;
        m_playBar->bindTask(nullptr);
        refreshBar();
        if (!ok) {
            showErrorWithDetails(this, installOnly ? tr("Install failed") : tr("Couldn't start the game"),
                                 prep->errorString(), Logger::redacted(prep->errorString()));
            prep->deleteLater();
            return;
        }
        prep->deleteLater();
        if (installOnly) {
            refreshBar();
            return;
        }
        launchBuilt(versionId, demo);
    });
    prep->start();
}

void MainWindow::startPlayInstance(const QString &instanceId, bool safeMode)
{
    if (m_activeTask) {
        return;
    }
    Instance in = m_instances->get(instanceId);
    if (!in.isValid()) {
        showErrorWithDetails(this, tr("Couldn't start the game"), tr("That profile no longer exists."), {});
        return;
    }
    const bool demo = m_versions && m_versions->demoRequested();
    Account acc;
    QString accErr;
    if (!demo) {
        if (!resolveInstanceAccount(in, &acc, &accErr)) {
            if (!accErr.isEmpty()) {
                showErrorWithDetails(this, tr("Pick an account"), accErr, {});
            }
            return;
        }
    }
    const bool isMicrosoft = !demo && acc.isValid() && acc.type == AccountType::Microsoft;
    const int parallel = m_settings->downloadParallelism();
    auto *prep = new LambdaTask(
        safeMode ? tr("Prepare %1 (safe mode)").arg(in.name) : tr("Prepare %1").arg(in.name),
        [this, in, parallel, acc, demo, isMicrosoft, safeMode](Task::Context &ctx) mutable {
            struct Remap : public Task::Context {
                Task::Context &o;
                int from, to;
                Remap(Task::Context &outer, int f, int t)
                    : o(outer)
                    , from(f)
                    , to(t)
                {
                }
                void report(qint64 r, qint64 t, const QString &m = {}) override
                {
                    const double f = (t > 0 && r >= 0) ? qBound(0.0, double(r) / double(t), 1.0) : 0.0;
                    o.report(from + qint64(f * (to - from)), 1000, m);
                }
                bool isCancelled() const override { return o.isCancelled(); }
                void fail(const QString &m) override { o.fail(m); }
            };
            // 0. Hearthstone auto-backup when worlds changed (fast stat check;
            //    copies only when needed, pruned to the last 3 autos).
            if (m_stones) {
                const QString autoName = m_stones->autoSnapshotIfNeeded(in.id);
                if (!autoName.isEmpty()) {
                    Logger::info(QStringLiteral("Auto world backup for %1: %2").arg(in.id, autoName));
                }
            }
            // 1. Vanilla files (0–55%).
            {
                Remap sub(ctx, 0, 550);
                if (!m_installer->installBlocking(in.versionId, parallel, sub)) {
                    return false;
                }
            }
            // 2. Microsoft silent refresh (55–65%).
            if (isMicrosoft && m_microsoft) {
                Remap sub(ctx, 550, 650);
                sub.report(0, 1, QObject::tr("Checking your Microsoft sign-in…"));
                Account tmp = acc;
                if (m_microsoft->refreshBlocking(acc.id, &tmp, sub).isEmpty()) {
                    return false;
                }
                sub.report(1, 1, QObject::tr("Done"));
            }
            // 3. Loader (65–85%).
            {
                Remap sub(ctx, 650, 850);
                const LoaderType lt = loaderTypeFromString(in.loaderType);
                if (loaderNeedsInstall(lt)) {
                    QString want = in.loaderVersion;
                    LoaderProfile prof;
                    if (!m_loaders->installBlocking(in.versionId, lt, want, parallel, sub, &prof)) {
                        return false;
                    }
                    // Record the resolved version back into instance.json.
                    if (!want.isEmpty() || (prof.ok && !prof.profileId.isEmpty())) {
                        Instance upd = m_instances->get(in.id);
                        if (upd.isValid() && upd.loaderVersion.isEmpty()) {
                            // installBlocking resolved "Latest stable": persist what we found.
                            upd.loaderVersion = want;
                            QString uerr;
                            m_instances->update(upd, &uerr);
                        }
                    }
                    // Ensure loader files exist for the effective profile.
                    bool vok = false;
                    QString verr;
                    ParsedVersion vanilla = m_api->loadMergedVersion(in.versionId, &vok, &verr);
                    if (!vok) {
                        ctx.fail(verr);
                        return false;
                    }
                    const ParsedVersion eff = ModLoaderInstaller::effectiveVersion(vanilla, prof);
                    if (!m_loaders->ensureLoaderFilesBlocking(eff, parallel, sub)) {
                        return false;
                    }
                } else {
                    sub.report(1, 1, QObject::tr("Done"));
                }
            }
            // 4. Java (85–100%).
            {
                bool vok = false;
                QString verr;
                ParsedVersion vanilla = m_api->loadMergedVersion(in.versionId, &vok, &verr);
                LoaderProfile prof;
                ParsedVersion eff = vanilla;
                if (vok && loaderNeedsInstall(loaderTypeFromString(in.loaderType))) {
                    QString lerr;
                    prof = m_loaders->loadCachedProfile(in.versionId, loaderTypeFromString(in.loaderType),
                                                        in.loaderVersion, &lerr);
                    if (prof.ok) {
                        eff = ModLoaderInstaller::effectiveVersion(vanilla, prof);
                    }
                }
                if (!vok) {
                    ctx.fail(verr);
                    return false;
                }
                Remap sub(ctx, 850, 1000);
                const QString javaPath = !in.javaPath.trimmed().isEmpty() ? in.javaPath : m_settings->javaPath();
                if (!m_java->ensureBlocking(JavaManager::requiredMajorFor(eff.javaMajor), javaPath, sub)) {
                    return false;
                }
            }
            ctx.report(1000, 1000, QObject::tr("Done"));
            Q_UNUSED(demo);
            return true;
        },
        this);
    m_activeTask = prep;
    m_playBar->bindTask(prep);
    refreshBar();
    connect(prep, &Task::finished, this, [this, prep, instanceId, acc, demo, safeMode](bool ok) {
        m_activeTask = nullptr;
        m_playBar->bindTask(nullptr);
        refreshBar();
        if (!ok) {
            showErrorWithDetails(this, tr("Couldn't start the game"), prep->errorString(),
                                 Logger::redacted(prep->errorString()));
            prep->deleteLater();
            return;
        }
        prep->deleteLater();
        Instance fresh = m_instances->get(instanceId);
        Account useAcc = acc;
        QString mcTok;
        if (!demo && useAcc.type == AccountType::Microsoft && m_microsoft) {
            mcTok = m_microsoft->cachedMinecraftToken(useAcc.id);
            // Refresh the display name from extras (minecraft.net renames).
            const MicrosoftExtras ex = loadMicrosoftExtras(m_dataDir, useAcc.id.toLower());
            if (!ex.username.isEmpty()) {
                useAcc.username = ex.username;
                useAcc.displayName = ex.username;
            }
            if (mcTok.isEmpty()) {
                showErrorWithDetails(this, tr("Couldn't start the game"),
                                     tr("Your Microsoft sign-in expired during preparation. Try playing again."),
                                     {});
                return;
            }
        }
        if (demo) {
            useAcc = Account{};
            useAcc.type = AccountType::Offline;
            useAcc.username = QStringLiteral("Player");
            useAcc.displayName = QStringLiteral("Player (demo)");
            useAcc.uuid = offlineUuidForUsername(QStringLiteral("Player"));
            useAcc.id = useAcc.uuid.toString(QUuid::WithoutBraces).toLower();
        }
        launchInstanceBuilt(fresh, useAcc, mcTok, safeMode);
    });
    prep->start();
}

void MainWindow::launchBuilt(const QString &versionId, bool demo)
{
    bool vok = false;
    QString verr;
    const ParsedVersion merged = m_api->loadMergedVersion(versionId, &vok, &verr);
    if (!vok) {
        showErrorWithDetails(this, tr("Couldn't start the game"), verr, verr);
        return;
    }
    LaunchOptions opts;
    opts.account = demo ? Account{} : m_store->activeAccount();
    if (demo) {
        opts.account.type = AccountType::Offline;
        opts.account.username = QStringLiteral("Player");
        opts.account.displayName = QStringLiteral("Player (demo)");
        opts.account.uuid = offlineUuidForUsername(QStringLiteral("Player"));
        opts.account.id = opts.account.uuid.toString(QUuid::WithoutBraces).toLower();
    }
    if (!demo && opts.account.type == AccountType::Microsoft && m_microsoft) {
        opts.minecraftToken = m_microsoft->cachedMinecraftToken(opts.account.id);
        const MicrosoftExtras ex = loadMicrosoftExtras(m_dataDir, opts.account.id.toLower());
        if (!ex.username.isEmpty()) {
            opts.account.username = ex.username;
            opts.account.displayName = ex.username;
        }
    }
    opts.versionId = versionId;
    opts.javaPath = m_settings->javaPath();
    opts.memoryMb = m_settings->memoryMb();
    opts.extraJvmArgs = m_settings->extraJvmArgs();
    opts.demo = demo;
    opts.width = m_versions->launchWidth();
    opts.height = m_versions->launchHeight();
    opts.fullscreen = m_versions->launchFullscreen();
    opts.clientId = m_settings->clientId();

    BuiltLaunch built;
    QString err;
    if (!m_launcher->build(merged, opts, &built, &err)) {
        showErrorWithDetails(this, tr("Couldn't start the game"), err, Logger::redacted(err));
        return;
    }
    QProcess *proc = m_launcher->spawn(built, opts, &err);
    if (!proc) {
        showErrorWithDetails(this, tr("Couldn't start the game"), err, Logger::redacted(err));
        return;
    }

    const QString behavior = m_settings->closeBehavior();
    bool doHide = false;
    if (behavior == QStringLiteral("minimize")) {
        showMinimized();
    } else if (behavior == QStringLiteral("close")) {
        doHide = true;
        hide();
    }

    auto *log = new GameLogDialog(versionId, this);
    log->setAttribute(Qt::WA_DeleteOnClose);
    log->setGameDir(built.workDir);
    log->attach(proc);
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    connect(proc, &QProcess::finished, this,
            [this, versionId, built, log, proc, startMs, doHide, opts](int code, QProcess::ExitStatus status) {
                Q_UNUSED(status);
                const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000;
                m_launcher->recordPlaytime(versionId, secs);
                // Post-exit command (Advanced; best-effort, detached).
                if (!opts.postExitCommand.isEmpty()) {
                    QProcess::startDetached(opts.postExitCommand.first(),
                                            opts.postExitCommand.mid(1), built.workDir);
                }
                log->detach();
                proc->deleteLater();
                if (doHide) {
                    show();
                    raise();
                    activateWindow();
                } else if (isMinimized()) {
                    showNormal();
                }
                if (code != 0) {
                    handleCrash(built.workDir, log->fullText(), code,
                                tr("Minecraft closed unexpectedly after %1 seconds.").arg(secs), log, nullptr);
                }
                Logger::info(QStringLiteral("Game %1 exited code=%2 after %3s")
                                 .arg(versionId)
                                 .arg(code)
                                 .arg(secs));
            });
    log->show();
}

void MainWindow::launchInstanceBuilt(const Instance &in, const Account &account, const QString &mcToken,
                                      bool safeMode)
{
    bool vok = false;
    QString verr;
    const ParsedVersion vanilla = m_api->loadMergedVersion(in.versionId, &vok, &verr);
    if (!vok) {
        showErrorWithDetails(this, tr("Couldn't start the game"), verr, verr);
        return;
    }
    LoaderProfile prof;
    ParsedVersion effective = vanilla;
    const LoaderType lt = loaderTypeFromString(in.loaderType);
    if (loaderNeedsInstall(lt)) {
        QString lerr;
        prof = m_loaders->loadCachedProfile(in.versionId, lt, in.loaderVersion, &lerr);
        if (!prof.ok && !in.loaderVersion.isEmpty()) {
            showErrorWithDetails(this, tr("Couldn't start the game"), lerr, lerr);
            return;
        }
        if (prof.ok) {
            effective = ModLoaderInstaller::effectiveVersion(vanilla, prof);
        }
    }
    LaunchOptions opts;
    opts.account = account;
    opts.versionId = in.versionId;
    opts.javaPath = !in.javaPath.trimmed().isEmpty() ? in.javaPath : m_settings->javaPath();
    opts.memoryMb = in.memoryMb > 0 ? in.memoryMb : m_settings->memoryMb();
    QString extra = m_settings->extraJvmArgs();
    if (!in.extraJvmArgs.trimmed().isEmpty()) {
        extra = (extra.trimmed() + QLatin1Char(' ') + in.extraJvmArgs).trimmed();
    }
    opts.extraJvmArgs = extra;
    opts.demo = m_versions && m_versions->demoRequested();
    opts.width = in.width;
    opts.height = in.height;
    opts.fullscreen = in.fullscreen;
    opts.gameDirOverride = instanceGameDir(m_dataDir, in);
    QDir().mkpath(opts.gameDirOverride);
    opts.clientId = m_settings->clientId();
    opts.minecraftToken = mcToken;
    opts.quickPlayWorld = in.quickPlayWorld;
    opts.quickPlayServer = in.quickPlayServer;
    opts.quickPlayRealm = in.quickPlayRealm;
    opts.preLaunchCommand = in.preLaunchCommand;
    opts.postExitCommand = in.postExitCommand;
    opts.extraEnv = in.envVars;
    // Memory suggestion accounts for mods in this instance.
    if (opts.memoryMb <= 0) {
        const int mods = instanceModCount(m_dataDir, in.id);
        opts.memoryMb = JavaManager::suggestMemoryMb(JavaManager::systemRamMb(), mods);
    }

    // Mod safe-mode: hide the mods folder for this launch only. It is
    // restored on every exit path (build/spawn failure, game exit).
    const QString safeModsDir = QDir(opts.gameDirOverride).filePath(QStringLiteral("mods"));
    const QString safeHoldDir = QDir(opts.gameDirOverride).filePath(QStringLiteral("mods.safehold"));
    bool modsHidden = false;
    auto restoreMods = [&] {
        if (modsHidden) {
            QFile::remove(safeModsDir); // stale empty dir, if any
            QFile::rename(safeHoldDir, safeModsDir);
            modsHidden = false;
        }
    };
    if (safeMode && QDir(safeModsDir).exists()) {
        QDir(safeHoldDir).removeRecursively();
        if (QFile::rename(safeModsDir, safeHoldDir)) {
            modsHidden = true;
            Logger::info(QStringLiteral("Safe mode for %1: mods hidden for one launch.").arg(in.id));
        } else {
            showErrorWithDetails(this, tr("Couldn't start safe mode"),
                                 tr("Hearthlight couldn't temporarily hide the mods folder (is the game running?)."),
                                 safeModsDir);
            return;
        }
    }

    // Offline guard for servers: explain instead of a generic failure.
    if (account.type == AccountType::Offline && !in.quickPlayServer.isEmpty()) {
        auto rc = QMessageBox::question(
            this, tr("Online server with an offline profile?"),
            tr("“%1” looks like an online server, and offline profiles can't authenticate to official "
               "online services.\n\nSingle-player and LAN where the game permits still work. Continue anyway?")
                .arg(in.quickPlayServer));
        if (rc != QMessageBox::Yes) {
            restoreMods();
            return;
        }
    }

    BuiltLaunch built;
    QString err;
    if (!m_launcher->build(effective, opts, &built, &err)) {
        bool authHelp = err.contains(QStringLiteral("Microsoft")) || err.contains(QStringLiteral("sign-in"));
        showErrorWithDetails(this, tr("Couldn't start the game"), err,
                             authHelp ? Logger::redacted(err)
                                      : (Logger::redacted(err) + QStringLiteral("\n\n")
                                         + Logger::redacted(Launcher::redactedCommand(built))));
        restoreMods();
        return;
    }
    QProcess *proc = m_launcher->spawn(built, opts, &err);
    if (!proc) {
        showErrorWithDetails(this, tr("Couldn't start the game"), err, Logger::redacted(err));
        restoreMods();
        return;
    }

    const QString behavior = m_settings->closeBehavior();
    bool doHide = false;
    if (behavior == QStringLiteral("minimize")) {
        showMinimized();
    } else if (behavior == QStringLiteral("close")) {
        doHide = true;
        hide();
    }

    auto *log = new GameLogDialog(QStringLiteral("%1 (%2)%3").arg(in.name, in.versionId,
                                                                               safeMode ? tr(" — safe mode") : QString()),
                                  this);
    log->setAttribute(Qt::WA_DeleteOnClose);
    log->setGameDir(built.workDir);
    log->attach(proc);
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    connect(proc, &QProcess::finished, this,
            [this, in, built, log, proc, startMs, doHide, opts, safeModsDir, safeHoldDir, modsHidden,
             safeMode](int code, QProcess::ExitStatus status) {
                Q_UNUSED(status);
                if (modsHidden) {
                    QFile::remove(safeModsDir);
                    QFile::rename(safeHoldDir, safeModsDir);
                    Logger::info(QStringLiteral("Safe mode for %1: mods restored.").arg(in.id));
                }
                const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000;
                m_launcher->recordPlaytime(in.versionId, secs);
                m_instances->recordPlay(in.id, secs, opts.account.username);
                if (!opts.postExitCommand.isEmpty()) {
                    QProcess::startDetached(opts.postExitCommand.first(), opts.postExitCommand.mid(1),
                                            built.workDir);
                }
                log->detach();
                proc->deleteLater();
                if (doHide) {
                    show();
                    raise();
                    activateWindow();
                } else if (isMinimized()) {
                    showNormal();
                }
                if (code != 0) {
                    handleCrash(built.workDir, log->fullText(), code,
                                tr("“%1” closed unexpectedly after %2 seconds.").arg(in.name).arg(secs), log, &in);
                }
                refreshBar();
                Logger::info(QStringLiteral("Instance %1 exited code=%2 after %3s")
                                 .arg(in.id)
                                 .arg(code)
                                 .arg(secs));
            });
    log->show();
}

void MainWindow::importPack(const QString &kindAndPath)
{
    if (m_activeTask) {
        return;
    }
    QString kind, path;
    const int colon = kindAndPath.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        kind = kindAndPath.left(colon);
        path = kindAndPath.mid(colon + 1);
    } else {
        return;
    }
    bool okName = false;
    const QString defName = QFileInfo(path).completeBaseName();
    const QString name = QInputDialog::getText(this, tr("Import as"), tr("Profile name:"), QLineEdit::Normal,
                                               defName, &okName);
    if (!okName || name.trimmed().isEmpty()) {
        return;
    }
    auto *t = new LambdaTask(
        tr("Import %1").arg(name.trimmed()),
        [this, kind, path, name](Task::Context &ctx) {
            if (kind == QStringLiteral("import-hearthpack")) {
                QString newId, err;
                if (!m_instances->importHearthpack(path, &newId, &err)) {
                    ctx.fail(err);
                    return false;
                }
                ctx.report(1, 1, tr("Done"));
                return true;
            }
            if (kind == QStringLiteral("import-mrpack")) {
                return m_instances->importMrpackBlocking(path, name.trimmed(), ctx, nullptr);
            }
            if (kind == QStringLiteral("import-curseforge")) {
                QString newId, err;
                QStringList skipped;
                if (!m_instances->importCurseforgeZip(path, name.trimmed(), &newId, &err, &skipped)) {
                    ctx.fail(err);
                    return false;
                }
                if (!skipped.isEmpty()) {
                    Logger::warning(QStringLiteral("CurseForge import skipped %1 remote file(s).").arg(skipped.size()));
                }
                ctx.report(1, 1, tr("Done"));
                return true;
            }
            ctx.fail(tr("Unknown import."));
            return false;
        },
        this);
    m_activeTask = t;
    m_playBar->bindTask(t);
    connect(t, &Task::finished, this, [this, t, name](bool ok2) {
        m_activeTask = nullptr;
        m_playBar->bindTask(nullptr);
        if (!ok2) {
            showErrorWithDetails(this, tr("Import failed"), t->errorString(), Logger::redacted(t->errorString()));
        } else {
            selectPage(2);
            if (!m_instances->instances().isEmpty()) {
                m_currentInstance = m_instances->instances().last().id;
            }
            refreshBar();
        }
        t->deleteLater();
    });
    t->start();
}

void MainWindow::openLog()
{
    LogViewerDialog dlg(this);
    dlg.exec();
}

void MainWindow::openMods(const QString &instanceId)
{
    if (!m_instances->has(instanceId)) {
        return;
    }
    InstalledModsDialog dlg(instanceId, m_instances, m_mods, m_modrinth, m_embers, m_dataDir, this);
    dlg.exec();
}

void MainWindow::openWorlds(const QString &instanceId)
{
    if (!m_instances->has(instanceId)) {
        return;
    }
    WorldsDialog dlg(instanceId, m_instances, m_stones, m_dataDir, this);
    dlg.exec();
}

void MainWindow::runUndo(const QString &instanceId)
{
    if (!m_instances->has(instanceId)) {
        return;
    }
    const Instance in = m_instances->get(instanceId);
    auto rc = QMessageBox::question(this, tr("Undo last change?"),
                                    tr("Restore “%1” to the snapshot Embers took before the last change? "
                                       "Today's state is preserved first, so nothing is lost.")
                                        .arg(in.name));
    if (rc != QMessageBox::Yes) {
        return;
    }
    auto *t = new LambdaTask(
        tr("Undo last change"),
        [this, instanceId](Task::Context &ctx) {
            Q_UNUSED(ctx);
            QString err;
            if (!m_embers->undoLast(instanceId, &err)) {
                ctx.fail(err);
                return false;
            }
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    connect(t, &Task::finished, this, [this, t, in](bool ok) {
        t->deleteLater();
        if (!ok) {
            showErrorWithDetails(this, tr("Couldn't undo"), t->errorString(), Logger::redacted(t->errorString()));
            return;
        }
        m_instances->load();
        m_profiles->rebuild();
        QMessageBox::information(this, tr("Undone"), tr("“%1” is back to how it was.").arg(in.name));
    });
    t->start();
    prog.exec();
}

void MainWindow::addJars(const QString &instanceId, const QStringList &jarPaths)
{
    if (!m_instances->has(instanceId) || jarPaths.isEmpty()) {
        return;
    }
    const Instance in = m_instances->get(instanceId);
    QStringList added, failed;
    for (const auto &jar : jarPaths) {
        QString err, name;
        if (m_mods->addExternalJar(instanceId, jar, &err, &name)) {
            added.append(name);
        } else {
            failed.append(QStringLiteral("%1 (%2)").arg(QFileInfo(jar).fileName(), err));
        }
    }
    if (!failed.isEmpty()) {
        showErrorWithDetails(this, tr("Some jars couldn't be added"), failed.join(QLatin1Char('\n')),
                             added.isEmpty() ? QString() : tr("Added: %1").arg(added.join(QStringLiteral(", "))));
    } else {
        QMessageBox::information(this, tr("Mods added"),
                                 tr("Added %1 to “%2”. They show as “added by hand” in Mods….").arg(added.join(QStringLiteral(", ")), in.name));
    }
    m_profiles->rebuild();
}

void MainWindow::onModpackFile(const QString &mrpackPath)
{
    importPack(QStringLiteral("import-mrpack:") + mrpackPath);
}

void MainWindow::maybeKindling()
{
    if (m_kindlingShown) {
        return;
    }
    m_kindlingShown = true;
    if (!m_instances->instances().isEmpty()) {
        return; // returning player: nothing to set up
    }
    KindlingDialog dlg(m_store, m_instances, m_api, m_modrinth, m_mods, m_dataDir, this);
    if (dlg.exec() != QDialog::Accepted || dlg.createdInstanceId().isEmpty()) {
        // Declined or failed with no profile: fall back to the plain starter
        // so Play still works in 60 seconds.
        if (m_instances->instances().isEmpty()) {
            bool ok = false;
            const VersionManifest manifest = m_api->cachedManifest(&ok);
            Instance starter;
            starter.name = QStringLiteral("My first world");
            starter.versionId =
                (ok && !manifest.latestRelease.isEmpty()) ? manifest.latestRelease : QStringLiteral("1.20.4");
            starter.loaderType = QStringLiteral("vanilla");
            starter.accountMode = InstanceAccountMode::Current;
            QString cerr;
            m_instances->create(starter, &cerr);
        }
        return;
    }
    m_currentInstance = dlg.createdInstanceId();
    refreshBar();
    if (m_hearth) {
        m_hearth->refresh();
    }
}

void MainWindow::handleCrash(const QString &gameDir, const QString &logText, int exitCode,
                             const QString &titleSeed, QWidget *logWindow, const struct Instance *inst)
{
    QString reportFile;
    const QString report = CrashDoctor::latestCrashReport(gameDir, &reportFile);
    // Freshness: only trust reports written around this session (10 min).
    bool fresh = false;
    if (!reportFile.isEmpty()) {
        const QDateTime mt =
            QFileInfo(QDir(gameDir).filePath(QStringLiteral("crash-reports/%1").arg(reportFile))).lastModified();
        fresh = mt.isValid() && mt.toMSecsSinceEpoch() >= QDateTime::currentMSecsSinceEpoch() - 600000;
    }
    const CrashDiagnosis d = CrashDoctor::diagnose(logText, fresh ? report : QString(), exitCode);
    if (logWindow) {
        logWindow->setWindowTitle(d.title);
    }

    QMessageBox box(logWindow);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(d.title);
    QString text = titleSeed + QStringLiteral("\n\n") + d.explanation;
    if (fresh && !reportFile.isEmpty()) {
        text += QStringLiteral("\n\n") + tr("Crash report: crash-reports/%1").arg(reportFile);
    }
    box.setText(text);
    box.setDetailedText(Logger::redacted(logText.right(8000)));
    box.addButton(QMessageBox::Ok);
    QMap<QString, QPushButton *> fixBtns;
    for (const auto &fix : d.fixes) {
        // Safe-mode and memory fixes only make sense with a real profile.
        if (!inst && (fix.action == QStringLiteral("safe-mode") || fix.action == QStringLiteral("raise-memory"))) {
            continue;
        }
        QPushButton *b = box.addButton(fix.label, QMessageBox::ActionRole);
        fixBtns.insert(fix.action, b);
    }
    box.exec();
    auto *clicked = qobject_cast<QPushButton *>(box.clickedButton());
    if (!clicked) {
        return;
    }
    const QString action = fixBtns.key(clicked);
    if (action == QStringLiteral("safe-mode") && inst) {
        startPlayInstance(inst->id, true);
    } else if (action == QStringLiteral("raise-memory") && inst) {
        Instance upd = m_instances->get(inst->id);
        if (upd.isValid()) {
            const int current = upd.memoryMb > 0 ? upd.memoryMb : m_settings->memoryMb();
            const int base = current > 0 ? current
                                         : JavaManager::suggestMemoryMb(JavaManager::systemRamMb(),
                                                                        instanceModCount(m_dataDir, upd.id));
            upd.memoryMb = CrashDoctor::suggestedMemoryForOom(base, JavaManager::systemRamMb());
            QString err;
            if (m_instances->update(upd, &err)) {
                QMessageBox::information(this, tr("Memory raised"),
                                         tr("“%1” now gets %2 MB. Press Play to try again.").arg(upd.name).arg(upd.memoryMb));
            } else {
                showErrorWithDetails(this, tr("Couldn't change memory"), err, err);
            }
        }
    } else if (action == QStringLiteral("open-mods") && inst) {
        openMods(inst->id);
    } else if (action == QStringLiteral("open-crash")) {
        if (!reportFile.isEmpty()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QDir(gameDir).filePath(QStringLiteral("crash-reports/%1").arg(reportFile))));
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(gameDir).filePath(QStringLiteral("crash-reports"))));
        }
    } else if (action == QStringLiteral("open-java")) {
        selectPage(5); // Settings (Java lives there + per-profile settings)
    } else if (action == QStringLiteral("resign-microsoft")) {
        selectPage(4); // Accounts
    }
}

#include "MainWindow.moc"
