#include "dialogs/InstalledModsDialog.h"

#include "Embers.h"
#include "IconProvider.h"
#include "InstanceManager.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "Task.h"
#include "dialogs/TaskProgressDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

InstalledModsDialog::InstalledModsDialog(const QString &instanceId, InstanceManager *instances, ModManager *mods,
                                         ModrinthApi *modrinth, Embers *embers, const QString &dataDir, QWidget *parent)
    : QDialog(parent)
    , m_instanceId(instanceId)
    , m_instances(instances)
    , m_mods(mods)
    , m_modrinth(modrinth)
    , m_embers(embers)
    , m_dataDir(dataDir)
{
    setWindowTitle(tr("Content — %1").arg(instanceName()));
    resize(680, 520);

    auto *lay = new QVBoxLayout(this);
    m_updates = new QLabel(this);
    m_updates->setWordWrap(true);
    m_updates->setVisible(false);
    lay->addWidget(m_updates);

    m_tabs = new QTabWidget(this);
    auto *modsTab = new QWidget(m_tabs);
    auto *ml = new QVBoxLayout(modsTab);
    ml->setContentsMargins(0, 6, 0, 0);
    m_list = new QListWidget(modsTab);
    m_list->setSelectionMode(QListWidget::SingleSelection);
    ml->addWidget(m_list, 1);
    m_tabs->addTab(modsTab, tr("Mods"));
    connect(m_list, &QListWidget::itemSelectionChanged, this, &InstalledModsDialog::onSelectionChanged);

    auto makePackTab = [this](const QString &label) {
        auto *tab = new QWidget(m_tabs);
        auto *tl = new QVBoxLayout(tab);
        tl->setContentsMargins(0, 6, 0, 0);
        auto *list = new QListWidget(tab);
        list->setSelectionMode(QListWidget::SingleSelection);
        tl->addWidget(list, 1);
        m_tabs->addTab(tab, label);
        return list;
    };
    m_packsList = makePackTab(tr("Resource packs"));
    m_shadersList = makePackTab(tr("Shader packs"));
    m_dataList = makePackTab(tr("Data packs"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &InstalledModsDialog::onPackTabChanged);
    lay->addWidget(m_tabs, 1);

    auto *row = new QHBoxLayout();
    auto *toggleBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("toggle")), tr("Turn off/on"), this);
    connect(toggleBtn, &QPushButton::clicked, this, &InstalledModsDialog::onToggle);
    row->addWidget(toggleBtn);
    auto *rmBtn = new QPushButton(tr("Remove"), this);
    connect(rmBtn, &QPushButton::clicked, this, &InstalledModsDialog::onRemove);
    row->addWidget(rmBtn);
    auto *folderBtn = new QPushButton(tr("Open folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &InstalledModsDialog::onOpenFolder);
    row->addWidget(folderBtn);
    row->addStretch(1);
    lay->addLayout(row);

    auto *row2 = new QHBoxLayout();
    auto *checkBtn = new QPushButton(tr("Check for updates"), this);
    connect(checkBtn, &QPushButton::clicked, this, &InstalledModsDialog::onCheckUpdates);
    row2->addWidget(checkBtn);
    auto *updateAllBtn = new QPushButton(tr("Update all"), this);
    connect(updateAllBtn, &QPushButton::clicked, this, &InstalledModsDialog::onUpdateAll);
    row2->addWidget(updateAllBtn);
    auto *undoBtn = new QPushButton(tr("Undo last change"), this);
    undoBtn->setToolTip(tr("Restore the Embers snapshot taken before the last mod change."));
    connect(undoBtn, &QPushButton::clicked, this, &InstalledModsDialog::onUndo);
    row2->addWidget(undoBtn);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    row2->addWidget(closeBtn);
    lay->addLayout(row2);

    auto *rowP = new QHBoxLayout();
    m_packHint = new QLabel(this);
    m_packHint->setObjectName(QStringLiteral("secondary"));
    m_packHint->setWordWrap(true);
    rowP->addWidget(m_packHint, 1);
    auto *packAddBtn = new QPushButton(tr("Add…"), this);
    connect(packAddBtn, &QPushButton::clicked, this, &InstalledModsDialog::onPackAdd);
    rowP->addWidget(packAddBtn);
    auto *packToggleBtn = new QPushButton(tr("Turn off/on"), this);
    connect(packToggleBtn, &QPushButton::clicked, this, &InstalledModsDialog::onPackToggle);
    rowP->addWidget(packToggleBtn);
    auto *packRmBtn = new QPushButton(tr("Remove"), this);
    connect(packRmBtn, &QPushButton::clicked, this, &InstalledModsDialog::onPackRemove);
    rowP->addWidget(packRmBtn);
    auto *packFolderBtn = new QPushButton(tr("Open folder"), this);
    connect(packFolderBtn, &QPushButton::clicked, this, &InstalledModsDialog::onPackOpenFolder);
    rowP->addWidget(packFolderBtn);
    lay->addLayout(rowP);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("secondary"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    connect(m_mods, &ModManager::modsChanged, this, [this](const QString &id) {
        if (id == m_instanceId) {
            rebuild();
        }
    });
    rebuild();
    onPackTabChanged(m_tabs ? m_tabs->currentIndex() : 0);
    setStatus(tr("Drag a .jar onto the profile list to add it by hand."));
}

QString InstalledModsDialog::instanceName() const
{
    const Instance in = m_instances->get(m_instanceId);
    return in.isValid() ? in.name : m_instanceId;
}

void InstalledModsDialog::setStatus(const QString &s)
{
    m_status->setText(s);
}

void InstalledModsDialog::rebuild()
{
    m_list->blockSignals(true);
    m_list->clear();
    const auto mods = m_mods->listMods(m_instanceId);
    QSet<QString> updating;
    for (const auto &u : m_pending) {
        updating.insert(u.installed.fileName);
    }
    if (mods.isEmpty()) {
        auto *it = new QListWidgetItem(tr("No mods yet — find some on the Discover page."), m_list);
        it->setFlags(Qt::NoItemFlags);
    }
    for (const auto &m : mods) {
        QStringList badges;
        if (!m.enabled) {
            badges.append(tr("off"));
        }
        if (m.manual) {
            badges.append(tr("added by hand"));
        } else if (!m.versionNumber.isEmpty()) {
            badges.append(m.versionNumber);
        }
        if (updating.contains(m.fileName)) {
            badges.append(tr("update ready"));
        }
        auto *it = new QListWidgetItem(
            QStringLiteral("%1%2").arg(m.title, badges.isEmpty() ? QString() : QStringLiteral("  [%1]").arg(badges.join(QStringLiteral(", ")))), m_list);
        it->setData(Qt::UserRole, m.fileName);
        if (!m.enabled) {
            it->setForeground(QColor(QStringLiteral("#8E8E93")));
        }
    }
    m_list->blockSignals(false);
    onSelectionChanged();
    rebuildPacks();
}

void InstalledModsDialog::onSelectionChanged()
{
    // Hook for future per-mod detail; keeps selection UX predictable.
}

static bool snapshotFirst(Embers *embers, const QString &id, QWidget *parent)
{
    if (!embers) {
        return true;
    }
    QString err;
    if (embers->snapshot(id, QStringLiteral("mod-change"), false, &err).isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Couldn't back up first"),
                             QObject::tr("Embers couldn't snapshot this profile (%1). Nothing was changed.").arg(err));
        return false;
    }
    return true;
}

void InstalledModsDialog::onToggle()
{
    auto *it = m_list->currentItem();
    if (!it) {
        return;
    }
    const QString file = it->data(Qt::UserRole).toString();
    const auto mods = m_mods->listMods(m_instanceId);
    bool enabled = true;
    for (const auto &m : mods) {
        if (m.fileName == file) {
            enabled = m.enabled;
            break;
        }
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    QString err;
    if (!m_mods->setEnabled(m_instanceId, file, !enabled, &err)) {
        QMessageBox::warning(this, tr("Couldn't do that"), err);
    }
}

void InstalledModsDialog::onRemove()
{
    auto *it = m_list->currentItem();
    if (!it) {
        return;
    }
    const QString file = it->data(Qt::UserRole).toString();
    auto rc = QMessageBox::question(this, tr("Remove this mod?"), tr("Remove “%1” from this profile?").arg(file));
    if (rc != QMessageBox::Yes) {
        return;
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    QString err;
    if (!m_mods->removeMod(m_instanceId, file, &err)) {
        QMessageBox::warning(this, tr("Couldn't remove that"), err);
    }
}

void InstalledModsDialog::onOpenFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(ModManager::modsDir(m_dataDir, m_instanceId)));
}

void InstalledModsDialog::onCheckUpdates()
{
    const Instance in = m_instances->get(m_instanceId);
    if (!in.isValid()) {
        return;
    }
    setStatus(tr("Checking…"));
    m_pending.clear();
    auto *t = new LambdaTask(
        tr("Check for updates"),
        [this, in](Task::Context &ctx) {
            m_pending = m_mods->checkUpdatesBlocking(in.id, in.versionId, in.loaderType, ctx, m_modrinth);
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    connect(t, &Task::finished, this, [this, t](bool ok) {
        t->deleteLater();
        if (!ok) {
            setStatus(t->errorString());
            return;
        }
        rebuild();
        if (m_pending.isEmpty()) {
            m_updates->setVisible(false);
            setStatus(tr("Everything is up to date."));
        } else {
            QStringList names;
            for (const auto &u : m_pending) {
                names.append(QStringLiteral("%1 → %2").arg(u.installed.title, u.newer.versionNumber));
            }
            m_updates->setText(tr("Updates ready: %1").arg(names.join(QStringLiteral("; "))));
            m_updates->setVisible(true);
            setStatus(tr("%1 update(s) ready — “Update all” installs them.").arg(m_pending.size()));
        }
    });
    t->start();
    prog.exec();
}

void InstalledModsDialog::onUpdateAll()
{
    if (m_pending.isEmpty()) {
        setStatus(tr("Check for updates first."));
        return;
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    const QList<ModUpdate> updates = m_pending;
    auto *t = new LambdaTask(
        tr("Update %1 mod(s)").arg(updates.size()),
        [this, updates](Task::Context &ctx) {
            int i = 0;
            for (const auto &u : updates) {
                if (ctx.isCancelled()) {
                    ctx.fail(tr("Cancelled — use Undo last change to roll back."));
                    return false;
                }
                ctx.report(i++, updates.size(), tr("Updating %1…").arg(u.installed.title));
                // Remove the old jar, then install the new file.
                QString rerr;
                m_mods->removeMod(m_instanceId, u.installed.fileName, &rerr);
                ModInstallPlan unit;
                unit.project.id = u.installed.projectId;
                unit.project.title = u.installed.title;
                unit.project.slug = u.installed.slug;
                unit.version = u.newer;
                unit.file = u.newer.bestFile();
                unit.targetDir = QStringLiteral("mods");
                if (!m_mods->installUnitBlocking(m_instanceId, unit, 8, ctx)) {
                    return false;
                }
            }
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    connect(t, &Task::finished, this, [this, t](bool ok) {
        t->deleteLater();
        if (!ok) {
            QMessageBox::warning(this, tr("Some updates failed"),
                                 tr("%1\n\nEmbers snapshotted first — “Undo last change” rolls this back.")
                                     .arg(t->errorString()));
        }
        m_pending.clear();
        m_updates->setVisible(false);
        rebuild();
    });
    t->start();
    prog.exec();
}

void InstalledModsDialog::onUndo()
{
    QString err;
    if (!m_embers || !m_embers->undoLast(m_instanceId, &err)) {
        QMessageBox::information(this, tr("Undo last change"), err.isEmpty() ? tr("Nothing to undo.") : err);
        return;
    }
    m_pending.clear();
    m_updates->setVisible(false);
    rebuild();
    setStatus(tr("Rolled back to the snapshot before the last change."));
}

QString InstalledModsDialog::currentPackFolder() const
{
    const int idx = m_tabs ? m_tabs->currentIndex() : 0;
    if (idx == 2) {
        return QStringLiteral("shaderpacks");
    }
    if (idx == 3) {
        return QStringLiteral("datapacks");
    }
    return QStringLiteral("resourcepacks");
}

QListWidget *InstalledModsDialog::currentPackList() const
{
    const QString f = currentPackFolder();
    if (f == QStringLiteral("shaderpacks")) {
        return m_shadersList;
    }
    if (f == QStringLiteral("datapacks")) {
        return m_dataList;
    }
    return m_packsList;
}

void InstalledModsDialog::onPackTabChanged(int idx)
{
    Q_UNUSED(idx);
    if (!m_packHint) {
        return;
    }
    const QString f = currentPackFolder();
    if (f == QStringLiteral("datapacks")) {
        m_packHint->setText(tr("Data packs stage here; copy them into a world's datapacks folder to activate."));
    } else if (f == QStringLiteral("shaderpacks")) {
        m_packHint->setText(tr("Shader packs (.zip) — needs a shader-capable mod + loader."));
    } else {
        m_packHint->setText(tr("Resource packs (.zip or unpacked folder)."));
    }
    rebuildPacks();
}

void InstalledModsDialog::rebuildPacks()
{
    const struct {
        QListWidget *list;
        const char *folder;
    } tabs[] = {
        { m_packsList, "resourcepacks" },
        { m_shadersList, "shaderpacks" },
        { m_dataList, "datapacks" },
    };
    for (const auto &t : tabs) {
        if (!t.list) {
            continue;
        }
        t.list->blockSignals(true);
        t.list->clear();
        const QString folder = QString::fromLatin1(t.folder);
        const auto items = m_mods->listContent(m_instanceId, folder);
        if (items.isEmpty()) {
            auto *it = new QListWidgetItem(tr("(empty — Discover installs %1 here too)").arg(folder), t.list);
            it->setFlags(Qt::NoItemFlags);
        }
        for (const auto &m : items) {
            const QString badge = m.enabled ? QString() : tr("  [off]");
            auto *it = new QListWidgetItem(QStringLiteral("%1%2").arg(m.title, badge), t.list);
            it->setData(Qt::UserRole, m.fileName);
            if (!m.enabled) {
                it->setForeground(QColor(QStringLiteral("#8E8E93")));
            }
        }
        t.list->blockSignals(false);
    }
}

void InstalledModsDialog::onPackAdd()
{
    const QString folder = currentPackFolder();
    const QString p = QFileDialog::getOpenFileName(this, tr("Add to %1").arg(folder), {},
                                                   tr("Packs (*.zip)"));
    if (p.isEmpty()) {
        return;
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    QString err, added;
    if (!m_mods->addExternalPack(m_instanceId, folder, p, { QStringLiteral("*.zip") }, &err, &added)) {
        QMessageBox::warning(this, tr("Couldn't add that"), err);
        return;
    }
    rebuildPacks();
    setStatus(tr("Added %1 to %2.").arg(added, folder));
}

void InstalledModsDialog::onPackToggle()
{
    QListWidget *list = currentPackList();
    auto *it = list ? list->currentItem() : nullptr;
    const QString file = it ? it->data(Qt::UserRole).toString() : QString();
    if (file.isEmpty()) {
        return;
    }
    const QString folder = currentPackFolder();
    const auto items = m_mods->listContent(m_instanceId, folder);
    bool enabled = true;
    for (const auto &m : items) {
        if (m.fileName == file) {
            enabled = m.enabled;
            break;
        }
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    QString err;
    if (!m_mods->setContentEnabled(m_instanceId, folder, file, !enabled, &err)) {
        QMessageBox::warning(this, tr("Couldn't do that"), err);
        return;
    }
    rebuildPacks();
}

void InstalledModsDialog::onPackRemove()
{
    QListWidget *list = currentPackList();
    auto *it = list ? list->currentItem() : nullptr;
    const QString file = it ? it->data(Qt::UserRole).toString() : QString();
    if (file.isEmpty()) {
        return;
    }
    auto rc = QMessageBox::question(this, tr("Remove this file?"),
                                    tr("Remove “%1” from %2?").arg(file, currentPackFolder()));
    if (rc != QMessageBox::Yes) {
        return;
    }
    if (!snapshotFirst(m_embers, m_instanceId, this)) {
        return;
    }
    QString err;
    if (!m_mods->removeContent(m_instanceId, currentPackFolder(), file, &err)) {
        QMessageBox::warning(this, tr("Couldn't remove that"), err);
        return;
    }
    rebuildPacks();
}

void InstalledModsDialog::onPackOpenFolder()
{
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(ModManager::contentDir(m_dataDir, m_instanceId, currentPackFolder())));
}

#include "InstalledModsDialog.moc"
