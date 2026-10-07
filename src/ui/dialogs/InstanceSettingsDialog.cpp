#include "dialogs/InstanceSettingsDialog.h"

#include "AccountStore.h"
#include "JavaManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

InstanceSettingsDialog::InstanceSettingsDialog(const Instance &in, AccountStore *accounts, QWidget *parent)
    : QDialog(parent)
    , m_result(in)
    , m_accounts(accounts)
{
    setWindowTitle(tr("Settings — %1").arg(in.name));
    setModal(true);
    resize(560, 520);

    auto *lay = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    lay->addWidget(tabs, 1);

    // --- General ---
    auto *g = new QWidget(tabs);
    auto *gf = new QFormLayout(g);
    m_accountMode = new QComboBox(g);
    m_accountMode->addItem(tr("Whoever is selected when I press Play"), (int)InstanceAccountMode::Current);
    m_accountMode->addItem(tr("A specific account"), (int)InstanceAccountMode::Specific);
    m_accountMode->addItem(tr("Ask every launch"), (int)InstanceAccountMode::Ask);
    m_accountMode->setCurrentIndex((int)in.accountMode);
    gf->addRow(tr("Account"), m_accountMode);
    m_accountId = new QComboBox(g);
    for (const auto &a : m_accounts->accounts()) {
        m_accountId->addItem(QStringLiteral("%1  [%2]").arg(a.username).arg(accountTypeBadge(a.type)), a.id);
    }
    const int ai = m_accountId->findData(in.accountId);
    if (ai >= 0) {
        m_accountId->setCurrentIndex(ai);
    }
    gf->addRow(tr("Specific account"), m_accountId);
    m_group = new QLineEdit(in.group, g);
    m_group->setPlaceholderText(tr("Collection, e.g. Modpacks"));
    gf->addRow(tr("Collection"), m_group);
    m_gameDir = new QLineEdit(in.gameDirOverride, g);
    m_gameDir->setPlaceholderText(tr("Empty = inside the profile folder"));
    gf->addRow(tr("Game folder override"), m_gameDir);
    tabs->addTab(g, tr("General"));

    // --- Java / performance (Advanced, but plain language) ---
    auto *j = new QWidget(tabs);
    auto *jf = new QFormLayout(j);
    auto *javaRow = new QHBoxLayout();
    m_java = new QLineEdit(in.javaPath, j);
    m_java->setPlaceholderText(tr("Empty = automatic"));
    javaRow->addWidget(m_java, 1);
    auto *pick = new QPushButton(tr("Browse…"), j);
    connect(pick, &QPushButton::clicked, this, &InstanceSettingsDialog::onPickJava);
    javaRow->addWidget(pick);
    auto *test = new QPushButton(tr("Test"), j);
    connect(test, &QPushButton::clicked, this, &InstanceSettingsDialog::onTestJava);
    javaRow->addWidget(test);
    jf->addRow(tr("Java path"), javaRow);
    m_memory = new QSpinBox(j);
    m_memory->setRange(0, 32768);
    m_memory->setValue(in.memoryMb);
    m_memory->setSpecialValueText(tr("Automatic (Recommended)"));
    m_memory->setSuffix(tr(" MB"));
    jf->addRow(tr("Memory"), m_memory);
    m_jvm = new QLineEdit(in.extraJvmArgs, j);
    m_jvm->setPlaceholderText(tr("Extra JVM arguments (Advanced)"));
    jf->addRow(tr("JVM args"), m_jvm);
    auto *winRow = new QHBoxLayout();
    m_width = new QSpinBox(j);
    m_width->setRange(0, 7680);
    m_width->setValue(in.width);
    m_width->setSpecialValueText(tr("Default"));
    m_height = new QSpinBox(j);
    m_height->setRange(0, 4320);
    m_height->setValue(in.height);
    m_height->setSpecialValueText(tr("Default"));
    m_full = new QCheckBox(tr("Fullscreen"), j);
    m_full->setChecked(in.fullscreen);
    winRow->addWidget(m_width);
    winRow->addWidget(m_height);
    winRow->addWidget(m_full);
    jf->addRow(tr("Window"), winRow);
    tabs->addTab(j, tr("Java & Window"));

    // --- Commands / env (Advanced) ---
    auto *c = new QWidget(tabs);
    auto *cf = new QFormLayout(c);
    m_pre = new QLineEdit(in.preLaunchCommand.join(QLatin1Char(' ')), c);
    cf->addRow(tr("Before launch"), m_pre);
    m_post = new QLineEdit(in.postExitCommand.join(QLatin1Char(' ')), c);
    cf->addRow(tr("After exit"), m_post);
    m_env = new QPlainTextEdit(c);
    QStringList envLines;
    for (auto it = in.envVars.begin(); it != in.envVars.end(); ++it) {
        envLines.append(it.key() + QLatin1Char('=') + it.value());
    }
    m_env->setPlainText(envLines.join(QLatin1Char('\n')));
    m_env->setPlaceholderText(tr("One per line: NAME=value"));
    cf->addRow(tr("Environment"), m_env);
    tabs->addTab(c, tr("Advanced"));

    // --- Quick Play ---
    auto *q = new QWidget(tabs);
    auto *qf = new QFormLayout(q);
    m_qpWorld = new QLineEdit(in.quickPlayWorld, q);
    m_qpWorld->setPlaceholderText(tr("Single-player world name"));
    qf->addRow(tr("World"), m_qpWorld);
    m_qpServer = new QLineEdit(in.quickPlayServer, q);
    m_qpServer->setPlaceholderText(tr("Server address, e.g. play.example.com"));
    qf->addRow(tr("Server"), m_qpServer);
    m_qpRealm = new QLineEdit(in.quickPlayRealm, q);
    qf->addRow(tr("Realm ID"), m_qpRealm);
    auto *hint = new QLabel(tr("Quick Play jumps straight in on launch (supported versions only)."), q);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("secondary"));
    qf->addRow(hint);
    tabs->addTab(q, tr("Quick Play"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_result.accountMode = (InstanceAccountMode)m_accountMode->currentData().toInt();
        m_result.accountId = m_accountId->currentData().toString();
        m_result.group = m_group->text().trimmed();
        m_result.gameDirOverride = m_gameDir->text().trimmed();
        m_result.javaPath = m_java->text().trimmed();
        m_result.memoryMb = m_memory->value();
        m_result.extraJvmArgs = m_jvm->text();
        m_result.width = m_width->value();
        m_result.height = m_height->value();
        m_result.fullscreen = m_full->isChecked();
        m_result.preLaunchCommand = m_pre->text().trimmed().isEmpty()
            ? QStringList{}
            : m_pre->text().trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m_result.postExitCommand = m_post->text().trimmed().isEmpty()
            ? QStringList{}
            : m_post->text().trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m_result.envVars.clear();
        for (const auto &line : m_env->toPlainText().split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                m_result.envVars.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
            }
        }
        m_result.quickPlayWorld = m_qpWorld->text().trimmed();
        m_result.quickPlayServer = m_qpServer->text().trimmed();
        m_result.quickPlayRealm = m_qpRealm->text().trimmed();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_accountMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InstanceSettingsDialog::onAccountModeChanged);
    onAccountModeChanged(m_accountMode->currentIndex());
}

void InstanceSettingsDialog::onAccountModeChanged(int idx)
{
    Q_UNUSED(idx);
    const bool specific = m_accountMode->currentData().toInt() == (int)InstanceAccountMode::Specific;
    m_accountId->setEnabled(specific);
}

void InstanceSettingsDialog::onPickJava()
{
    const QString p = QFileDialog::getOpenFileName(this, tr("Choose a Java executable"));
    if (!p.isEmpty()) {
        m_java->setText(p);
    }
}

void InstanceSettingsDialog::onTestJava()
{
    JavaInfo info;
    QString out;
    const bool ok = JavaManager::testJava(m_java->text().trimmed().isEmpty() ? QStringLiteral("java") : m_java->text().trimmed(), &info, &out);
    QMessageBox::information(this, tr("Test Java"),
                             ok ? tr("Works: Java %1 (%2)").arg(info.major).arg(info.path)
                                : tr("That Java didn't pass its self-test.\n\n%1").arg(out));
}

#include "InstanceSettingsDialog.moc"
