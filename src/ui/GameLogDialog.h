#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

// Campfire log — the friendly live console while the game runs (spec 10.6).
// Streams stdout/stderr with severity filter, search, error navigation and
// auto-scroll, shows the exit code, offers Copy, and never prints tokens.
class GameLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit GameLogDialog(const QString &versionId, QWidget *parent = nullptr);

    void attach(QProcess *proc);
    void detach(); // forget the process (it is being deleted elsewhere)
    void setGameDir(const QString &dir);

    QString fullText() const;

private slots:
    void appendStdout();
    void appendStderr();
    void onFinished(int code);
    void onStop();
    void onCopy();
    void applyFilter();
    void onNextError();

private:
    void appendText(const QString &chunk);
    static int severityOf(const QString &line); // 0 info, 1 warning, 2 error

    QString m_version;
    QString m_gameDir;
    QProcess *m_proc = nullptr;
    qint64 m_startMs = 0;
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_level = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_counts = nullptr;
    bool m_auto = true;
    int m_errors = 0;
    int m_warnings = 0;
};
