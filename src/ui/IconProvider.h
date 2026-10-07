#pragma once

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPixmap>
#include <QString>

// Loads hand-written 24x24 SVGs (1.75px rounded strokes, currentColor),
// recolors by state, caches renders at the right DPI.
class IconProvider : public QObject {
    Q_OBJECT
public:
    static IconProvider &instance();

    bool hasIcon(const QString &name) const;
    QIcon icon(const QString &name, const QColor &color = {}) const;
    QPixmap pixmap(const QString &name, int pixelSize, const QColor &color = {}) const;

    static QStringList allNames();

private:
    explicit IconProvider(QObject *parent = nullptr);
    QByteArray loadSvg(const QString &name, const QColor &color) const;

    mutable QHash<QString, QPixmap> m_cache;
};
