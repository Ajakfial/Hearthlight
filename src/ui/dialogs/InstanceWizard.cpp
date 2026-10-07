#include "dialogs/InstanceWizard.h"

#include "AccountStore.h"
#include "DownloadManager.h"
#include "GamePaths.h"
#include "ModLoader.h"
#include "MojangApi.h"
#include "Task.h"

#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWizardPage>

InstanceWizard::InstanceWizard(AccountStore *accounts, MojangApi *api, const GamePaths &paths, QWidget *parent)
    : QWizard(parent)
    , m_accounts(accounts)
    , m_api(api)
    , m_paths(paths)
{
    setWindowTitle(tr("New profile"));
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(560, 420);

    // Page 1: name.
    auto *p1 = new QWizardPage(this);
    p1->setTitle(tr("Name your profile"));
    p1->setSubTitle(tr("Something friendly like “Survival” or “Create with friends”."));
    auto *l1 = new QVBoxLayout(p1);
    m_name = new QLineEdit(p1);
    m_name->setPlaceholderText(tr("e.g. Cozy survival"));
    l1->addWidget(new QLabel(tr("Profile name"), p1));
    l1->addWidget(m_name);
    addPage(p1);

    // Page 2: version + loader.
    auto *p2 = new QWizardPage(this);
    p2->setTitle(tr("Pick Minecraft and how to play it"));
    p2->setSubTitle(tr("Vanilla is just Minecraft. Fabric/Quilt are light mod loaders; "
                       "Forge/NeoForge power big modpacks."));
    auto *form = new QFormLayout(p2);
    m_version = new QComboBox(p2);
    m_version->setEditable(false);
    form->addRow(tr("Minecraft version"), m_version);
    m_loader = new QComboBox(p2);
    m_loader->addItem(loaderDisplayName(LoaderType::Vanilla), loaderTypeToString(LoaderType::Vanilla));
    m_loader->addItem(loaderDisplayName(LoaderType::Fabric), loaderTypeToString(LoaderType::Fabric));
    m_loader->addItem(loaderDisplayName(LoaderType::Quilt), loaderTypeToString(LoaderType::Quilt));
    m_loader->addItem(loaderDisplayName(LoaderType::Forge), loaderTypeToString(LoaderType::Forge));
    m_loader->addItem(loaderDisplayName(LoaderType::NeoForge), loaderTypeToString(LoaderType::NeoForge));
    form->addRow(tr("Play style"), m_loader);
    m_loaderVer = new QComboBox(p2);
    m_loaderVer->setToolTip(tr("Latest stable is recommended. Full lists load when online."));
    form->addRow(tr("Loader version"), m_loaderVer);
    addPage(p2);

    // Page 3: account.
    auto *p3 = new QWizardPage(this);
    p3->setTitle(tr("Who plays here?"));
    p3->setSubTitle(tr("Offline profiles work with no internet. Microsoft accounts unlock online servers."));
    auto *l3 = new QVBoxLayout(p3);
    m_account = new QComboBox(p3);
    m_account->addItem(tr("Whoever is selected when I press Play"), QStringLiteral("current"));
    for (const auto &a : m_accounts->accounts()) {
        m_account->addItem(QStringLiteral("%1  [%2]").arg(a.username).arg(accountTypeBadge(a.type)), a.id);
    }
    m_account->addItem(tr("Ask me every launch"), QStringLiteral("ask"));
    l3->addWidget(new QLabel(tr("Account for this profile"), p3));
    l3->addWidget(m_account);
    addPage(p3);

    loadCachedVersions();
    connect(m_loader, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InstanceWizard::onLoaderChanged);
    connect(m_version, &QComboBox::currentTextChanged, this, &InstanceWizard::onMcVersionChanged);
    onLoaderChanged(0);
}

bool InstanceWizard::loadCachedVersions()
{
    bool ok = false;
    const VersionManifest m = m_api->cachedManifest(&ok);
    m_version->clear();
    if (!ok || m.versions.isEmpty()) {
        m_version->addItem(tr("(go online once to fetch versions)"), QString());
        return false;
    }
    // Releases first, newest first (manifest is already newest-first).
    for (const auto &e : m.versions) {
        if (e.type == QStringLiteral("release")) {
            m_version->addItem(e.id, e.id);
        }
    }
    for (const auto &e : m.versions) {
        if (e.type != QStringLiteral("release")) {
            m_version->addItem(QStringLiteral("%1 (%2)").arg(e.id, e.type), e.id);
        }
    }
    if (!m.latestRelease.isEmpty()) {
        const int idx = m_version->findData(m.latestRelease);
        if (idx >= 0) {
            m_version->setCurrentIndex(idx);
        }
    }
    return true;
}

void InstanceWizard::onLoaderChanged(int idx)
{
    Q_UNUSED(idx);
    refreshLoaderVersions();
}

void InstanceWizard::onMcVersionChanged(const QString &mc)
{
    Q_UNUSED(mc);
    refreshLoaderVersions();
}

void InstanceWizard::refreshLoaderVersions()
{
    m_loaderVer->clear();
    const LoaderType t = loaderTypeFromString(m_loader->currentData().toString());
    if (t == LoaderType::Vanilla) {
        m_loaderVer->addItem(tr("None (vanilla)"), QString());
        m_loaderVer->setEnabled(false);
        return;
    }
    m_loaderVer->setEnabled(true);
    m_loaderVer->addItem(tr("Latest stable (recommended)"), QString());
    // Show cached loader lists when offline; full refresh happens at install.
    const QString mc = m_version->currentData().toString();
    ModLoaderInstaller tmpInstaller(m_paths, nullptr, this);
    const QString cacheDir = tmpInstaller.loaderCacheDir();
    if (t == LoaderType::Fabric && QFile::exists(QDir(cacheDir).filePath(QStringLiteral("fabric-%1.json").arg(mc)))) {
        m_loaderVer->addItem(tr("(cached list — full list loads at install)"), QStringLiteral("__cached__"));
    } else if (t == LoaderType::Quilt
        && QFile::exists(QDir(cacheDir).filePath(QStringLiteral("quilt-%1.json").arg(mc)))) {
        m_loaderVer->addItem(tr("(cached list — full list loads at install)"), QStringLiteral("__cached__"));
    }
    m_loaderVer->setCurrentIndex(0);
}

bool InstanceWizard::validateCurrentPage()
{
    if (currentId() == 0 && m_name->text().trimmed().isEmpty()) {
        return false;
    }
    if (currentId() == 1 && m_version->currentData().toString().isEmpty()) {
        return false;
    }
    if (currentId() == 2) {
        Instance in;
        in.name = m_name->text().trimmed();
        in.versionId = m_version->currentData().toString();
        in.loaderType = m_loader->currentData().toString();
        QString lv = m_loaderVer->currentData().toString();
        if (lv == QStringLiteral("__cached__")) {
            lv.clear();
        }
        in.loaderVersion = lv;
        const QString acc = m_account->currentData().toString();
        if (acc == QStringLiteral("ask")) {
            in.accountMode = InstanceAccountMode::Ask;
        } else if (acc == QStringLiteral("current") || acc.isEmpty()) {
            in.accountMode = InstanceAccountMode::Current;
        } else {
            in.accountMode = InstanceAccountMode::Specific;
            in.accountId = acc;
        }
        m_result = in;
    }
    return QWizard::validateCurrentPage();
}

#include "InstanceWizard.moc"
