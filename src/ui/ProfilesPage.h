#pragma once

#include <QWidget>

class AccountStore;
class InstanceManager;
class ModLoaderInstaller;
class MojangApi;
class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
struct GamePaths;

// Real profiles page (spec 7): cards with playtime/last-played/account/mods,
// New/Edit/Clone/Rename/Export/Delete (trash + confirm), drag-reorder,
// collections filter, per-instance account display, Quick Play badges,
// import/export (.hearthpack, .mrpack, CurseForge best-effort),
// loader/version switching with backup + incompatible-mods warning.
class ProfilesPage : public QWidget {
    Q_OBJECT
public:
    ProfilesPage(AccountStore *accounts, InstanceManager *instances, MojangApi *api, const GamePaths &paths,
                 QWidget *parent = nullptr);
    void setModLoaderInstaller(ModLoaderInstaller *l) { m_loaders = l; }

signals:
    void playRequested(const QString &instanceId);
    void modsRequested(const QString &instanceId);
    void worldsRequested(const QString &instanceId);
    void undoRequested(const QString &instanceId);
    void jarsDropped(const QString &instanceId, const QStringList &jarPaths);
    void changed();

public slots:
    void rebuild();

private slots:
    void onNew();
    void onImportMenu();
    void onFilterChanged();
    void onItemActivated(QListWidgetItem *it);
    void onContextMenu(const QPoint &pos);
    void onRowsMoved();

    void onSettings(const QString &id);
    void onSwitchLoader(const QString &id);
    void onClone(const QString &id);
    void onRename(const QString &id);
    void onExport(const QString &id);
    void onDelete(const QString &id);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QString describe(const struct Instance &in) const;

    AccountStore *m_accounts = nullptr;
    InstanceManager *m_instances = nullptr;
    ModLoaderInstaller *m_loaders = nullptr;
    MojangApi *m_api = nullptr;
    const GamePaths *m_paths = nullptr;

    QLineEdit *m_search = nullptr;
    QComboBox *m_groupFilter = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
};
