#include "widgets/AccountSwitcher.h"

#include "AccountStore.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>

namespace {
QPixmap dotAvatar(const Account &a)
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(a.avatarColor.isEmpty() ? QStringLiteral("#8E8E93") : a.avatarColor));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, 28, 28);
    p.setPen(Qt::black);
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, 28, 28), Qt::AlignCenter, a.username.left(1).toUpper());
    return pm;
}
QPixmap headOrDot(const QString &dataDir, const Account &a)
{
    const QString p = QDir(dataDir).filePath(QStringLiteral("accounts/avatars/%1.png").arg(a.id.toLower()));
    if (a.type == AccountType::Microsoft && QFile::exists(p)) {
        QPixmap pm(p);
        if (!pm.isNull()) {
            return pm.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    return dotAvatar(a);
}
} // namespace

AccountSwitcher::AccountSwitcher(AccountStore *store, const QString &dataDir, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_dataDir(dataDir)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    m_box->setMinimumWidth(220);
    m_box->setToolTip(tr("Switch account. Badges show MICROSOFT (online) vs OFFLINE (local)."));
    lay->addWidget(m_box);
    connect(m_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        const QString id = m_box->itemData(i).toString();
        if (!id.isEmpty()) {
            m_store->setActiveAccountId(id);
        }
    });
    connect(m_store, &AccountStore::changed, this, &AccountSwitcher::rebuild);
    connect(m_store, &AccountStore::activeAccountChanged, this, &AccountSwitcher::rebuild);
    rebuild();
}

void AccountSwitcher::rebuild()
{
    m_box->blockSignals(true);
    m_box->clear();
    const auto list = m_store->accounts();
    if (list.isEmpty()) {
        m_box->addItem(tr("No account — visit Accounts"), QString());
    }
    for (const auto &a : list) {
        // e.g. "River   [OFFLINE]"
        m_box->addItem(headOrDot(m_dataDir, a),
                       QStringLiteral("%1   [%2]").arg(a.username).arg(accountTypeBadge(a.type)), a.id);
    }
    const int idx = m_box->findData(m_store->activeAccountId());
    m_box->setCurrentIndex(idx < 0 ? 0 : idx);
    m_box->blockSignals(false);
}

#include "AccountSwitcher.moc"
