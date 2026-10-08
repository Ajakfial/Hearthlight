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

// Per-profile content dialog: Mods tab (icon/name/version list, toggle
// on/off, remove, open folder, update badges + update-all, manual detection,
// rollback through Embers) plus Resource packs / Shader packs / Data packs
// tabs (zip on/off toggles, add by hand, remove, open folder).
class InstalledModsDialog : public QDialog {
    Q_OBJECT
public:
    InstalledModsDialog(const QString &instanceId, InstanceManager *instances, ModManager *mods,
                        ModrinthApi *modrinth, Embers *embers, const QString &dataDir, QWidget *parent = nullptr);

private slots:
    void rebuild();
    void rebuildPacks();
    void onToggle();
    void onRemove();
    void onOpenFolder();
    void onCheckUpdates();
    void onUpdateAll();
    void onUndo();
    void onSelectionChanged();
    void onPackTabChanged(int idx);
    void onPackAdd();
    void onPackToggle();
    void onPackRemove();
    void onPackOpenFolder();

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

    class QTabWidget *m_tabs = nullptr;
    QListWidget *m_packsList = nullptr;
    QListWidget *m_shadersList = nullptr;
    QListWidget *m_dataList = nullptr;
    QLabel *m_packHint = nullptr;

    QString currentPackFolder() const;
    QListWidget *currentPackList() const;
};
