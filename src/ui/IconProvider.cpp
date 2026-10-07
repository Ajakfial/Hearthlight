#include "IconProvider.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

IconProvider &IconProvider::instance()
{
    static IconProvider s;
    return s;
}

IconProvider::IconProvider(QObject *parent)
    : QObject(parent)
{
    // Pin the compiled :/icons resources into the binary. Without this,
    // linkers may drop the qrc object from the static UI lib (nothing else
    // references its symbols) and every icon silently degrades to a box.
    Q_INIT_RESOURCE(icons);
}

QStringList IconProvider::allNames()
{
    return { QStringLiteral("home"),       QStringLiteral("play"),     QStringLiteral("stop"),
             QStringLiteral("profile"),    QStringLiteral("plus"),     QStringLiteral("search"),
             QStringLiteral("download"),   QStringLiteral("update"),   QStringLiteral("settings"),
             QStringLiteral("account"),    QStringLiteral("offline"),  QStringLiteral("mod"),
             QStringLiteral("resource"),   QStringLiteral("shader"),   QStringLiteral("world"),
             QStringLiteral("folder"),     QStringLiteral("trash"),    QStringLiteral("copy"),
             QStringLiteral("export"),     QStringLiteral("import"),   QStringLiteral("clone"),
             QStringLiteral("edit"),       QStringLiteral("check"),    QStringLiteral("toggle"),
             QStringLiteral("info"),       QStringLiteral("warning"),  QStringLiteral("error"),
             QStringLiteral("undo"),       QStringLiteral("snapshot"), QStringLiteral("log"),
             QStringLiteral("crash"),      QStringLiteral("loader"),   QStringLiteral("logo") };
}

bool IconProvider::hasIcon(const QString &name) const
{
    return QFile::exists(QStringLiteral(":/icons/%1.svg").arg(name));
}

QByteArray IconProvider::loadSvg(const QString &name, const QColor &color) const
{
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray data = f.readAll();
    if (color.isValid()) {
        data.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    }
    return data;
}

QPixmap IconProvider::pixmap(const QString &name, int pixelSize, const QColor &color) const
{
    qreal dpr = 1.0;
    if (qApp && qApp->primaryScreen()) {
        dpr = qApp->primaryScreen()->devicePixelRatio();
    }
    const QColor useColor = color.isValid() ? color : QColor(QStringLiteral("#F2F2F7"));
    const QString key = QStringLiteral("%1@%2@%3").arg(name).arg(pixelSize).arg(useColor.name());
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        return it.value();
    }
    QPixmap pm(qRound(pixelSize * dpr), qRound(pixelSize * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QSvgRenderer renderer(loadSvg(name, useColor));
    if (renderer.isValid()) {
        QPainter p(&pm);
        renderer.render(&p, QRect(0, 0, pixelSize, pixelSize));
    } else {
        // Fallback glyph so a missing icon never breaks layout.
        QPainter p(&pm);
        p.setPen(useColor);
        p.drawRect(2, 2, pixelSize - 4, pixelSize - 4);
    }
    m_cache.insert(key, pm);
    return pm;
}

QIcon IconProvider::icon(const QString &name, const QColor &color) const
{
    const QColor base = color.isValid() ? color : QColor(QStringLiteral("#F2F2F7"));
    QIcon ic;
    ic.addPixmap(pixmap(name, 16, base), QIcon::Normal, QIcon::Off);
    ic.addPixmap(pixmap(name, 24, base), QIcon::Normal, QIcon::On);
    ic.addPixmap(pixmap(name, 24, QColor(QStringLiteral("#8E8E93"))), QIcon::Disabled, QIcon::Off);
    return ic;
}
