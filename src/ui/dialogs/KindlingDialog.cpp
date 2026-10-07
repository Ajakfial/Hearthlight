#include "dialogs/KindlingDialog.h"

#include "AccountProvider.h"
#include "AccountStore.h"
#include "InstanceManager.h"
#include "Logger.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "Task.h"
#include "dialogs/CreateOfflineAccountDialog.h"
#include "dialogs/MicrosoftLoginDialog.h"
#include "dialogs/TaskProgressDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWizardPage>

KindlingDialog::KindlingDialog(AccountStore *accounts, InstanceManager *instances, MojangApi *mojang,
                               ModrinthApi *modrinth, ModManager *mods, const QString &dataDir, QWidget *parent)
    : QWizard(parent)
    , m_accounts(accounts)
    , m_instances(instances)
    , m_mojang(mojang)
    , m_modrinth(modrinth)
    , m_mods(mods)
    , m_dataDir(dataDir)
{
    setWindowTitle(tr("Welcome to Hearthlight"));
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(560, 460);

    // Page 1: who are you?
    auto *p1 = new QWizardPage(this);
    p1->setTitle(tr("First, who is playing?"));
    p1->setSubTitle(tr("A local profile works offline in seconds; a Microsoft account unlocks online servers."));
    auto *l1 = new QVBoxLayout(p1);
    m_accountLabel = new QLabel(p1);
    m_accountLabel->setWordWrap(true);
    l1->addWidget(m_accountLabel);
    auto *offBtn = new QPushButton(tr("Create an offline profile"), p1);
    connect(offBtn, &QPushButton::clicked, this, &KindlingDialog::onOfflineClicked);
    l1->addWidget(offBtn);
    auto *msBtn = new QPushButton(tr("Sign in with Microsoft"), p1);
    connect(msBtn, &QPushButton::clicked, this, &KindlingDialog::onMicrosoftClicked);
    l1->addWidget(msBtn);
    l1->addStretch(1);
    addPage(p1);

    // Page 2: starting style.
    auto *p2 = new QWizardPage(this);
    p2->setTitle(tr("How do you want to start?"));
    p2->setSubTitle(tr("Pick a style — Hearthlight builds the profile and fetches matching mods."));
    auto *l2 = new QVBoxLayout(p2);
    m_packs = Kindling::builtinPacks();
    QStringList skipped;
    for (const auto &cp : Kindling::loadCustomPacks(m_dataDir, &skipped)) {
        m_packs.append(cp);
    }
    for (const auto &pack : m_packs) {
        auto *r = new QRadioButton(QStringLiteral("%1 — %2").arg(pack.title, pack.summary), p2);
        r->setProperty("packId", pack.id);
        l2->addWidget(r);
        m_styleRadios.append(r);
    }
    if (!m_styleRadios.isEmpty()) {
        m_styleRadios.first()->setChecked(true);
    }
    if (!skipped.isEmpty()) {
        auto *note = new QLabel(tr("Skipped %1 invalid pack file(s) in the kindling folder.").arg(skipped.size()), p2);
        note->setObjectName(QStringLiteral("secondary"));
        note->setWordWrap(true);
        l2->addWidget(note);
    }
    auto *mcRow = new QHBoxLayout();
    mcRow->addWidget(new QLabel(tr("Minecraft version:"), p2));
    m_mc = new QComboBox(p2);
    {
        bool ok = false;
        const VersionManifest manifest = m_mojang ? m_mojang->cachedManifest(&ok) : VersionManifest{};
        if (ok && !manifest.latestRelease.isEmpty()) {
            m_mc->addItem(manifest.latestRelease + tr(" (latest)"), manifest.latestRelease);
        }
        if (ok) {
            int added = 0;
            for (const auto &e : manifest.versions) {
                if (e.type == QStringLiteral("release") && e.id != manifest.latestRelease && added < 15) {
                    m_mc->addItem(e.id, e.id);
                    ++added;
                }
            }
        }
        if (m_mc->count() == 0) {
            m_mc->addItem(tr("(go online once to fetch versions)"), QString());
        }
    }
    mcRow->addWidget(m_mc, 1);
    l2->addLayout(mcRow);
    auto *off = new QLabel(tr("Without internet, the profile is created vanilla and mods wait for later."), p2);
    off->setObjectName(QStringLiteral("secondary"));
    off->setWordWrap(true);
    l2->addWidget(off);
    addPage(p2);

    refreshAccountLabel();
    connect(m_accounts, &AccountStore::changed, this, &KindlingDialog::refreshAccountLabel);
    connect(m_accounts, &AccountStore::activeAccountChanged, this, &KindlingDialog::refreshAccountLabel);
}

void KindlingDialog::refreshAccountLabel()
{
    const Account a = m_accounts->activeAccount();
    if (a.isValid()) {
        m_accountLabel->setText(tr("Playing as %1 (%2). You can change this any time on the Accounts page.")
                                    .arg(a.username, accountTypeBadge(a.type)));
    } else {
        m_accountLabel->setText(tr("No account yet — create one below (ten seconds, works offline)."));
    }
}

void KindlingDialog::onOfflineClicked()
{
    CreateOfflineAccountDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    OfflineAccountProvider provider(this);
    OfflineAccountProvider::CreateOptions opts;
    opts.username = dlg.username();
    opts.avatarColor = dlg.avatarColor();
    opts.useCustomUuid = dlg.useCustomUuid();
    if (opts.useCustomUuid) {
        opts.customUuid = QUuid::fromString(dlg.customUuidText());
    }
    const auto res = provider.createAccount(opts);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Can't create that account"),
                             res.error + QStringLiteral("\n\n") + res.errorDetails);
        return;
    }
    QString err;
    if (!m_accounts->addAccount(res.value, &err)) {
        QMessageBox::warning(this, tr("Can't create that account"), err);
        return;
    }
    m_accounts->setActiveAccountId(res.value.id);
}

void KindlingDialog::onMicrosoftClicked()
{
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        QMessageBox::information(this, tr("Sign in with Microsoft"),
                                 tr("Microsoft sign-in needs an internet connection."));
        return;
    }
    // SecureTokenStore is owned by Application; the dialog resolves it
    // through the provider chain — pass nullptr and let it degrade? No:
    // MicrosoftLoginDialog requires the store. Ask MainWindow? Simplest
    // honest path: reuse the Accounts page after setup.
    QMessageBox::information(this, tr("Sign in with Microsoft"),
                             tr("Finish this quick setup with an offline profile (or none), then sign in "
                                "on the Accounts page — it takes a minute and your profile stays."));
}

bool KindlingDialog::validateCurrentPage()
{
    if (currentId() == 0 && !m_accounts->activeAccount().isValid()) {
        // Allow continuing without an account (demo/anonymous setup);
        // the profile uses "Current" and Play will ask then.
        auto rc = QMessageBox::question(this, tr("No account yet"),
                                        tr("Continue without an account for now? You can add one any time on "
                                           "the Accounts page."));
        if (rc != QMessageBox::Yes) {
            return false;
        }
    }
    if (currentId() == 1 && m_mc->currentData().toString().isEmpty()) {
        QMessageBox::information(this, tr("No versions"),
                                 tr("Go online once so Hearthlight can fetch the version list, then try again."));
        return false;
    }
    return QWizard::validateCurrentPage();
}

void KindlingDialog::accept()
{
    StarterPack pack = m_packs.isEmpty() ? StarterPack{} : m_packs.first();
    for (int i = 0; i < m_styleRadios.size(); ++i) {
        if (m_styleRadios.at(i)->isChecked() && i < m_packs.size()) {
            pack = m_packs.at(i);
        }
    }
    const QString mc = m_mc->currentData().toString();
    const QString loader = pack.loader.isEmpty() ? QStringLiteral("vanilla") : pack.loader;
    const QStringList slugs = pack.slugs;
    const QString title = pack.title.isEmpty() ? tr("My first world") : pack.title;

    QString createdId;
    QStringList installed;
    QStringList skipped;
    auto *t = new LambdaTask(
        tr("Setting up “%1”").arg(title),
        [this, title, mc, loader, slugs, &createdId, &installed, &skipped](Task::Context &ctx) {
            Instance in;
            in.name = title;
            in.versionId = mc;
            in.loaderType = loader;
            in.accountMode = InstanceAccountMode::Current;
            QString cerr;
            if (!m_instances->create(in, &cerr)) {
                ctx.fail(cerr);
                return false;
            }
            createdId = m_instances->instances().last().id;
            if (slugs.isEmpty() || loader == QStringLiteral("vanilla")
                || NetworkStatus::instance().isEffectivelyOffline()) {
                ctx.report(1, 1, tr("Done"));
                return true;
            }
            int i = 0;
            QList<ModInstallPlan> roots;
            for (const auto &slug : slugs) {
                if (ctx.isCancelled()) {
                    ctx.fail(tr("Cancelled."));
                    return false;
                }
                ctx.report(i++, slugs.size() * 2, tr("Finding %1…").arg(slug));
                const ModrinthProject proj = m_modrinth->projectBlocking(slug, ctx);
                if (proj.id.isEmpty()) {
                    skipped.append(tr("%1 (not found)").arg(slug));
                    continue;
                }
                const QList<ModrinthVersion> vers = m_modrinth->versionsBlocking(proj.id, loader, mc, ctx);
                const int best = ModrinthMeta::pickBestVersion(vers, mc, loader, proj.projectType);
                if (best < 0) {
                    skipped.append(tr("%1 (no file for Minecraft %2 + %3)").arg(proj.title, mc, loader));
                    continue;
                }
                ModInstallPlan root;
                root.project = proj;
                root.version = vers.at(best);
                root.file = root.version.bestFile();
                root.targetDir = ModrinthMeta::targetDirForType(proj.projectType);
                roots.append(root);
            }
            if (!roots.isEmpty()) {
                QStringList warnings;
                QString err;
                const auto plan = m_modrinth->resolveInstallPlan(roots, mc, loader, ctx, &warnings, &err);
                if (plan.isEmpty() && !err.isEmpty()) {
                    // Install what we can; report the conflict honestly.
                    skipped.append(err);
                } else {
                    int j = 0;
                    for (const auto &u : plan) {
                        if (ctx.isCancelled()) {
                            ctx.fail(tr("Cancelled."));
                            return false;
                        }
                        ctx.report(slugs.size() + j++, slugs.size() + plan.size(),
                                   tr("Installing %1…").arg(u.project.title));
                        if (!m_mods->installUnitBlocking(createdId, u, 8, ctx)) {
                            skipped.append(tr("%1 (download failed)").arg(u.project.title));
                            continue;
                        }
                        installed.append(u.project.title);
                    }
                    skipped += warnings;
                }
            }
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    QString taskErr;
    connect(t, &Task::finished, this, [&](bool ok) {
        if (!ok) {
            taskErr = t->errorString();
        }
    });
    t->start();
    prog.exec();
    t->deleteLater();
    if (!taskErr.isEmpty() && createdId.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't finish setup"), taskErr);
        return;
    }
    if (!skipped.isEmpty()) {
        QMessageBox::information(this, tr("“%1” is ready").arg(title),
                                 tr("Your profile is ready. A few things need attention:\n\n%1%2")
                                     .arg(skipped.join(QStringLiteral("\n")),
                                          installed.isEmpty()
                                              ? tr("\n\nThe profile works as vanilla; try the mods again online.")
                                              : QString()));
    }
    m_createdId = createdId;
    QWizard::accept();
}

#include "KindlingDialog.moc"
