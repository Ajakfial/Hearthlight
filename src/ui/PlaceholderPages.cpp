#include "PlaceholderPages.h"

#include "IconProvider.h"

#include <QLabel>
#include <QVBoxLayout>

PlaceholderPage::PlaceholderPage(const QString &iconName, const QString &title, const QString &body, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setAlignment(Qt::AlignCenter);
    lay->setSpacing(12);
    lay->setContentsMargins(48, 48, 48, 48);

    auto *icon = new QLabel(this);
    icon->setPixmap(IconProvider::instance().pixmap(iconName, 64, QColor(QStringLiteral("#8E8E93"))));
    icon->setAlignment(Qt::AlignCenter);
    lay->addWidget(icon);

    auto *h = new QLabel(title, this);
    h->setAlignment(Qt::AlignCenter);
    QFont hf = h->font();
    hf.setPointSize(18);
    hf.setBold(true);
    h->setFont(hf);
    lay->addWidget(h);

    auto *b = new QLabel(body, this);
    b->setObjectName(QStringLiteral("secondary"));
    b->setAlignment(Qt::AlignCenter);
    b->setWordWrap(true);
    lay->addWidget(b);
}

#include "PlaceholderPages.moc"
