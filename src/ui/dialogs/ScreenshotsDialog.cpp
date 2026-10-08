#include "dialogs/ScreenshotsDialog.h"

#include "Instance.h"
#include "InstanceManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

ScreenshotsDialog::ScreenshotsDialog(const QString &instanceId, InstanceManager *instances, const QString &dataDir,
                                     QWidget *parent)
    : QDialog(parent)
    , m_instanceId(instanceId)
    , m_instances(instances)
    , m_dataDir(dataDir)
{
    const Instance in = m_instances->get(m_instanceId);
    setWindowTitle(tr("Screenshots — %1").arg(in.isValid() ? in.name : m_instanceId));
    resize(720, 480);

    auto *lay = new QVBoxLayout(this);
    auto *split = new QSplitter(Qt::Horizontal, this);

    m_list = new QListWidget(split);
    m_list->setSelectionMode(QListWidget::SingleSelection);
    connect(m_list, &QListWidget::currentItemChanged, this, &ScreenshotsDialog::onPreview);
    split->addWidget(m_list);

    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_preview = new QLabel(right);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(320, 240);
    m_preview->setScaledContents(false);
    rl->addWidget(m_preview, 1);
    m_info = new QLabel(right);
    m_info->setObjectName(QStringLiteral("secondary"));
    m_info->setWordWrap(true);
    rl->addWidget(m_info);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    auto *row = new QHBoxLayout();
    auto *folderBtn = new QPushButton(tr("Open folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &ScreenshotsDialog::onOpenFolder);
    row->addWidget(folderBtn);
    auto *copyBtn = new QPushButton(tr("Copy path"), this);
    connect(copyBtn, &QPushButton::clicked, this, &ScreenshotsDialog::onCopyPath);
    row->addWidget(copyBtn);
    auto *delBtn = new QPushButton(tr("Delete"), this);
    connect(delBtn, &QPushButton::clicked, this, &ScreenshotsDialog::onDelete);
    row->addWidget(delBtn);
    row->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(closeBtn);
    lay->addLayout(row);

    rebuild();
}

QString ScreenshotsDialog::prettySize(qint64 bytes)
{
    if (bytes < 1024) {
        return tr("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return tr("%1 KB").arg(bytes / 1024);
    }
    return tr("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

void ScreenshotsDialog::rebuild()
{
    m_list->blockSignals(true);
    m_list->clear();
    const Instance in = m_instances->get(m_instanceId);
    QStringList names;
    if (in.isValid()) {
        QDir d(QDir(instanceGameDir(m_dataDir, in)).filePath(QStringLiteral("screenshots")));
        if (d.exists()) {
            names = d.entryList({ QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg") },
                                QDir::Files, QDir::Time | QDir::Reversed);
        }
    }
    if (names.isEmpty()) {
        auto *it = new QListWidgetItem(tr("(no screenshots yet — press F2 in game)"), m_list);
        it->setFlags(Qt::NoItemFlags);
    }
    for (const auto &n : names) {
        const Instance cur = m_instances->get(m_instanceId);
        const QString full =
            QDir(QDir(instanceGameDir(m_dataDir, cur)).filePath(QStringLiteral("screenshots"))).filePath(n);
        const QFileInfo fi(full);
        auto *it = new QListWidgetItem(
            QStringLiteral("%1  ·  %2").arg(n, prettySize(fi.size())), m_list);
        it->setData(Qt::UserRole, full);
        it->setToolTip(QLocale().toString(fi.lastModified(), QLocale::ShortFormat));
    }
    m_list->blockSignals(false);
    if (m_list->count() > 0 && m_list->currentRow() < 0) {
        m_list->setCurrentRow(0);
    }
    onPreview();
}

void ScreenshotsDialog::onPreview()
{
    auto *it = m_list->currentItem();
    const QString full = it ? it->data(Qt::UserRole).toString() : QString();
    if (full.isEmpty() || !QFileInfo::exists(full)) {
        m_preview->setText(tr("No preview"));
        m_info->clear();
        return;
    }
    QPixmap pm(full);
    if (pm.isNull()) {
        m_preview->setText(tr("Can't preview this file."));
    } else {
        m_preview->setPixmap(pm.scaled(m_preview->size().expandedTo(QSize(320, 240)), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    }
    const QFileInfo fi(full);
    m_info->setText(QStringLiteral("%1\n%2 · %3x%4")
                        .arg(fi.fileName(), prettySize(fi.size()))
                        .arg(pm.width())
                        .arg(pm.height()));
}

void ScreenshotsDialog::onOpenFolder()
{
    const Instance in = m_instances->get(m_instanceId);
    if (!in.isValid()) {
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QDir(instanceGameDir(m_dataDir, in)).filePath(QStringLiteral("screenshots"))));
}

void ScreenshotsDialog::onDelete()
{
    auto *it = m_list->currentItem();
    const QString full = it ? it->data(Qt::UserRole).toString() : QString();
    if (full.isEmpty()) {
        return;
    }
    auto rc = QMessageBox::question(this, tr("Delete this screenshot?"),
                                    tr("Delete “%1”?").arg(QFileInfo(full).fileName()));
    if (rc != QMessageBox::Yes) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (!QFile::moveToTrash(full)) {
        QFile::remove(full);
    }
#else
    QFile::remove(full);
#endif
    rebuild();
}

void ScreenshotsDialog::onCopyPath()
{
    auto *it = m_list->currentItem();
    const QString full = it ? it->data(Qt::UserRole).toString() : QString();
    if (!full.isEmpty()) {
        QApplication::clipboard()->setText(full);
    }
}

#include "ScreenshotsDialog.moc"
