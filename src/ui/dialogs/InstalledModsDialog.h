#pragma once

#include "ModManager.h"

#include <QDialog>

class Embers;
class InstanceManager;
class ModrinthApi;
class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

// Per-profile installed-mods tab (spec 9): icon/name/version list, toggle
// on/off, remove, open folder, update badges + update-all, manual detection,
// and rollback through Embers snapshots.
class InstalledModsDialog : public QDialog {
    Q_OBJECT
public:
    InstalledModsDialog(const QString &instanceId, InstanceManager *instances, ModManager *mods,
                        ModrinthApi *modrinth, Embers *embers, const QString &dataDir, QWidget *parent = nullptr);

private slots:
    void rebuild();
    void onToggle();
    void onRemove();
    void onOpenFolder();
    void onCheckUpdates();
    void onUpdateAll();
    void onUndo();
    void onSelectionChanged();

private:
    QString instanceName() const;
    void setStatus(const QString &s);

    QString m_instanceId;
    QString m_dataDir;
    InstanceManager *m_instances = nullptr;
    ModManager *m_mods = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    Embers *m_embers = nullptr;

    QListWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_updates = nullptr;
    QList<ModUpdate> m_pending;
};
