#include "dialogs/WorldsDialog.h"

#include "Instance.h"
#include "InstanceManager.h"
#include "Task.h"
#include "dialogs/TaskProgressDialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

WorldsDialog::WorldsDialog(const QString &instanceId, InstanceManager *instances, Hearthstones *stones,
                           const QString &dataDir, QWidget *parent)
    : QDialog(parent)
    , m_instanceId(instanceId)
    , m_instances(instances)
    , m_stones(stones)
    , m_dataDir(dataDir)
{
    const Instance in = m_instances->get(m_instanceId);
    setWindowTitle(tr("Worlds & backups — %1").arg(in.isValid() ? in.name : m_instanceId));
    resize(680, 480);

    auto *lay = new QVBoxLayout(this);
    auto *split = new QSplitter(Qt::Horizontal, this);

    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(new QLabel(tr("Worlds"), left));
    m_worlds = new QListWidget(left);
    m_worlds->setSelectionMode(QListWidget::ExtendedSelection);
    ll->addWidget(m_worlds, 1);

    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(new QLabel(tr("Backups (Hearthstones)"), right));
    m_backups = new QListWidget(right);
    rl->addWidget(m_backups, 1);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);

    auto *form = new QHBoxLayout();
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Backup name (optional)"));
    form->addWidget(m_name, 1);
    m_config = new QCheckBox(tr("Include settings"), this);
    m_config->setChecked(true);
    m_config->setToolTip(tr("Also back up configs and options.txt."));
    form->addWidget(m_config);
    auto *backupBtn = new QPushButton(tr("Back up now"), this);
    connect(backupBtn, &QPushButton::clicked, this, &WorldsDialog::onBackup);
    form->addWidget(backupBtn);
    lay->addLayout(form);

    auto *row = new QHBoxLayout();
    auto *restoreBtn = new QPushButton(tr("Restore backup…"), this);
    connect(restoreBtn, &QPushButton::clicked, this, &WorldsDialog::onRestore);
    row->addWidget(restoreBtn);
    auto *delBtn = new QPushButton(tr("Delete backup"), this);
    connect(delBtn, &QPushButton::clicked, this, &WorldsDialog::onDelete);
    row->addWidget(delBtn);
    auto *folderBtn = new QPushButton(tr("Open saves folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &WorldsDialog::onOpenFolder);
    row->addWidget(folderBtn);
    row->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(closeBtn);
    lay->addLayout(row);

    connect(m_stones, &Hearthstones::changed, this, [this](const QString &id) {
        if (id == m_instanceId) {
            rebuild();
        }
    });
    rebuild();
}

QString WorldsDialog::prettySize(qint64 bytes)
{
    if (bytes < 1024) {
        return tr("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return tr("%1 KB").arg(bytes / 1024);
    }
    if (bytes < 1024 * 1024 * 1024) {
        return tr("%1 MB").arg(bytes / (1024 * 1024));
    }
    return tr("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1));
}

QString WorldsDialog::prettyTime(qint64 ms)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    const qint64 days = QDateTime::currentDateTime().toMSecsSinceEpoch() / 86400000 - ms / 86400000;
    if (days <= 0) {
        return tr("today %1").arg(dt.toString(QStringLiteral("h:mm AP")));
    }
    if (days == 1) {
        return tr("yesterday");
    }
    return tr("%1 days ago").arg(days);
}

void WorldsDialog::rebuild()
{
    m_worlds->blockSignals(true);
    m_worlds->clear();
    const auto worlds = m_stones->listWorlds(m_instanceId);
    if (worlds.isEmpty()) {
        auto *it = new QListWidgetItem(tr("(no worlds yet — play first)"), m_worlds);
        it->setFlags(Qt::NoItemFlags);
    }
    for (const auto &w : worlds) {
        auto *it = new QListWidgetItem(QStringLiteral("%1  ·  %2  ·  %3").arg(w.folder, prettySize(w.sizeBytes),
                                                                              prettyTime(w.lastModifiedMs)),
                                       m_worlds);
        it->setData(Qt::UserRole, w.folder);
    }
    m_worlds->blockSignals(false);

    m_backups->blockSignals(true);
    m_backups->clear();
    const auto snaps = m_stones->list(m_instanceId);
    if (snaps.isEmpty()) {
        auto *it = new QListWidgetItem(tr("(no backups yet)"), m_backups);
        it->setFlags(Qt::NoItemFlags);
    }
    for (const auto &h : snaps) {
        const QString when = h.created.isEmpty() ? h.name.left(15)
                                                 : QDateTime::fromString(h.created, Qt::ISODate)
                                                       .toString(QStringLiteral("MMM d h:mm AP"));
        auto *it = new QListWidgetItem(
            QStringLiteral("%1  ·  %2  ·  %3%4")
                .arg(h.name, when, prettySize(h.sizeBytes), h.automatic ? tr("  ·  auto") : QString()),
            m_backups);
        it->setData(Qt::UserRole, h.name);
        it->setToolTip(h.worlds.isEmpty() ? (h.includesConfig ? tr("Settings only") : h.name)
                                          : tr("Worlds: %1%2").arg(h.worlds.join(QStringLiteral(", ")),
                                                                   h.includesConfig ? tr(" + settings") : QString()));
    }
    m_backups->blockSignals(false);
}

void WorldsDialog::onBackup()
{
    QStringList worlds;
    for (auto *it : m_worlds->selectedItems()) {
        const QString f = it->data(Qt::UserRole).toString();
        if (!f.isEmpty()) {
            worlds.append(f);
        }
    }
    const QString name = m_name->text().trimmed();
    const bool config = m_config->isChecked();
    auto *t = new LambdaTask(
        tr("Back up worlds"),
        [this, worlds, name, config](Task::Context &ctx) {
            Q_UNUSED(ctx);
            QString err;
            const QString made = m_stones->create(m_instanceId, name, worlds, config, false, &err);
            if (made.isEmpty()) {
                ctx.fail(err);
                return false;
            }
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    connect(t, &Task::finished, this, [this, t](bool ok) {
        t->deleteLater();
        if (!ok) {
            QMessageBox::warning(this, tr("Couldn't back up"), t->errorString());
            return;
        }
        m_name->clear();
        rebuild();
    });
    t->start();
    prog.exec();
}

void WorldsDialog::onRestore()
{
    auto *it = m_backups->currentItem();
    if (!it || it->data(Qt::UserRole).toString().isEmpty()) {
        return;
    }
    const QString name = it->data(Qt::UserRole).toString();
    auto rc = QMessageBox::question(
        this, tr("Restore this backup?"),
        tr("Restoring “%1” overwrites the current worlds/settings with the backup. "
           "Hearthlight preserves today's state first, so nothing is lost — but the live world will change. "
           "Continue?")
            .arg(name));
    if (rc != QMessageBox::Yes) {
        return;
    }
    auto *t = new LambdaTask(
        tr("Restore backup"),
        [this, name](Task::Context &ctx) {
            Q_UNUSED(ctx);
            QString err;
            if (!m_stones->restore(m_instanceId, name, &err)) {
                ctx.fail(err);
                return false;
            }
            return true;
        },
        this);
    TaskProgressDialog prog(t, this);
    connect(t, &Task::finished, this, [this, t, name](bool ok) {
        t->deleteLater();
        if (!ok) {
            QMessageBox::warning(this, tr("Couldn't restore"), t->errorString());
            return;
        }
        rebuild();
        QMessageBox::information(this, tr("Restored"), tr("“%1” is live now.").arg(name));
    });
    t->start();
    prog.exec();
}

void WorldsDialog::onDelete()
{
    auto *it = m_backups->currentItem();
    if (!it || it->data(Qt::UserRole).toString().isEmpty()) {
        return;
    }
    const QString name = it->data(Qt::UserRole).toString();
    auto rc = QMessageBox::question(this, tr("Delete this backup?"), tr("Delete backup “%1”?").arg(name));
    if (rc != QMessageBox::Yes) {
        return;
    }
    QString err;
    if (!m_stones->remove(m_instanceId, name, &err)) {
        QMessageBox::warning(this, tr("Couldn't delete"), err);
    }
    rebuild();
}

void WorldsDialog::onOpenFolder()
{
    const Instance in = m_instances->get(m_instanceId);
    if (!in.isValid()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(instanceGameDir(m_dataDir, in)).filePath(QStringLiteral("saves"))));
}

#include "WorldsDialog.moc"
