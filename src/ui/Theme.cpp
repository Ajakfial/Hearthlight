#include "Theme.h"

#include <QApplication>
#include <QFont>

Theme::Theme(QObject *parent)
    : QObject(parent)
{
}

void Theme::setAccent(const QColor &c)
{
    if (c.isValid() && c != m_accent) {
        m_accent = c;
        emit changed();
    }
}

void Theme::setUiScale(double s)
{
    s = qBound(0.8, s, 2.0);
    if (!qFuzzyCompare(s, m_scale)) {
        m_scale = s;
        emit changed();
    }
}

void Theme::apply()
{
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setPalette(palette());
        app->setStyleSheet(stylesheet());
        QFont f = app->font();
        f.setPointSizeF(f.pointSizeF() * 1.0); // keep system UI font stack
        app->setFont(f);
    }
}

QPalette Theme::palette() const
{
    QPalette p;
    const QColor bg(QStringLiteral("#0A0A0A"));
    const QColor panel(QStringLiteral("#1C1C1E"));
    const QColor panel2(QStringLiteral("#2C2C2E"));
    const QColor text(QStringLiteral("#F2F2F7"));
    const QColor secondary(QStringLiteral("#8E8E93"));
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::Base, QColor(QStringLiteral("#000000")));
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, m_accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::PlaceholderText, secondary);
    p.setColor(QPalette::ToolTipBase, panel2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Disabled, QPalette::Text, secondary);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, secondary);
    return p;
}

QString Theme::stylesheet() const
{
    const QString accent = m_accent.name();
    const int base = qRound(13 * m_scale);
    // Keep QSS in one place. Accent appears ONLY on focus rings + Play button.
    return QStringLiteral(R"QSS(
* { outline: none; }
QMainWindow, QWidget#centralPage { background: #0A0A0A; color: #F2F2F7; font-size: %1pt; }
QWidget#sidebar { background: #000000; border-right: 1px solid #2C2C2E; }
QWidget#sidebar QPushButton {
    background: transparent; color: #F2F2F7; border: none; border-radius: 8px;
    padding: 10px 12px; text-align: left; font-size: %1pt;
}
QWidget#sidebar QPushButton:hover { background: #1C1C1E; }
QWidget#sidebar QPushButton:checked { background: #2C2C2E; }
QWidget#sidebar QPushButton:focus { border: 2px solid %2; }
QWidget#topbar { background: #000000; border-bottom: 1px solid #2C2C2E; }
QWidget#playbar { background: #000000; border-top: 1px solid #2C2C2E; }
QPushButton#playButton {
    background: %2; color: #000000; border: none; border-radius: 10px;
    padding: 12px 42px; font-weight: 700; font-size: 15pt;
}
QPushButton#playButton:hover:!disabled { background: %2; }
QPushButton#playButton:disabled { background: #2C2C2E; color: #8E8E93; }
QPushButton { background: #1C1C1E; color: #F2F2F7; border: 1px solid #2C2C2E; border-radius: 8px; padding: 8px 14px; }
QPushButton:hover { background: #2C2C2E; }
QPushButton:focus { border: 2px solid %2; }
QLineEdit, QComboBox, QSpinBox {
    background: #1C1C1E; color: #F2F2F7; border: 1px solid #2C2C2E;
    border-radius: 8px; padding: 8px 10px; selection-background-color: %2; selection-color: #000;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 2px solid %2; }
QLabel#secondary { color: #8E8E93; }
QLabel#badge {
    background: #2C2C2E; color: #F2F2F7; border-radius: 6px; padding: 2px 8px;
    font-size: 9pt; font-weight: 700;
}
QLabel#card { background: #1C1C1E; border: 1px solid #2C2C2E; border-radius: 12px; }
QProgressBar { background: #1C1C1E; border: 1px solid #2C2C2E; border-radius: 6px; text-align: center; color: #F2F2F7; }
QProgressBar::chunk { background: %2; border-radius: 5px; }
QToolTip { background: #2C2C2E; color: #F2F2F7; border: 1px solid #48484A; padding: 6px; }
QScrollBar:vertical { background: transparent; width: 12px; }
QScrollBar::handle:vertical { background: #2C2C2E; border-radius: 6px; min-height: 30px; }
)QSS")
        .arg(base)
        .arg(accent);
}

QStringList Theme::availableAccents()
{
    return { QStringLiteral("#FFB347"), QStringLiteral("#7FB069"), QStringLiteral("#6AAFE6"),
             QStringLiteral("#C58AF9"), QStringLiteral("#EF6461"), QStringLiteral("#F2F2F7") };
}
