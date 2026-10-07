#pragma once

#include <QProgressDialog>

class Task;

// Modal progress bound to any Task: shows title + message, progress bar,
// Cancel wired to Task::cancel(). Auto-closes on finish; exposes whether the
// task succeeded and its error text.
class TaskProgressDialog : public QProgressDialog {
    Q_OBJECT
public:
    explicit TaskProgressDialog(Task *task, QWidget *parent = nullptr);

    bool succeeded() const { return m_ok; }
    QString errorText() const { return m_error; }

private slots:
    void onProgress(qint64 received, qint64 total, const QString &message);
    void onFinished(bool ok);

private:
    Task *m_task = nullptr;
    bool m_ok = false;
    QString m_error;
};
