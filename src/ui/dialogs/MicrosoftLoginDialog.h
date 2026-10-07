#pragma once

#include <QDialog>

class AccountStore;
class QLabel;
class QLineEdit;
class MicrosoftAuth;
struct DeviceCodeInfo;

// Microsoft sign-in dialog (spec 6.1):
// Device-code flow by default (code + "Open browser" + polling), with an
// optional browser auth-code flow for environments where device codes fail.
// On success the dialog creates/updates the AccountStore entry, stores the
// refresh token in SecureTokenStore, and saves non-secret extras.
class MicrosoftLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit MicrosoftLoginDialog(AccountStore *store, const QString &dataDir, class SecureTokenStore *tokens,
                                  QWidget *parent = nullptr);
    QString createdAccountId() const { return m_createdId; }

private slots:
    void onDeviceCode(const DeviceCodeInfo &dc);
    void onLoginFinished(const struct MicrosoftLoginResult &r);
    void onStatus(const QString &msg);
    void onOpenBrowser();
    void onCopyCode();
    void onTryBrowserFlow();
    void onRedeemBrowserCode();

private:
    void startDeviceFlow();
    void fail(const QString &error, const QString &details);
    void succeed(const struct MicrosoftLoginResult &r);

    AccountStore *m_store = nullptr;
    QString m_dataDir;
    class SecureTokenStore *m_tokens = nullptr;
    MicrosoftAuth *m_auth = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_codeLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLineEdit *m_browserCode = nullptr;
    QString m_verifyUrl;
    QString m_createdId;
    bool m_busy = false;
};
