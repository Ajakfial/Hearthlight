#include "ProfilesPage.h"

#include "AccountStore.h"
#include "GamePaths.h"
#include "IconProvider.h"
#include "InstanceManager.h"
#include "Logger.h"
#include "ModLoader.h"
#include "MojangApi.h"
#include "Task.h"
#include "dialogs/InstanceSettingsDialog.h"
#include "dialogs/InstanceWizard.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

ProfilesPage::ProfilesPage(AccountStore *accounts, InstanceManager *instances, MojangApi *api, const GamePaths &paths,
                           QWidget *parent)
    : QWidget(parent)
    , m_accounts(accounts)
    , m_instances(instances)
    , m_api(api)
    , m_paths(new GamePaths(paths))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(12);

    auto *title = new QLabel(tr("Profiles"), this);
    QFont tf = title->font();
    tf.setPointSize(20);
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);
    auto *sub = new QLabel(tr("Each profile is its own folder: mods, saves, settings — safely separate."), this);
    sub->setObjectName(QStringLiteral("secondary"));
    sub->setWordWrap(true);
    outer->addWidget(sub);

    auto *bar = new QHBoxLayout();
    auto *newBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("plus")), tr("New profile"), this);
    connect(newBtn, &QPushButton::clicked, this, &ProfilesPage::onNew);
    bar->addWidget(newBtn);
    auto *importBtn = new QToolButton(this);
    importBtn->setText(tr("Import"));
    importBtn->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(importBtn);
    menu->addAction(tr("Hearthlight pack (.hearthpack)…"), this, [this] { onImportMenu(); });
    menu->addAction(tr("Modrinth pack (.mrpack)…"), this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import Modrinth pack"), {}, tr("Modrinth (*.mrpack)"));
        if (!p.isEmpty()) {
            emit playRequested(QStringLiteral("import-mrpack:") + p);
        }
    });
    menu->addAction(tr("CurseForge zip (best-effort)…"), this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import CurseForge zip"), {}, tr("Zips (*.zip)"));
        if (!p.isEmpty()) {
            emit playRequested(QStringLiteral("import-curseforge:") + p);
        }
    });
    importBtn->setMenu(menu);
    bar->addWidget(importBtn);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search profiles…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &ProfilesPage::onFilterChanged);
    bar->addWidget(m_search, 1);
    m_groupFilter = new QComboBox(this);
    m_groupFilter->setToolTip(tr("Filter by collection"));
    connect(m_groupFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ProfilesPage::onFilterChanged);
    bar->addWidget(m_groupFilter);
    auto *openFolderBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("folder")), tr("Open folder"), this);
    openFolderBtn->setToolTip(tr("Open the profiles folder in your file manager."));
    connect(openFolderBtn, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_instances->instancesDir()));
    });
    bar->addWidget(openFolderBtn);
    outer->addLayout(bar);

    m_list = new QListWidget(this);
    m_list->setDragDropMode(QListWidget::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QListWidget::SingleSelection);
    m_list->setSpacing(6);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setToolTip(tr("Double-click to play. Right-click for settings. Drag to reorder. Drop a .jar on a profile to add it."));
    outer->addWidget(m_list, 1);
    m_empty = new QLabel(this);
    m_empty->setPixmap(IconProvider::instance().pixmap(QStringLiteral("profile"), 64, QColor(QStringLiteral("#8E8E93"))));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setVisible(false);
    outer->addWidget(m_empty);
    // External .jar drops land on the list (internal moves still reorder).
    setAcceptDrops(true);

    connect(m_list, &QListWidget::itemDoubleClicked, this, &ProfilesPage::onItemActivated);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ProfilesPage::onContextMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &ProfilesPage::onRowsMoved);
    connect(m_instances, &InstanceManager::changed, this, &ProfilesPage::rebuild);
    connect(m_accounts, &AccountStore::changed, this, &ProfilesPage::rebuild);
    rebuild();
}

QString ProfilesPage::describe(const Instance &in) const
{
    const QString loader = in.loaderType == QStringLiteral("vanilla")
        ? tr("Vanilla")
        : QStringLiteral("%1 %2").arg(loaderDisplayName(loaderTypeFromString(in.loaderType)), in.loaderVersion);
    QStringList bits;
    bits.append(loader);
    if (!in.group.isEmpty()) {
        bits.append(in.group);
    }
    if (!in.quickPlayWorld.isEmpty()) {
        bits.append(tr("Quick Play: %1").arg(in.quickPlayWorld));
    } else if (!in.quickPlayServer.isEmpty()) {
        bits.append(tr("Quick Play: %1").arg(in.quickPlayServer));
    }
    if (in.accountMode == InstanceAccountMode::Specific) {
        const Account a = m_accounts->accountById(in.accountId);
        bits.append(a.isValid() ? a.username : tr("(account missing)"));
    } else if (in.accountMode == InstanceAccountMode::Ask) {
        bits.append(tr("asks account"));
    }
    const int mods = 0; // mod count needs dataDir; shown in tooltip via game dir size instead
    Q_UNUSED(mods);
    if (!in.lastPlayed.isEmpty()) {
        const int mins = (int)(in.playtimeSecs / 60);
        const int hrs = mins / 60;
        bits.append(hrs > 0 ? tr("played %1h %2m").arg(hrs).arg(mins % 60) : tr("played %1m").arg(mins));
        if (!in.lastAccount.isEmpty()) {
            bits.append(tr("as %1").arg(in.lastAccount));
        }
    } else {
        bits.append(tr("never played"));
    }
    return QStringLiteral("Minecraft %1 · %2").arg(in.versionId, bits.join(QStringLiteral(" · ")));
}

void ProfilesPage::rebuild()
{
    const QString gf = m_groupFilter->currentData().toString();
    m_groupFilter->blockSignals(true);
    m_groupFilter->clear();
    m_groupFilter->addItem(tr("All collections"), QString());
    for (const auto &g : m_instances->groups()) {
        m_groupFilter->addItem(g, g);
    }
    const int gi = m_groupFilter->findData(gf);
    m_groupFilter->setCurrentIndex(gi < 0 ? 0 : gi);
    m_groupFilter->blockSignals(false);

    const QString q = m_search->text().trimmed().toLower();
    const QString group = m_groupFilter->currentData().toString();
    m_list->blockSignals(true);
    m_list->clear();
    const auto all = m_instances->instances();
    if (all.isEmpty()) {
        auto *it = new QListWidgetItem(tr("No profiles yet — press “New profile” to create one."), m_list);
        it->setFlags(Qt::NoItemFlags);
    }
    for (const auto &in : all) {
        if (!group.isEmpty() && in.group != group) {
            continue;
        }
        if (!q.isEmpty() && !in.name.toLower().contains(q) && !in.versionId.toLower().contains(q)) {
            continue;
        }
        auto *it = new QListWidgetItem(
            IconProvider::instance().icon(in.loaderType == QStringLiteral("vanilla") ? QStringLiteral("profile")
                                                                                      : QStringLiteral("mod")),
            QStringLiteral("%1\n%2").arg(in.name, describe(in)), m_list);
        it->setData(Qt::UserRole, in.id);
        it->setToolTip(describe(in));
    }
    m_list->blockSignals(false);
    // Illustration under the list whenever it shows the "nothing here" row
    // (no profiles at all) or a filter matched nothing.
    m_empty->setVisible(all.isEmpty() || m_list->count() == 0);
}

void ProfilesPage::onFilterChanged()
{
    rebuild();
}
void ProfilesPage::onItemActivated(QListWidgetItem *it)
{
    if (it && m_instances->has(it->data(Qt::UserRole).toString())) {
        emit playRequested(it->data(Qt::UserRole).toString());
    }
}

void ProfilesPage::onRowsMoved()
{
    // Persist drag-reorder: map visual row order back to ids.
    for (int row = 0; row < m_list->count(); ++row) {
        const QString id = m_list->item(row)->data(Qt::UserRole).toString();
        if (!id.isEmpty()) {
            m_instances->move(id, row);
        }
    }
}

void ProfilesPage::onContextMenu(const QPoint &pos)
{
    auto *it = m_list->itemAt(pos);
    if (!it || !m_instances->has(it->data(Qt::UserRole).toString())) {
        return;
    }
    const QString id = it->data(Qt::UserRole).toString();
    QMenu menu(this);
    menu.addAction(tr("Play"), this, [this, id] { emit playRequested(id); });
    menu.addAction(tr("Play without mods once (safe mode)"), this,
                  [this, id] { emit playRequested(QStringLiteral("safe-mode:") + id); });
    menu.addSeparator();
    menu.addAction(tr("Mods…"), this, [this, id] { emit modsRequested(id); });
    menu.addAction(tr("Worlds & backups…"), this, [this, id] { emit worldsRequested(id); });
    menu.addAction(tr("Settings…"), this, [this, id] { onSettings(id); });
    menu.addAction(tr("Switch loader / version…"), this, [this, id] { onSwitchLoader(id); });
    menu.addAction(tr("Undo last change"), this, [this, id] { emit undoRequested(id); });
    menu.addSeparator();
    menu.addAction(tr("Clone…"), this, [this, id] { onClone(id); });
    menu.addAction(tr("Rename…"), this, [this, id] { onRename(id); });
    menu.addAction(tr("Export .hearthpack…"), this, [this, id] { onExport(id); });
    menu.addSeparator();
    menu.addAction(tr("Delete…"), this, [this, id] { onDelete(id); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void ProfilesPage::onNew()
{
    InstanceWizard wiz(m_accounts, m_api, *m_paths, this);
    if (wiz.exec() != QDialog::Accepted) {
        return;
    }
    QString err;
    if (!m_instances->create(wiz.resultInstance(), &err)) {
        QMessageBox::warning(this, tr("Couldn't create that profile"), err);
        return;
    }
    emit changed();
}

void ProfilesPage::onImportMenu()
{
    const QString p = QFileDialog::getOpenFileName(this, tr("Import Hearthlight pack"), {},
                                                   tr("Hearthlight packs (*.hearthpack *.zip)"));
    if (!p.isEmpty()) {
        emit playRequested(QStringLiteral("import-hearthpack:") + p);
    }
}

void ProfilesPage::onSettings(const QString &id)
{
    Instance in = m_instances->get(id);
    if (!in.isValid()) {
        return;
    }
    InstanceSettingsDialog dlg(in, m_accounts, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QString err;
    if (!m_instances->update(dlg.result(), &err)) {
        QMessageBox::warning(this, tr("Couldn't save those settings"), err);
    }
}

void ProfilesPage::onSwitchLoader(const QString &id)
{
    Instance in = m_instances->get(id);
    if (!in.isValid() || !m_loaders) {
        return;
    }
    QStringList loaderNames = { tr("Vanilla"), tr("Fabric"), tr("Quilt"), tr("Forge"), tr("NeoForge") };
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, tr("Switch loader / version"),
                                               tr("Loader for “%1” (current: %2 %3, Minecraft %4):")
                                                   .arg(in.name, in.loaderType, in.loaderVersion, in.versionId),
                                               loaderNames, 0, false, &ok);
    if (!ok) {
        return;
    }
    const QStringList keys = { QStringLiteral("vanilla"), QStringLiteral("fabric"), QStringLiteral("quilt"),
                               QStringLiteral("forge"), QStringLiteral("neoforge") };
    const QString newLoader = keys.value(loaderNames.indexOf(pick), QStringLiteral("vanilla"));
    QString newMc = QInputDialog::getText(this, tr("Switch loader / version"),
                                          tr("Minecraft version (current: %1):").arg(in.versionId), QLineEdit::Normal,
                                          in.versionId, &ok);
    if (!ok || newMc.trimmed().isEmpty()) {
        return;
    }
    newMc = newMc.trimmed();
    // Safety backup first + incompatible-mods warning.
    QString berr;
    const QString backup = m_instances->backupForSwitch(id, &berr);
    if (backup.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't back up first"), berr);
        return;
    }
    const int mods = 0; // counted at launch; warn generically here
    Q_UNUSED(mods);
    auto rc = QMessageBox::question(this, tr("Switch “%1”?").arg(in.name),
                                    tr("Backed up to:\n%1\n\nSwitching loader/Minecraft version can make "
                                       "existing mods stop working. Continue?")
                                        .arg(backup));
    if (rc != QMessageBox::Yes) {
        return;
    }
    Instance upd = in;
    upd.loaderType = newLoader;
    upd.loaderVersion.clear(); // "Latest stable" resolves at next launch
    upd.versionId = newMc;
    QString uerr;
    if (!m_instances->update(upd, &uerr)) {
        QMessageBox::warning(this, tr("Couldn't switch"), uerr);
        return;
    }
    QMessageBox::information(this, tr("Switched"),
                             tr("“%1” now uses %2 on Minecraft %3.\nThe loader installs on next Play "
                                "(“Latest stable”). Your backup is at:\n%4")
                                 .arg(in.name, newLoader, newMc, backup));
}

void ProfilesPage::onClone(const QString &id)
{
    const Instance src = m_instances->get(id);
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Clone profile"), tr("Name for the copy:"), QLineEdit::Normal,
                              src.name + tr(" copy"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString err, newId;
    if (!m_instances->clone(id, name.trimmed(), &newId, &err)) {
        QMessageBox::warning(this, tr("Couldn't clone that profile"), err);
    }
}

void ProfilesPage::onRename(const QString &id)
{
    const Instance src = m_instances->get(id);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("New name:"), QLineEdit::Normal,
                                               src.name, &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString err;
    if (!m_instances->rename(id, name.trimmed(), &err)) {
        QMessageBox::warning(this, tr("Couldn't rename that profile"), err);
    }
}

void ProfilesPage::onExport(const QString &id)
{
    const Instance src = m_instances->get(id);
    const QString p = QFileDialog::getSaveFileName(this, tr("Export “%1”").arg(src.name),
                                                   QStringLiteral("%1.hearthpack").arg(src.id),
                                                   tr("Hearthlight packs (*.hearthpack)"));
    if (p.isEmpty()) {
        return;
    }
    QString err;
    if (!m_instances->exportHearthpack(id, p, &err)) {
        QMessageBox::warning(this, tr("Couldn't export that profile"), err);
    } else {
        QMessageBox::information(this, tr("Exported"), tr("Saved to:\n%1").arg(p));
    }
}

void ProfilesPage::onDelete(const QString &id)
{
    const Instance src = m_instances->get(id);
    auto rc = QMessageBox::question(this, tr("Delete “%1”?").arg(src.name),
                                    tr("Delete this profile?\nThe game folder stays in the Recycle Bin/Trash "
                                       "so worlds can be recovered."));
    if (rc != QMessageBox::Yes) {
        return;
    }
    if (!m_instances->remove(id, true)) {
        QMessageBox::warning(this, tr("Couldn't delete that profile"), tr("Delete failed."));
    }
}

static bool urlsAreJars(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    for (const auto &u : mime->urls()) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void ProfilesPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (urlsAreJars(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void ProfilesPage::dragMoveEvent(QDragMoveEvent *event)
{
    if (urlsAreJars(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void ProfilesPage::dropEvent(QDropEvent *event)
{
    if (!urlsAreJars(event->mimeData())) {
        return;
    }
    // Target: the row under the cursor, else the current selection.
    QString target;
    if (auto *it = m_list->itemAt(m_list->viewport()->mapFromGlobal(QCursor::pos()))) {
        target = it->data(Qt::UserRole).toString();
    } else if (auto *cur = m_list->currentItem()) {
        target = cur->data(Qt::UserRole).toString();
    }
    if (target.isEmpty() || !m_instances->has(target)) {
        QMessageBox::information(this, tr("Drop a mod"),
                                 tr("Drop the .jar onto a profile row to add it to that profile."));
        return;
    }
    QStringList jars;
    for (const auto &u : event->mimeData()->urls()) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty() && local.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
            jars.append(local);
        }
    }
    if (!jars.isEmpty()) {
        event->acceptProposedAction();
        emit jarsDropped(target, jars);
    }
}

#include "ProfilesPage.moc"
