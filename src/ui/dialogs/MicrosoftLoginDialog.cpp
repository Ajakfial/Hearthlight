#include "dialogs/MicrosoftLoginDialog.h"

#include "AccountProvider.h"
#include "AccountStore.h"
#include "Constants.h"
#include "Logger.h"
#include "MicrosoftAuth.h"
#include "SecureTokenStore.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

MicrosoftLoginDialog::MicrosoftLoginDialog(AccountStore *store, const QString &dataDir, SecureTokenStore *tokens,
                                           QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_dataDir(dataDir)
    , m_tokens(tokens)
{
    setWindowTitle(tr("Sign in with Microsoft"));
    setModal(true);
    setMinimumWidth(480);

    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Hearthlight opens a Microsoft page where you approve the sign-in. "
                               "Hearthlight never sees your password."),
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    m_codeLabel = new QLabel(tr("Getting a sign-in code…"), this);
    QFont cf = m_codeLabel->font();
    cf.setPointSize(22);
    cf.setBold(true);
    m_codeLabel->setFont(cf);
    m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_codeLabel);

    auto *btnRow = new QHBoxLayout();
    auto *openBtn = new QPushButton(tr("Open browser"), this);
    connect(openBtn, &QPushButton::clicked, this, &MicrosoftLoginDialog::onOpenBrowser);
    btnRow->addWidget(openBtn);
    auto *copyBtn = new QPushButton(tr("Copy code"), this);
    connect(copyBtn, &QPushButton::clicked, this, &MicrosoftLoginDialog::onCopyCode);
    btnRow->addWidget(copyBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    m_status = new QLabel(tr("Contacting Microsoft…"), this);
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("secondary"));
    lay->addWidget(m_status);

    m_detailLabel = new QLabel(this);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setObjectName(QStringLiteral("secondary"));
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_detailLabel);

    // Optional browser auth-code flow (for kiosks where device codes fail).
    auto *alt = new QLabel(tr("Trouble with the code? Use the browser flow instead:"), this);
    alt->setObjectName(QStringLiteral("secondary"));
    lay->addWidget(alt);
    auto *codeRow = new QHBoxLayout();
    m_browserCode = new QLineEdit(this);
    m_browserCode->setPlaceholderText(tr("Paste the code from the browser here"));
    codeRow->addWidget(m_browserCode, 1);
    auto *altBtn = new QPushButton(tr("Use browser code"), this);
    connect(altBtn, &QPushButton::clicked, this, &MicrosoftLoginDialog::onTryBrowserFlow);
    codeRow->addWidget(altBtn);
    auto *redeemBtn = new QPushButton(tr("Sign in"), this);
    connect(redeemBtn, &QPushButton::clicked, this, &MicrosoftLoginDialog::onRedeemBrowserCode);
    codeRow->addWidget(redeemBtn);
    lay->addLayout(codeRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        if (m_auth) {
            m_auth->cancelPending();
        }
        reject();
    });

    m_auth = new MicrosoftAuth(this);
    connect(m_auth, &MicrosoftAuth::deviceCodeReady, this, &MicrosoftLoginDialog::onDeviceCode);
    connect(m_auth, &MicrosoftAuth::loginFinished, this, &MicrosoftLoginDialog::onLoginFinished);
    connect(m_auth, &MicrosoftAuth::statusMessage, this, &MicrosoftLoginDialog::onStatus);
    startDeviceFlow();
}

void MicrosoftLoginDialog::startDeviceFlow()
{
    m_busy = true;
    m_status->setText(tr("Contacting Microsoft…"));
    m_auth->startDeviceCode(QString::fromLatin1(Hearthlight::kAzureClientId));
}

void MicrosoftLoginDialog::onDeviceCode(const DeviceCodeInfo &dc)
{
    m_codeLabel->setText(dc.userCode);
    m_verifyUrl = !dc.verificationUriComplete.isEmpty() ? dc.verificationUriComplete : dc.verificationUri;
    if (m_verifyUrl.isEmpty()) {
        m_verifyUrl = QStringLiteral("https://www.microsoft.com/link");
    }
    m_status->setText(tr("Enter this code in your browser, then approve the sign-in. This window continues by itself."));
    QDesktopServices::openUrl(QUrl(m_verifyUrl));
}

void MicrosoftLoginDialog::onStatus(const QString &msg)
{
    m_status->setText(msg);
}

void MicrosoftLoginDialog::onOpenBrowser()
{
    if (!m_verifyUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_verifyUrl));
    }
}

void MicrosoftLoginDialog::onCopyCode()
{
    QGuiApplication::clipboard()->setText(m_codeLabel->text());
    m_status->setText(tr("Code copied — paste it in the Microsoft page."));
}

void MicrosoftLoginDialog::onTryBrowserFlow()
{
    const QString state = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString redirect = QStringLiteral("https://login.microsoftonline.com/common/oauth2/nativeclient");
    const QString url = MicrosoftAuth::browserAuthorizeUrl(QString::fromLatin1(Hearthlight::kAzureClientId),
                                                           redirect, state);
    QDesktopServices::openUrl(QUrl(url));
    m_status->setText(tr("A browser page opened. Sign in there, copy the resulting code, paste it below, then press Sign in."));
    m_detailLabel->setText(tr("Redirect: %1").arg(redirect));
}

void MicrosoftLoginDialog::onRedeemBrowserCode()
{
    const QString code = m_browserCode->text().trimmed();
    if (code.isEmpty()) {
        m_status->setText(tr("Paste the browser code first."));
        return;
    }
    m_status->setText(tr("Checking with Microsoft…"));
    const QString redirect = QStringLiteral("https://login.microsoftonline.com/common/oauth2/nativeclient");
    m_auth->redeemAuthCode(QString::fromLatin1(Hearthlight::kAzureClientId), code, redirect);
}

void MicrosoftLoginDialog::fail(const QString &error, const QString &details)
{
    m_busy = false;
    m_status->setText(error);
    m_detailLabel->setText(details.left(800));
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Couldn't sign in"));
    box.setText(error);
    if (!details.isEmpty()) {
        box.setDetailedText(Logger::redacted(details));
    }
    box.addButton(QMessageBox::Ok);
    box.exec();
}

void MicrosoftLoginDialog::succeed(const MicrosoftLoginResult &r)
{
    // Upsert the AccountStore entry (stable local id by player UUID).
    Account a;
    a.type = AccountType::Microsoft;
    a.username = r.profile.username;
    a.uuid = QUuid::fromString(r.profile.uuid);
    a.displayName = r.profile.username;
    a.id = a.uuid.toString(QUuid::WithoutBraces).toLower();
    QString err;
    Account existing = m_store->accountById(a.id);
    if (existing.isValid()) {
        existing.username = a.username;
        existing.displayName = a.displayName;
        existing.uuid = a.uuid;
        if (!m_store->updateAccount(existing, &err)) {
            fail(err, {});
            return;
        }
    } else {
        if (!m_store->addAccount(a, &err)) {
            fail(err, {});
            return;
        }
    }
    m_store->setActiveAccountId(a.id);
    // Refresh token -> OS credential store. Minecraft token stays in memory
    // (the provider cache refreshes it before each launch).
    if (m_tokens && !r.msRefreshToken.isEmpty()) {
        if (!m_tokens->setToken(a.id, QStringLiteral("msRefresh"), r.msRefreshToken)) {
            Logger::warning(QStringLiteral("Could not save Microsoft credentials in %1.")
                                .arg(m_tokens->backendName()));
        }
    }
    MicrosoftExtras ex = loadMicrosoftExtras(m_dataDir, a.id);
    ex.accountId = a.id;
    ex.username = r.profile.username;
    ex.uuid = r.profile.uuid;
    ex.skins = r.profile.skins;
    ex.capes = r.profile.capes;
    ex.hasOwnership = r.hasOwnership;
    ex.lastChecked = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    saveMicrosoftExtras(m_dataDir, ex);
    m_createdId = a.id;
    m_busy = false;
    accept();
}

void MicrosoftLoginDialog::onLoginFinished(const MicrosoftLoginResult &r)
{
    if (r.ok) {
        succeed(r);
        return;
    }
    fail(r.error.isEmpty() ? tr("Sign-in failed.") : r.error, Logger::redacted(r.errorDetails));
    // Leave the dialog open so the user can retry without losing the code.
}

#include "MicrosoftLoginDialog.moc"
