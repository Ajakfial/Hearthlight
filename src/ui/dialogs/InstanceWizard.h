#pragma once

#include "GamePaths.h"
#include "Instance.h"

#include <QWizard>

class AccountStore;
class MojangApi;
class QComboBox;
class QLineEdit;

// Friendly creation wizard (spec 7):
// name → Minecraft version → loader (Vanilla/Fabric/Quilt/Forge/NeoForge)
// → account behavior → done. Loader versions load in the background with
// "Latest stable" recommended; everything works from cache offline.
class InstanceWizard : public QWizard {
    Q_OBJECT
public:
    InstanceWizard(AccountStore *accounts, MojangApi *api, const GamePaths &paths, QWidget *parent = nullptr);

    Instance resultInstance() const { return m_result; }

private slots:
    void onLoaderChanged(int idx);
    void onMcVersionChanged(const QString &mc);
    void refreshLoaderVersions();

private:
    bool validateCurrentPage() override;
    bool loadCachedVersions();

    AccountStore *m_accounts = nullptr;
    MojangApi *m_api = nullptr;
    GamePaths m_paths;

    QLineEdit *m_name = nullptr;
    QComboBox *m_version = nullptr;
    QComboBox *m_loader = nullptr;
    QComboBox *m_loaderVer = nullptr;
    QComboBox *m_account = nullptr;
    Instance m_result;
};
