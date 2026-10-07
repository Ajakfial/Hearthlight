#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;

// Friendly placeholder: an SVG illustration, a calm headline and one
// next-step line — never a dead blank panel.
class PlaceholderPage : public QWidget {
    Q_OBJECT
public:
    explicit PlaceholderPage(const QString &iconName, const QString &title, const QString &body,
                             QWidget *parent = nullptr);
};
