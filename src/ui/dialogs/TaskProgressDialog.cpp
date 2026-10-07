#include "dialogs/TaskProgressDialog.h"

#include "Task.h"

TaskProgressDialog::TaskProgressDialog(Task *task, QWidget *parent)
    : QProgressDialog(parent)
    , m_task(task)
{
    setWindowTitle(task ? task->title() : tr("Working…"));
    setLabelText(tr("Starting…"));
    setRange(0, 1000);
    setValue(0);
    setMinimumDuration(250);
    setModal(true);
    if (!m_task) {
        return;
    }
    connect(m_task, &Task::progressChanged, this, &TaskProgressDialog::onProgress);
    connect(m_task, &Task::finished, this, &TaskProgressDialog::onFinished);
    connect(this, &QProgressDialog::canceled, m_task, &Task::cancel);
}

void TaskProgressDialog::onProgress(qint64 received, qint64 total, const QString &message)
{
    if (!message.isEmpty()) {
        setLabelText(message);
    }
    if (total > 0 && received >= 0) {
        setValue((int)qBound(qint64(0), received * 1000 / qMax<qint64>(1, total), qint64(1000)));
    } else {
        // Indeterminate: keep the dialog alive without a lying percentage.
        if (maximum() != 0) {
            setRange(0, 0);
        }
    }
}

void TaskProgressDialog::onFinished(bool ok)
{
    m_ok = ok;
    if (!ok && m_task) {
        m_error = m_task->errorString();
    }
    // Defer so the finished signal unwinds before we close the dialog.
    if (!isHidden()) {
        accept();
    } else {
        close();
    }
}

#include "TaskProgressDialog.moc"
