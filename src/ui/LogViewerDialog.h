#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// In-app log viewer: live view of Logger::recentLines with filter, search,
// auto-scroll and a Copy button.
class LogViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit LogViewerDialog(QWidget *parent = nullptr);

private slots:
    void refresh();
    void onCopy();

private:
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_auto = nullptr;
};
