#pragma once

#include <QWidget>

class AccountStore;
class QComboBox;

// Top-bar account switcher: avatar + username + immediately understandable
// type badge (MICROSOFT / OFFLINE). Microsoft entries show the cached skin
// head when available; offline entries show their local tint avatar.
class AccountSwitcher : public QWidget {
    Q_OBJECT
public:
    explicit AccountSwitcher(AccountStore *store, const QString &dataDir, QWidget *parent = nullptr);

private slots:
    void rebuild();

private:
    AccountStore *m_store = nullptr;
    QString m_dataDir;
    QComboBox *m_box = nullptr;
};
