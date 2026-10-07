#pragma once

#include "Kindling.h"

#include <QWizard>

class AccountStore;
class InstanceManager;
class ModManager;
class ModrinthApi;
class MojangApi;
class QComboBox;
class QLabel;
class QRadioButton;

// Kindling — first-run guided setup (spec 10.3): choose account type →
// sign in / create offline profile → pick a starting style → Hearthlight
// creates a profile preloaded with a curated, loader-matched Modrinth set.
// Fully offline-capable (mods are skipped honestly when offline).
class KindlingDialog : public QWizard {
    Q_OBJECT
public:
    KindlingDialog(AccountStore *accounts, InstanceManager *instances, MojangApi *mojang, ModrinthApi *modrinth,
                   ModManager *mods, const QString &dataDir, QWidget *parent = nullptr);

    QString createdInstanceId() const { return m_createdId; }

private slots:
    void onOfflineClicked();
    void onMicrosoftClicked();
    void refreshAccountLabel();

private:
    bool validateCurrentPage() override;
    void accept() override;

    AccountStore *m_accounts = nullptr;
    InstanceManager *m_instances = nullptr;
    MojangApi *m_mojang = nullptr;
    ModrinthApi *m_modrinth = nullptr;
    ModManager *m_mods = nullptr;
    QString m_dataDir;

    QLabel *m_accountLabel = nullptr;
    QList<QRadioButton *> m_styleRadios;
    QList<StarterPack> m_packs;
    QComboBox *m_mc = nullptr;
    QString m_createdId;
};
