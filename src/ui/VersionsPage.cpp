#include "VersionsPage.h"

#include "AccountStore.h"
#include "AppSettings.h"
#include "IconProvider.h"
#include "JavaManager.h"
#include "Launcher.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "Task.h"
#include "VersionInstaller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

VersionsPage::VersionsPage(AppSettings *settings, AccountStore *accounts, MojangApi *api, VersionInstaller *installer,
                           JavaManager *java, Launcher *launcher, const GamePaths &paths, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_accounts(accounts)
    , m_api(api)
    , m_installer(installer)
    , m_java(java)
    , m_launcher(launcher)
    , m_paths(paths)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(10);

    auto *title = new QLabel(tr("Minecraft versions"), this);
    QFont tf = title->font();
    tf.setPointSize(20);
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);

    auto *sub = new QLabel(tr("Official Mojang releases, downloaded once and playable offline afterwards."), this);
    sub->setObjectName(QStringLiteral("secondary"));
    sub->setWordWrap(true);
    outer->addWidget(sub);

    // Filter row.
    auto *filters = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search versions…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &VersionsPage::onSearchChanged);
    filters->addWidget(m_search, 1);
    m_fReleases = new QCheckBox(tr("Releases"), this);
    m_fSnapshots = new QCheckBox(tr("Snapshots"), this);
    m_fBeta = new QCheckBox(tr("Old Beta"), this);
    m_fAlpha = new QCheckBox(tr("Old Alpha"), this);
    m_fReleases->setChecked(m_settings->showReleases());
    m_fSnapshots->setChecked(m_settings->showSnapshots());
    m_fBeta->setChecked(m_settings->showBeta());
    m_fAlpha->setChecked(m_settings->showAlpha());
    for (auto *c : { m_fReleases, m_fSnapshots, m_fBeta, m_fAlpha }) {
        filters->addWidget(c);
        connect(c, &QCheckBox::toggled, this, &VersionsPage::onFilterToggled);
    }
    m_refreshBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("update")), tr("Refresh"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &VersionsPage::onRefresh);
    filters->addWidget(m_refreshBtn);
    outer->addLayout(filters);

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(split);
    m_list->setMinimumWidth(320);
    connect(m_list, &QListWidget::currentRowChanged, this, &VersionsPage::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (it && !it->data(Qt::UserRole).toString().isEmpty()) {
            emit playRequested(it->data(Qt::UserRole).toString());
        }
    });

    // Detail column.
    auto *detailW = new QWidget(split);
    auto *detail = new QVBoxLayout(detailW);
    detail->setContentsMargins(12, 0, 0, 0);
    m_detail = new QLabel(tr("Select a version to see details."), detailW);
    m_detail->setWordWrap(true);
    detail->addWidget(m_detail);

    auto *btnRow = new QHBoxLayout();
    m_installBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("download")), tr("Install"), detailW);
    connect(m_installBtn, &QPushButton::clicked, this, &VersionsPage::onInstall);
    btnRow->addWidget(m_installBtn);
    m_playBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("play")), tr("Play"), detailW);
    connect(m_playBtn, &QPushButton::clicked, this, &VersionsPage::onPlayClicked);
    btnRow->addWidget(m_playBtn);
    btnRow->addStretch(1);
    detail->addLayout(btnRow);

    m_demo = new QCheckBox(tr("Demo mode (official demo flag, no account needed)"), detailW);
    m_demo->setToolTip(tr("Launches with the game's official --demo flag for people without an account."));
    detail->addWidget(m_demo);

    // Advanced (hidden complexity by default).
    auto *adv = new QCheckBox(tr("Show advanced (memory, JVM args, window)"), detailW);
    detail->addWidget(adv);
    auto *advBox = new QWidget(detailW);
    advBox->setVisible(false);
    connect(adv, &QCheckBox::toggled, advBox, &QWidget::setVisible);
    auto *advLay = new QVBoxLayout(advBox);
    advLay->setContentsMargins(0, 0, 0, 0);
    auto *memRow = new QHBoxLayout();
    memRow->addWidget(new QLabel(tr("Memory"), advBox));
    m_memory = new QSlider(Qt::Horizontal, advBox);
    m_memory->setRange(1024, 8192);
    m_memory->setSingleStep(256);
    m_memory->setPageStep(512);
    m_memory->setValue(m_settings->memoryMb() > 0 ? m_settings->memoryMb()
                                                  : JavaManager::suggestMemoryMb(JavaManager::systemRamMb(), 0));
    connect(m_memory, &QSlider::valueChanged, this, &VersionsPage::updateMemoryLabel);
    memRow->addWidget(m_memory, 1);
    m_memoryLabel = new QLabel(advBox);
    memRow->addWidget(m_memoryLabel);
    advLay->addLayout(memRow);
    m_jvmArgs = new QLineEdit(m_settings->extraJvmArgs(), advBox);
    m_jvmArgs->setPlaceholderText(tr("Extra JVM args (optional)"));
    connect(m_jvmArgs, &QLineEdit::editingFinished, this,
            [this] { m_settings->setExtraJvmArgs(m_jvmArgs->text().trimmed()); });
    advLay->addWidget(m_jvmArgs);
    auto *winRow = new QHBoxLayout();
    winRow->addWidget(new QLabel(tr("Window"), advBox));
    m_width = new QSpinBox(advBox);
    m_width->setRange(0, 7680);
    m_width->setSpecialValueText(tr("auto"));
    m_height = new QSpinBox(advBox);
    m_height->setRange(0, 4320);
    m_height->setSpecialValueText(tr("auto"));
    m_fullscreen = new QCheckBox(tr("Fullscreen"), advBox);
    winRow->addWidget(m_width);
    winRow->addWidget(new QLabel(tr("×"), advBox));
    winRow->addWidget(m_height);
    winRow->addWidget(m_fullscreen);
    winRow->addStretch(1);
    advLay->addLayout(winRow);
    auto *closeRow = new QHBoxLayout();
    closeRow->addWidget(new QLabel(tr("While playing"), advBox));
    m_closeBox = new QComboBox(advBox);
    m_closeBox->addItem(tr("Keep launcher open"), QStringLiteral("keep"));
    m_closeBox->addItem(tr("Minimize launcher"), QStringLiteral("minimize"));
    m_closeBox->addItem(tr("Close launcher (reopens after)"), QStringLiteral("close"));
    m_closeBox->setCurrentIndex(qMax(0, m_closeBox->findData(m_settings->closeBehavior())));
    connect(m_closeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { m_settings->setCloseBehavior(m_closeBox->itemData(i).toString()); });
    closeRow->addWidget(m_closeBox);
    closeRow->addStretch(1);
    advLay->addLayout(closeRow);
    detail->addWidget(advBox);
    detail->addStretch(1);

    split->addWidget(m_list);
    split->addWidget(detailW);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("secondary"));
    m_status->setWordWrap(true);
    outer->addWidget(m_status);

    connect(m_api, &MojangApi::manifestChanged, this, &VersionsPage::reloadFromCache);
    connect(m_accounts, &AccountStore::changed, this, &VersionsPage::onSelectionChanged);
    connect(m_accounts, &AccountStore::activeAccountChanged, this, &VersionsPage::onSelectionChanged);
    connect(m_demo, &QCheckBox::toggled, this, &VersionsPage::onSelectionChanged);
    reloadFromCache();
    updateMemoryLabel();
}

QString VersionsPage::selectedVersion() const
{
    auto *it = m_list->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

bool VersionsPage::demoRequested() const
{
    return m_demo->isChecked();
}

int VersionsPage::launchWidth() const
{
    return m_width->value();
}

int VersionsPage::launchHeight() const
{
    return m_height->value();
}

bool VersionsPage::launchFullscreen() const
{
    return m_fullscreen->isChecked();
}

void VersionsPage::setStatus(const QString &s, bool isError)
{
    m_status->setText(s);
    m_status->setStyleSheet(isError ? QStringLiteral("color: #EF6461;") : QString());
}

QString VersionsPage::describe(const VersionEntry &e, bool installed) const
{
    QString date;
    QDateTime dt = QDateTime::fromString(e.releaseTime, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(e.releaseTime, Qt::ISODate);
    }
    date = dt.isValid() ? QLocale::system().toString(dt.date(), QLocale::ShortFormat) : e.releaseTime.left(10);
    return tr("%1  ·  %2  ·  %3%4")
        .arg(e.id)
        .arg(e.type)
        .arg(date)
        .arg(installed ? tr("  ·  installed") : QString());
}

void VersionsPage::reloadFromCache()
{
    const QString keep = selectedVersion();
    bool ok = false;
    const VersionManifest m = m_api->cachedManifest(&ok);
    m_all = ok ? m.versions : QList<VersionEntry>{};
    onSearchChanged(m_search->text());
    if (!ok) {
        if (NetworkStatus::instance().isEffectivelyOffline()) {
            setStatus(tr("You're offline and no version list is cached yet. Go online once to fetch it."));
        } else {
            setStatus(tr("No version list yet — press Refresh."));
        }
    } else if (!m_didAutoRefresh && !NetworkStatus::instance().isEffectivelyOffline()) {
        m_didAutoRefresh = true;
        onRefresh(); // background refresh on first show
    }
    // Restore selection.
    if (!keep.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == keep) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}

void VersionsPage::onSearchChanged(const QString &text)
{
    QSet<QString> types;
    if (m_fReleases->isChecked()) {
        types.insert(QStringLiteral("release"));
    }
    if (m_fSnapshots->isChecked()) {
        types.insert(QStringLiteral("snapshot"));
    }
    if (m_fBeta->isChecked()) {
        types.insert(QStringLiteral("old_beta"));
    }
    if (m_fAlpha->isChecked()) {
        types.insert(QStringLiteral("old_alpha"));
    }
    const auto shown = MojangApi::filterEntries(m_all, types, text);
    m_list->blockSignals(true);
    m_list->clear();
    for (const auto &e : shown) {
        const bool installed = m_installer->quickIsInstalled(e.id);
        auto *it = new QListWidgetItem(describe(e, installed), m_list);
        it->setData(Qt::UserRole, e.id);
        if (installed) {
            it->setIcon(IconProvider::instance().icon(QStringLiteral("check")));
        }
    }
    m_list->blockSignals(false);
    if (m_list->count() > 0 && m_list->currentRow() < 0) {
        m_list->setCurrentRow(0);
    } else {
        onSelectionChanged();
    }
    setStatus(m_all.isEmpty() ? m_status->text()
                              : tr("%1 versions shown (%2 cached).").arg(m_list->count()).arg(m_all.size()));
}

void VersionsPage::onFilterToggled()
{
    m_settings->setShowReleases(m_fReleases->isChecked());
    m_settings->setShowSnapshots(m_fSnapshots->isChecked());
    m_settings->setShowBeta(m_fBeta->isChecked());
    m_settings->setShowAlpha(m_fAlpha->isChecked());
    onSearchChanged(m_search->text());
}

void VersionsPage::onRefresh()
{
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        setStatus(tr("You're offline — showing cached versions."), true);
        return;
    }
    m_refreshBtn->setEnabled(false);
    setStatus(tr("Refreshing version list…"));
    Task *t = m_api->refreshManifestTask(this);
    connect(t, &Task::finished, this, [this](bool ok) {
        m_refreshBtn->setEnabled(true);
        if (!ok) {
            setStatus(sender() ? qobject_cast<Task *>(sender())->errorString() : tr("Refresh failed."), true);
        } else {
            setStatus(tr("Version list is up to date."));
        }
    });
    t->start();
}

void VersionsPage::onSelectionChanged()
{
    const QString id = selectedVersion();
    if (id.isEmpty()) {
        m_detail->setText(tr("Select a version to see details."));
        m_installBtn->setEnabled(false);
        m_playBtn->setEnabled(false);
        return;
    }
    emit versionSelected(id);
    const bool installed = m_installer->quickIsInstalled(id);
    bool ok = false;
    const VersionManifest m = m_api->cachedManifest(&ok);
    QString type, date;
    if (ok) {
        for (const auto &e : m.versions) {
            if (e.id == id) {
                type = e.type;
                date = e.releaseTime.left(10);
                break;
            }
        }
    }
    // Required Java (from cached version JSON when available).
    QString javaLine;
    bool vok = false;
    const ParsedVersion pv = m_api->loadMergedVersion(id, &vok, nullptr);
    if (vok) {
        const int need = JavaManager::requiredMajorFor(pv.javaMajor);
        const JavaInfo have = m_java->findForMajor(need);
        javaLine = have.valid ? tr("Java %1 ready (%2)").arg(need).arg(have.path)
                              : tr("Needs Java %1 (will download on install)").arg(need);
    } else if (!installed) {
        javaLine = tr("Version info downloads on install.");
    }
    m_detail->setText(tr("<b>%1</b><br>Type: %2<br>Released: %3<br>%4<br>%5")
                          .arg(id)
                          .arg(type.isEmpty() ? tr("unknown") : type)
                          .arg(date.isEmpty() ? tr("unknown") : date)
                          .arg(installed ? tr("Installed — playable offline.") : tr("Not installed yet."))
                          .arg(javaLine));
    m_installBtn->setEnabled(true);
    m_installBtn->setText(installed ? tr("Reinstall") : tr("Install"));
    const bool canPlay = m_accounts->activeAccount().isValid() || m_demo->isChecked();
    m_playBtn->setEnabled(canPlay);
}

void VersionsPage::onInstall()
{
    const QString id = selectedVersion();
    if (id.isEmpty()) {
        return;
    }
    // Route through MainWindow so progress + errors share one pipeline.
    emit playRequested(QStringLiteral("install:") + id);
}

void VersionsPage::onPlayClicked()
{
    const QString id = selectedVersion();
    if (id.isEmpty()) {
        return;
    }
    if (!m_accounts->activeAccount().isValid() && !m_demo->isChecked()) {
        QMessageBox::information(this, tr("Pick an account"),
                                 tr("Select an account in Accounts first — or tick Demo mode to try "
                                    "the official demo with no account."));
        return;
    }
    emit playRequested(id);
}

void VersionsPage::updateMemoryLabel()
{
    const int mb = m_memory->value();
    m_settings->setMemoryMb(mb == JavaManager::suggestMemoryMb(JavaManager::systemRamMb(), 0) ? 0 : mb);
    const bool rec = JavaManager::isRecommendedMemory(mb, JavaManager::systemRamMb(), 0);
    m_memoryLabel->setText(tr("%1 MB%2").arg(mb).arg(rec ? tr(" — Recommended") : QString()));
}
