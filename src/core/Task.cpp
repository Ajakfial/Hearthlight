#include "Task.h"

#include "Logger.h"

#include <QtConcurrent>
#include <QCoreApplication>
#include <QThread>

class Task::WatcherContext : public Task::Context {
public:
    WatcherContext(Task *t)
        : m_task(t)
    {
    }
    void report(qint64 received, qint64 total, const QString &msg = {}) override
    {
        if (!m_task) {
            return;
        }
        // Hop back to the task's thread for signal emission.
        QMetaObject::invokeMethod(
            m_task,
            [t = m_task, received, total, msg] {
                t->m_received = received;
                t->m_total = total;
                if (!msg.isEmpty()) {
                    t->m_details = msg;
                }
                emit t->progressChanged(received, total, t->m_details);
            },
            Qt::QueuedConnection);
    }
    bool isCancelled() const override { return m_task && m_task->m_cancelRequested; }
    void fail(const QString &message) override
    {
        if (!m_task) {
            return;
        }
        if (QThread::currentThread() == m_task->thread()) {
            if (m_task->m_error.isEmpty()) {
                m_task->m_error = message;
            }
            return;
        }
        QMetaObject::invokeMethod(
            m_task,
            [t = m_task, message] {
                if (t->m_error.isEmpty()) {
                    t->m_error = message;
                }
            },
            Qt::BlockingQueuedConnection);
    }

private:
    Task *m_task = nullptr;
};

Task::Task(QString title, QObject *parent)
    : QObject(parent)
    , m_title(std::move(title))
{
}

Task::~Task() = default;

double Task::progress() const
{
    if (m_total <= 0) {
        return -1.0;
    }
    if (m_total == 0) {
        return 1.0;
    }
    const double p = static_cast<double>(m_received) / static_cast<double>(m_total);
    return qBound(0.0, p, 1.0);
}

bool Task::isCancelled() const
{
    return m_cancelRequested;
}

void Task::setDetails(const QString &d)
{
    m_details = d;
}

void Task::setCancellable(bool c)
{
    m_cancellable = c;
}

void Task::start()
{
    if (m_status == Status::Running) {
        return;
    }
    m_attempt = 0;
    m_cancelRequested = false;
    runInBackground();
}

void Task::cancel()
{
    if (m_status != Status::Running) {
        return;
    }
    m_cancelRequested = true;
    setDetails(tr("Cancelling…"));
}

void Task::retry()
{
    if (m_status == Status::Running) {
        return;
    }
    if (m_attempt > m_maxRetries) {
        m_attempt = 0;
    }
    m_cancelRequested = false;
    runInBackground();
}

void Task::runInBackground()
{
    m_status = Status::Running;
    m_error.clear();
    emit statusChanged(m_status);
    emit progressChanged(m_received, m_total, m_details);

    m_watcher = std::make_unique<QFutureWatcher<bool>>();
    Task *self = this;
    QObject::connect(m_watcher.get(), &QFutureWatcher<bool>::finished, self, [self] { self->onWorkDone(); },
                     Qt::QueuedConnection);
    auto fut = QtConcurrent::run([self]() -> bool {
        WatcherContext ctx(self);
        bool ok = false;
        try {
            ok = self->execute(ctx);
        } catch (const std::exception &e) {
            self->m_error = QString::fromLocal8Bit(e.what());
            return false;
        } catch (...) {
            self->m_error = QStringLiteral("Unknown error");
            return false;
        }
        if (ctx.isCancelled()) {
            return false;
        }
        return ok;
    });
    m_watcher->setFuture(fut);
}

void Task::onWorkDone()
{
    const bool wasCancelled = m_cancelRequested;
    bool ok = false;
    QString err = m_error;
    if (m_watcher && m_watcher->isFinished()) {
        ok = m_watcher->result();
    }
    if (wasCancelled) {
        m_status = Status::Cancelled;
        if (m_error.isEmpty()) {
            m_error = tr("Cancelled");
        }
        Logger::info(m_title + QStringLiteral(": cancelled"));
    } else if (ok) {
        m_status = Status::Succeeded;
        m_received = m_total > 0 ? m_total : m_received;
    } else {
        // Exponential-backoff retry is handled by owners (e.g. DownloadManager).
        // Here we simply mark failure; retry() can be invoked by UI.
        m_status = Status::Failed;
        if (m_error.isEmpty()) {
            m_error = tr("Task failed");
        }
        Logger::warning(m_title + QStringLiteral(": ") + m_error);
    }
    m_watcher.reset();
    emit statusChanged(m_status);
    emit finished(ok && !wasCancelled);
}

// ---- LambdaTask ----

LambdaTask::LambdaTask(QString title, Fn fn, QObject *parent)
    : Task(std::move(title), parent)
    , m_fn(std::move(fn))
{
}

bool LambdaTask::execute(Context &ctx)
{
    if (!m_fn) {
        return false;
    }
    return m_fn(ctx);
}

// ---- SequentialTaskGroup ----

SequentialTaskGroup::SequentialTaskGroup(QString title, QObject *parent)
    : Task(std::move(title), parent)
{
}

void SequentialTaskGroup::addSubtask(Task *task)
{
    task->setParent(this);
    m_subtasks.append(task);
}

bool SequentialTaskGroup::execute(Context &ctx)
{
    const int n = m_subtasks.size();
    if (n == 0) {
        return true;
    }
    // Run each subtask blocking on this worker thread, remapping its
    // 0..1 progress into our overall 0..1000 range.
    for (int i = 0; i < n; ++i) {
        if (ctx.isCancelled()) {
            return false;
        }
        Task *sub = m_subtasks.at(i);
        struct ForwardCtx : public Context {
            Context &outer;
            int index;
            int count;
            ForwardCtx(Context &o, int idx, int c)
                : outer(o)
                , index(idx)
                , count(c)
            {
            }
            void report(qint64 r, qint64 t, const QString &msg = {}) override
            {
                double frac = (t > 0 && r >= 0) ? (static_cast<double>(r) / static_cast<double>(t)) : 0.0;
                frac = qBound(0.0, frac, 1.0);
                const double overall = (static_cast<double>(index) + frac) / static_cast<double>(count);
                outer.report(static_cast<qint64>(overall * 1000.0), 1000, msg);
            }
            bool isCancelled() const override { return outer.isCancelled(); }
            void fail(const QString &message) override { outer.fail(message); }
        } fwd(ctx, i, n);
        ctx.report(static_cast<qint64>(i) * 1000 / (n > 0 ? n : 1), 1000, sub->title());
        const bool ok = sub->runBlocking(fwd);
        if (!ok || ctx.isCancelled()) {
            return false;
        }
    }
    ctx.report(1000, 1000, tr("Done"));
    return true;
}
