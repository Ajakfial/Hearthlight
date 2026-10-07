#pragma once

#include <QWidget>

class AccountStore;
class Embers;
class Hearthstones;
class InstanceManager;
class ModManager;
class ModrinthApi;
class QLabel;
class QPushButton;

// The Hearth — calm home dashboard (spec 10.1): big Play for the
// last-played profile, recent worlds, playtime summary, and one gentle
// suggestion (mod updates / undo availability / getting-started hint).
class HearthPage : public QWidget {
    Q_OBJECT
public:
    HearthPage(AccountStore *accounts, InstanceManager *instances, Hearthstones *stones, ModrinthApi *modrinth,
               ModManager *mods, Embers *embers, const QString &dataDir, QWidget *parent = nullptr);

signals:
    void playRequested(const QString &instanceId);
    void reviewMods(const QString &instanceId);
    void undoRequested(const QString &instanceId);
    void browseProfiles();

public slots:
    void refresh();

private slots:
    void onPlayLast();
    void onCheckUpdates();
    void onUndo();

private:
    QString playtimeText() const;

    AccountStore *m_accounts = nullptr;
    InstanceManager *m_instances = nullptr;
    Hearthstones *m_stones = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    ModManager *m_mods = nullptr;
    Embers *m_embers = nullptr;
    QString m_dataDir;

    QLabel *m_greeting = nullptr;
    QPushButton *m_play = nullptr;
    QLabel *m_playSub = nullptr;
    QLabel *m_worlds = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_suggest = nullptr;
    QPushButton *m_suggestBtn = nullptr;
    QPushButton *m_undoBtn = nullptr;
    QString m_lastId;
    QString m_updateInstance;
    int m_updateCount = 0;
    bool m_checkingUpdates = false;
};
