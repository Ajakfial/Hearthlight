#pragma once

#include "Account.h"

#include <QList>
#include <QObject>
#include <QString>

// Owns all accounts (Microsoft + Offline through one API) and persists the
// local metadata file:
//
//   <dataDir>/accounts/accounts.json
//   <dataDir>/accounts/avatars/
//
// Rules:
//  - atomic writes (QSaveFile), schema validation on load
//  - corrupted files are quarantined to *.corrupt-<stamp>.bak, never crash
//  - duplicate identities rejected (same UUID, or same offline username
//    case-insensitively)
//  - never stores secrets: Account::fromJson rejects token-bearing objects
class AccountStore : public QObject {
    Q_OBJECT
public:
    explicit AccountStore(const QString &dataDir, QObject *parent = nullptr);

    QString accountsFilePath() const;

    bool load(); // called at startup; emits loaded()
    bool save(); // atomic; emits changed() on success

    QList<Account> accounts() const { return m_accounts; }
    Account accountById(const QString &id) const;
    bool hasAccount(const QString &id) const;

    // Takes ownership-style add: validates + rejects duplicates.
    bool addAccount(const Account &a, QString *error = nullptr);
    bool updateAccount(const Account &a, QString *error = nullptr);
    bool removeAccount(const QString &id);
    bool hasDuplicate(const Account &a) const;

    QString activeAccountId() const { return m_activeId; }
    Account activeAccount() const { return accountById(m_activeId); }
    void setActiveAccountId(const QString &id);

    QList<Account> offlineAccounts() const;
    QList<Account> microsoftAccounts() const;

signals:
    void loaded();
    void changed();
    void accountAdded(const QString &id);
    void accountRemoved(const QString &id);
    void activeAccountChanged(const QString &id);

private:
    QString m_dataDir;
    QList<Account> m_accounts;
    QString m_activeId;
};
