#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

// Central theme: one QSS stylesheet + one QPalette.
//
// Palette (dark, high-contrast):
//   primary text  #F2F2F7   surfaces #000000 / #0A0A0A
//   panels        #1C1C1E / #2C2C2E   secondary text #8E8E93
//   accent (Play button + focus rings only): warm amber #FFB347, user-selectable.
class Theme : public QObject {
    Q_OBJECT
public:
    explicit Theme(QObject *parent = nullptr);

    QColor accent() const { return m_accent; }
    double uiScale() const { return m_scale; }

    void setAccent(const QColor &c);
    void setUiScale(double s);

    // Applies palette + stylesheet to the whole application.
    void apply();

    QString stylesheet() const;
    QPalette palette() const;

    static QStringList availableAccents(); // hex strings
    static QColor defaultAccent() { return QColor(QStringLiteral("#FFB347")); }

signals:
    void changed();

private:
    QColor m_accent = defaultAccent();
    double m_scale = 1.0;
};
