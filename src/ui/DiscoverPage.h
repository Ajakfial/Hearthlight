#pragma once

#include "ModrinthApi.h"

#include <QDialog>
#include <QWidget>

class AccountStore;
class Embers;
class InstanceManager;
class ModManager;
class MojangApi;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTextBrowser;

// One installed-or-installable project, opened from Discover.
class ProjectDialog : public QDialog {
    Q_OBJECT
public:
    ProjectDialog(const ModrinthSearchHit &hit, MojangApi *mojang, InstanceManager *instances,
                  ModrinthApi *modrinth, ModManager *mods, Embers *embers, const QString &dataDir,
                  QWidget *parent = nullptr);

signals:
    void installModpackFile(const QString &mrpackPath);
    void installedTo(const QString &instanceId);

private slots:
    void loadVersions();
    void onVersionSelected();
    void onInstall();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void renderHeader(const ModrinthProject &p);
    void requestImage(const QString &url, QLabel *label, int size);
    ModrinthVersion selectedOrBest(QString *error);

    ModrinthSearchHit m_hit;
    MojangApi *m_mojang = nullptr;
    InstanceManager *m_instances = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    ModManager *m_mods = nullptr;
    Embers *m_embers = nullptr;
    QString m_dataDir;

    ModrinthProject m_project;
    QList<ModrinthVersion> m_versions;
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QTextBrowser *m_about = nullptr;
    QListWidget *m_versionList = nullptr;
    QTextBrowser *m_changelog = nullptr;
    QWidget *m_galleryRow = nullptr;
    QComboBox *m_installTo = nullptr;
    QPushButton *m_installBtn = nullptr;
    QLabel *m_status = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_loaded = false;
};

// Modrinth discovery (spec 9): search + filters + sorting + pagination,
// project cards, detail with rendered description/gallery/versions/
// changelog/links/license, one-click install with dependency resolution,
// modpack-as-new-profile, offline-cached view.
class DiscoverPage : public QWidget {
    Q_OBJECT
public:
    DiscoverPage(MojangApi *mojang, InstanceManager *instances, ModrinthApi *modrinth, ModManager *mods,
                 Embers *embers, const QString &dataDir, QWidget *parent = nullptr);

signals:
    void installModpackFile(const QString &mrpackPath);
    void installedTo(const QString &instanceId);

private slots:
    void onSearch(bool reset = true);
    void onLoadMore();
    void onOpenProject(QListWidgetItem *it);
    void onIconReply(QNetworkReply *rep);

private:
    void setOfflineBanner();
    void addCards(const ModrinthSearchPage &page, bool append);
    QWidget *makeCard(const ModrinthSearchHit &hit);
    void requestIcon(const QString &url, QLabel *label);

    MojangApi *m_mojang = nullptr;
    InstanceManager *m_instances = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    ModManager *m_mods = nullptr;
    Embers *m_embers = nullptr;
    QString m_dataDir;

    QLineEdit *m_search = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_loader = nullptr;
    QComboBox *m_version = nullptr;
    QComboBox *m_category = nullptr;
    QComboBox *m_sort = nullptr;
    QLabel *m_banner = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_empty = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_more = nullptr;
    QNetworkAccessManager *m_nam = nullptr;

    ModrinthFilters m_current;
    int m_total = 0;
    bool m_searching = false;
};
