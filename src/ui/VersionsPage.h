#pragma once

#include "GamePaths.h"
#include "MojangApi.h"

#include <QWidget>

class AccountStore;
class AppSettings;
class JavaManager;
class Launcher;
class MojangApi;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QSpinBox;
class Theme;
class VersionInstaller;
struct VersionEntry;

// Vanilla version library: Mojang manifest with type filters,
// search, release dates, install state, and an Advanced section
// (memory, JVM args, window, demo flag). Play itself runs through the
// bottom Play bar via playRequested().
class VersionsPage : public QWidget {
    Q_OBJECT
public:
    VersionsPage(AppSettings *settings, AccountStore *accounts, MojangApi *api, VersionInstaller *installer,
                 JavaManager *java, Launcher *launcher, const GamePaths &paths, QWidget *parent = nullptr);

    QString selectedVersion() const;
    bool demoRequested() const;
    int launchWidth() const;
    int launchHeight() const;
    bool launchFullscreen() const;

signals:
    void versionSelected(const QString &id);
    void playRequested(const QString &id);

private slots:
    void reloadFromCache();
    void onRefresh();
    void onInstall();
    void onPlayClicked();
    void onSelectionChanged();
    void onSearchChanged(const QString &text);
    void onFilterToggled();
    void updateMemoryLabel();

private:
    void setStatus(const QString &s, bool isError = false);
    QString describe(const VersionEntry &e, bool installed) const;

    AppSettings *m_settings = nullptr;
    AccountStore *m_accounts = nullptr;
    MojangApi *m_api = nullptr;
    VersionInstaller *m_installer = nullptr;
    JavaManager *m_java = nullptr;
    Launcher *m_launcher = nullptr;
    GamePaths m_paths;

    QListWidget *m_list = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_fReleases = nullptr;
    QCheckBox *m_fSnapshots = nullptr;
    QCheckBox *m_fBeta = nullptr;
    QCheckBox *m_fAlpha = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_detail = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_installBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QCheckBox *m_demo = nullptr;
    QSlider *m_memory = nullptr;
    QLabel *m_memoryLabel = nullptr;
    QLineEdit *m_jvmArgs = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QCheckBox *m_fullscreen = nullptr;
    QComboBox *m_closeBox = nullptr;
    QList<VersionEntry> m_all;
    bool m_didAutoRefresh = false;
};
