#pragma once

#include "Hearthstones.h"

#include <QDialog>

class InstanceManager;
class QCheckBox;
class QLineEdit;
class QListWidget;

// Hearthstones UI (spec 10.5): per-profile worlds list plus manual/automatic
// backups with restore and delete. Restores ask for confirmation because
// they overwrite the live world.
class WorldsDialog : public QDialog {
    Q_OBJECT
public:
    WorldsDialog(const QString &instanceId, InstanceManager *instances, Hearthstones *stones,
                 const QString &dataDir, QWidget *parent = nullptr);

private slots:
    void rebuild();
    void onBackup();
    void onRestore();
    void onDelete();
    void onOpenFolder();

private:
    static QString prettySize(qint64 bytes);
    static QString prettyTime(qint64 ms);

    QString m_instanceId;
    QString m_dataDir;
    InstanceManager *m_instances = nullptr;
    Hearthstones *m_stones = nullptr;

    QListWidget *m_worlds = nullptr;
    QListWidget *m_backups = nullptr;
    QLineEdit *m_name = nullptr;
    QCheckBox *m_config = nullptr;
};
