#include "SettingsPage.h"

#include "AccountStore.h"
#include "AppSettings.h"
#include "CurseForgeApi.h"
#include "DownloadManager.h"
#include "IconProvider.h"
#include "JavaManager.h"
#include "SecureTokenStore.h"
#include "Theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

SettingsPage::SettingsPage(AppSettings *settings, AccountStore *store, Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_store(store)
    , m_theme(theme)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 24);

    auto *title = new QLabel(tr("Settings"), this);
    QFont tf = title->font();
    tf.setPointSize(20);
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll);
    auto *lay = new QVBoxLayout(body);
    lay->setSpacing(14);

    // ---- General ----
    auto *general = new QGroupBox(tr("General"), body);
    auto *gf = new QFormLayout(general);
    m_offlineBox = new QComboBox(general);
    m_offlineBox->addItem(tr("Automatic"), QStringLiteral("automatic"));
    m_offlineBox->addItem(tr("Always Offline"), QStringLiteral("always-offline"));
    m_offlineBox->addItem(tr("Never Offline"), QStringLiteral("never-offline"));
    const QString cur = offlineModeToString(m_settings->offlineMode());
    const int idx = m_offlineBox->findData(cur);
    m_offlineBox->setCurrentIndex(idx < 0 ? 0 : idx);
    m_offlineBox->setToolTip(tr("Always Offline prevents network-dependent sign-in and optional requests."));
    connect(m_offlineBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::applyOfflineMode);
    gf->addRow(tr("Offline Mode"), m_offlineBox);

    auto *langBox = new QComboBox(general);
    langBox->addItem(tr("System default"), QStringLiteral("system"));
    langBox->addItem(tr("English"), QStringLiteral("en"));
    for (const auto &code : AppSettings::availableLanguages(m_settings->dataDir())) {
        langBox->addItem(AppSettings::displayNameForLanguage(code), code);
    }
    const QString langCur = m_settings->language().toLower();
    int langIdx = langBox->findData(langCur);
    if (langIdx < 0) {
        langIdx = langBox->findData(langCur.startsWith(QStringLiteral("en")) ? QStringLiteral("en")
                                                                              : QStringLiteral("system"));
    }
    langBox->setCurrentIndex(langIdx < 0 ? 0 : langIdx);
    langBox->setToolTip(tr("Community translations load from the translations folder as hearthlight_<code>.qm."));
    connect(langBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, langBox](int i) {
        m_settings->setLanguage(langBox->itemData(i).toString());
        QMessageBox::information(this, tr("Language"),
                                 tr("Restart Hearthlight to use the new language."));
    });
    gf->addRow(tr("Language"), langBox);

    auto *dataRow = new QHBoxLayout();
    auto *dataEdit = new QLineEdit(m_settings->dataDir(), general);
    dataEdit->setReadOnly(true);
    dataRow->addWidget(dataEdit, 1);
    auto *openBtn = new QPushButton(tr("Open folder"), general);
    connect(openBtn, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings->dataDir()));
    });
    dataRow->addWidget(openBtn);
    gf->addRow(tr("Data folder"), dataRow);

    m_closeBox = new QComboBox(general);
    m_closeBox->addItem(tr("Keep launcher open"), QStringLiteral("keep"));
    m_closeBox->addItem(tr("Minimize while playing"), QStringLiteral("minimize"));
    m_closeBox->addItem(tr("Close while playing (reopens after)"), QStringLiteral("close"));
    m_closeBox->setCurrentIndex(qMax(0, m_closeBox->findData(m_settings->closeBehavior())));
    connect(m_closeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::applyCloseBehavior);
    gf->addRow(tr("When launching a game"), m_closeBox);
    lay->addWidget(general);

    // ---- Appearance ----
    auto *app = new QGroupBox(tr("Appearance"), body);
    auto *af = new QFormLayout(app);
    auto *accentRow = new QHBoxLayout();
    auto *accentPrev = new QLabel(QStringLiteral("  "), app);
    accentPrev->setAutoFillBackground(true);
    auto setPrev = [accentPrev, this] {
        QPalette p = accentPrev->palette();
        p.setColor(QPalette::Window, m_theme->accent());
        accentPrev->setPalette(p);
    };
    setPrev();
    accentRow->addWidget(accentPrev);
    auto *accentBtn = new QPushButton(tr("Choose accent…"), app);
    connect(accentBtn, &QPushButton::clicked, this, &SettingsPage::pickAccent);
    accentRow->addWidget(accentBtn);
    connect(m_theme, &Theme::changed, this, [setPrev] { setPrev(); });
    af->addRow(tr("Accent (Play button + focus rings)"), accentRow);

    auto *scale = new QDoubleSpinBox(app);
    scale->setRange(0.8, 2.0);
    scale->setSingleStep(0.05);
    scale->setValue(m_settings->uiScale());
    connect(scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        m_settings->setUiScale(v);
        m_theme->setUiScale(v);
        m_theme->apply();
    });
    af->addRow(tr("UI scale"), scale);
    lay->addWidget(app);

    // ---- Java: detected JVMs + custom path + real test ----
    auto *java = new QGroupBox(tr("Java"), body);
    auto *jf = new QFormLayout(java);
    m_javaList = new QComboBox(java);
    m_javaList->setToolTip(tr("JVMs found on this computer right now (no internet needed)."));
    jf->addRow(tr("Detected"), m_javaList);
    m_javaStatus = new QLabel(java);
    m_javaStatus->setObjectName(QStringLiteral("secondary"));
    m_javaStatus->setWordWrap(true);
    jf->addRow(m_javaStatus);
    auto *javaRow = new QHBoxLayout();
    auto *javaEdit = new QLineEdit(m_settings->javaPath(), java);
    javaEdit->setPlaceholderText(tr("Custom Java (optional — auto-detect otherwise)"));
    javaRow->addWidget(javaEdit, 1);
    auto *browse = new QPushButton(tr("Browse…"), java);
    connect(browse, &QPushButton::clicked, this, [javaEdit, this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Choose Java executable"));
        if (!p.isEmpty()) {
            javaEdit->setText(p);
            m_settings->setJavaPath(p);
            refreshJavaList();
        }
    });
    javaRow->addWidget(browse);
    auto *testBtn = new QPushButton(tr("Test"), java);
    connect(testBtn, &QPushButton::clicked, this, &SettingsPage::testJava);
    javaRow->addWidget(testBtn);
    connect(javaEdit, &QLineEdit::editingFinished, this, [this, javaEdit] {
        m_settings->setJavaPath(javaEdit->text().trimmed());
        refreshJavaList();
    });
    jf->addRow(tr("Custom Java (profiles can override this)"), javaRow);
    auto *javaNote = new QLabel(tr("Empty = auto-detect, or download Eclipse Temurin automatically when a "
                                   "version needs it. Existing runtimes always work offline."),
                                java);
    javaNote->setObjectName(QStringLiteral("secondary"));
    javaNote->setWordWrap(true);
    jf->addRow(javaNote);
    lay->addWidget(java);

    // ---- Downloads ----
    auto *dl = new QGroupBox(tr("Downloads"), body);
    auto *df = new QFormLayout(dl);
    auto *par = new QSpinBox(dl);
    par->setRange(1, 32);
    par->setValue(m_settings->downloadParallelism());
    connect(par, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int n) { m_settings->setDownloadParallelism(n); });
    df->addRow(tr("Parallel downloads"), par);
    m_cacheLabel = new QLabel(dl);
    df->addRow(tr("Cache size"), m_cacheLabel);
    auto *clearBtn = new QPushButton(tr("Clear cache"), dl);
    connect(clearBtn, &QPushButton::clicked, this, &SettingsPage::clearCache);
    df->addRow(clearBtn);
    lay->addWidget(dl);

    // ---- Mod sources ----
    auto *src = new QGroupBox(tr("Mod sources"), body);
    auto *sf = new QFormLayout(src);
    m_cfKeyEdit = new QLineEdit(src);
    m_cfKeyEdit->setEchoMode(QLineEdit::Password);
    m_cfKeyEdit->setPlaceholderText(tr("Paste your CurseForge API key…"));
    m_cfKeyEdit->setToolTip(tr("Stored in the OS credential store, never in plain files."));
    sf->addRow(tr("CurseForge API key"), m_cfKeyEdit);
    auto *cfRow = new QHBoxLayout();
    auto *cfSave = new QPushButton(tr("Save key"), src);
    connect(cfSave, &QPushButton::clicked, this, &SettingsPage::saveCurseForgeKey);
    cfRow->addWidget(cfSave);
    auto *cfClear = new QPushButton(tr("Remove key"), src);
    connect(cfClear, &QPushButton::clicked, this, &SettingsPage::clearCurseForgeKey);
    cfRow->addWidget(cfClear);
    auto *cfTest = new QPushButton(tr("Refresh status"), src);
    connect(cfTest, &QPushButton::clicked, this, &SettingsPage::refreshCurseForgeKeyState);
    cfRow->addWidget(cfTest);
    cfRow->addStretch(1);
    sf->addRow(cfRow);
    m_cfKeyState = new QLabel(src);
    m_cfKeyState->setObjectName(QStringLiteral("secondary"));
    m_cfKeyState->setWordWrap(true);
    sf->addRow(m_cfKeyState);
    auto *cfNote = new QLabel(tr("Optional. Needed only for full CurseForge pack imports — without it, "
                                 "remote files are listed as skipped. Get a free key at "
                                 "console.curseforge.com, then paste it here. The key lives in your "
                                 "OS credential store (Windows Credential Manager / macOS Keychain / "
                                 "Linux Secret Service), the same place Microsoft sign-in tokens live."),
                              src);
    cfNote->setObjectName(QStringLiteral("secondary"));
    cfNote->setWordWrap(true);
    cfNote->setOpenExternalLinks(true);
    sf->addRow(cfNote);
    lay->addWidget(src);

    // ---- Privacy ----
    auto *priv = new QGroupBox(tr("Privacy"), body);
    auto *pl = new QVBoxLayout(priv);
    auto *privNote = new QLabel(tr("Hearthlight collects no telemetry and has no accounts of its own. "
                                   "Your computer contacts outside services only when you use them: Mojang "
                                   "for game files, Microsoft/Xbox for sign-in, Modrinth for browsing mods, "
                                   "and Eclipse Temurin for Java runtimes."),
                                priv);
    privNote->setObjectName(QStringLiteral("secondary"));
    privNote->setWordWrap(true);
    pl->addWidget(privNote);
    lay->addWidget(priv);

    // ---- About ----
    auto *about = new QGroupBox(tr("About"), body);
    auto *al = new QVBoxLayout(about);
    auto *ver = new QLabel(tr("Hearthlight %1 — Your home for every world.").arg(QStringLiteral("0.1.0")), about);
    QFont vf = ver->font();
    vf.setBold(true);
    ver->setFont(vf);
    al->addWidget(ver);
    auto *dis = new QLabel(tr("Hearthlight is not an official Minecraft product, and is not approved by "
                              "or associated with Mojang or Microsoft."),
                           about);
    dis->setWordWrap(true);
    dis->setObjectName(QStringLiteral("secondary"));
    al->addWidget(dis);
    auto *lic = new QLabel(tr("License: MIT (see LICENSE). Icons: hand-drawn SVG, no Mojang assets."), about);
    lic->setObjectName(QStringLiteral("secondary"));
    lic->setWordWrap(true);
    al->addWidget(lic);
    auto *links = new QLabel(tr("Modrinth API · Adoptium Temurin · Minecraft Wiki"), about);
    links->setObjectName(QStringLiteral("secondary"));
    al->addWidget(links);
    lay->addWidget(about);

    lay->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    // Cache label refresh.
    const auto cacheDir = QDir(m_settings->dataDir()).filePath(QStringLiteral("cache/downloads"));
    const double mb = DownloadManager::cacheSize(cacheDir) / 1024.0 / 1024.0;
    m_cacheLabel->setText(tr("%1 MB in %2").arg(QString::number(mb, 'f', 1)).arg(cacheDir));
}

void SettingsPage::applyOfflineMode(int idx)
{
    bool ok = false;
    const OfflineMode m = offlineModeFromString(m_offlineBox->itemData(idx).toString(), &ok);
    if (ok) {
        m_settings->setOfflineMode(m);
    }
}

void SettingsPage::pickAccent()
{
    const QColor c = QColorDialog::getColor(m_theme->accent(), this, tr("Accent color"));
    if (c.isValid()) {
        m_theme->setAccent(c);
        m_theme->apply();
        m_settings->setAccentColor(c.name());
    }
}

void SettingsPage::testJava()
{
    QString java = m_settings->javaPath().trimmed();
    if (java.isEmpty()) {
        java = QStringLiteral("java"); // PATH lookup
    }
    QProcess p(this);
    p.start(java, { QStringLiteral("-version") });
    if (!p.waitForFinished(10000)) {
        QMessageBox::warning(this, tr("Java test"), tr("Could not run “%1” (%2).").arg(java).arg(p.errorString()));
        return;
    }
    const QString out = QString::fromLocal8Bit(p.readAllStandardError() + p.readAllStandardOutput());
    QMessageBox::information(this, tr("Java test"),
                             out.trimmed().isEmpty() ? tr("Java ran, but printed nothing.") : out.trimmed().left(1500));
}

void SettingsPage::clearCache()
{
    const auto cacheDir = QDir(m_settings->dataDir()).filePath(QStringLiteral("cache/downloads"));
    DownloadManager::clearCache(cacheDir);
    m_cacheLabel->setText(tr("0.0 MB in %1").arg(cacheDir));
}

void SettingsPage::setJavaManager(JavaManager *jm)
{
    m_java = jm;
    refreshJavaList();
}

void SettingsPage::setTokenStore(SecureTokenStore *tokens)
{
    m_tokens = tokens;
    refreshCurseForgeKeyState();
}

void SettingsPage::saveCurseForgeKey()
{
    if (!m_tokens) {
        QMessageBox::warning(this, tr("Not ready"), tr("The credential store isn't ready yet."));
        return;
    }
    const QString key = m_cfKeyEdit ? m_cfKeyEdit->text().trimmed() : QString();
    if (key.isEmpty()) {
        QMessageBox::information(this, tr("CurseForge key"), tr("Paste a key first."));
        return;
    }
    if (!m_tokens->setToken(CurseForgeMeta::storeAccountId(), CurseForgeMeta::storeKey(), key)) {
        QMessageBox::warning(this, tr("Couldn't save"), tr("The key couldn't be stored."));
        return;
    }
    if (m_cfKeyEdit) {
        m_cfKeyEdit->clear();
    }
    refreshCurseForgeKeyState();
    QMessageBox::information(this, tr("Saved"),
                             tr("CurseForge key saved in %1.").arg(m_tokens->backendName()));
}

void SettingsPage::clearCurseForgeKey()
{
    if (!m_tokens) {
        return;
    }
    m_tokens->clearToken(CurseForgeMeta::storeAccountId(), CurseForgeMeta::storeKey());
    if (m_cfKeyEdit) {
        m_cfKeyEdit->clear();
    }
    refreshCurseForgeKeyState();
}

void SettingsPage::refreshCurseForgeKeyState()
{
    if (!m_cfKeyState) {
        return;
    }
    if (!m_tokens) {
        m_cfKeyState->setText(tr("Credential store not ready."));
        return;
    }
    const QString key = m_tokens->token(CurseForgeMeta::storeAccountId(), CurseForgeMeta::storeKey());
    if (key.isEmpty()) {
        m_cfKeyState->setText(tr("No key saved — CurseForge imports install local files only."));
    } else {
        m_cfKeyState->setText(tr("Key saved in %1 (…%2). Full CurseForge imports enabled.")
                                  .arg(m_tokens->backendName(), key.right(4)));
    }
}

void SettingsPage::refreshJavaList()
{
    if (!m_javaList) {
        return;
    }
    m_javaList->clear();
    if (!m_java) {
        m_javaList->addItem(tr("Java manager not ready"));
        return;
    }
    const auto found = m_java->detectAll(m_settings->javaPath());
    if (found.isEmpty()) {
        m_javaList->addItem(tr("None found — a runtime downloads automatically when needed"));
        m_javaStatus->setText(tr("No compatible Java detected. Stay online for the first install, "
                                 "or set a custom path above."));
        return;
    }
    for (const auto &j : found) {
        m_javaList->addItem(tr("Java %1 — %2%3")
                                .arg(j.major)
                                .arg(j.path)
                                .arg(j.managed ? tr(" (managed)") : QString()),
                            j.path);
    }
    m_javaStatus->setText(tr("%1 runtime(s) ready, all usable offline.").arg(found.size()));
}

void SettingsPage::applyCloseBehavior(int idx)
{
    m_settings->setCloseBehavior(m_closeBox->itemData(idx).toString());
}
