#pragma once

#include <QWidget>

class AccountStore;
class MicrosoftAccountProvider;
class SecureTokenStore;
class QLabel;
class QVBoxLayout;

// Accounts page: two prominent actions (Sign in with Microsoft / Create
// Offline Account) + account cards with avatar, username, type badge,
// UUID in Advanced details, selected indicator, Select/Edit/Remove.
// Microsoft cards additionally show skin/cape management (official API),
// refresh state, and sign-out. Offline cards show the local-profile notice
// and optional local skin file (Hearthlight-UI-only, never uploaded).
class AccountsPage : public QWidget {
    Q_OBJECT
public:
    explicit AccountsPage(AccountStore *store, const QString &dataDir, SecureTokenStore *tokens,
                          MicrosoftAccountProvider *microsoft, QWidget *parent = nullptr);

private slots:
    void rebuild();

private:
    QWidget *makeCard(const struct Account &a, bool selected);
    QWidget *makeMicrosoftExtra(const struct Account &a, QWidget *card);
    void onCreateOffline();
    void onMicrosoftClicked();
    void onEdit(const QString &id);
    void onRemove(const QString &id);
    void onMicrosoftRefresh(const QString &id);
    void onMicrosoftSignOut(const QString &id);
    void onChangeSkin(const QString &id);
    void onCapeChanged(const QString &id, int idx);
    void onLocalSkin(const QString &id);
    void downloadHead(const struct Account &a);

    AccountStore *m_store = nullptr;
    QString m_dataDir;
    SecureTokenStore *m_tokens = nullptr;
    MicrosoftAccountProvider *m_microsoft = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QLabel *m_status = nullptr;
};
