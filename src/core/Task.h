#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

// Reusable async Task system driving every download/install with unified progress.
//
// Guarantees:
//  - never blocks the GUI thread (work runs on the global QThreadPool via QtConcurrent)
//  - progress / cancel / retry / chained subtasks
//  - signal/slot based so one progress UI can bind to any Task
//
// Example:
//   auto *t = new LambdaTask("Fetch manifest", [](TaskContext &ctx) {
//       ctx.report(0, 1, "connecting...");
//       if (ctx.isCancelled()) return false;
//       ...
//       return true;
//   });
//   QObject::connect(t, &Task::finished, ...);
//   t->start();
class Task : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString title READ title CONSTANT)
public:
    enum class Status { Pending = 0, Running, Succeeded, Failed, Cancelled };
    Q_ENUM(Status)

    explicit Task(QString title, QObject *parent = nullptr);
    ~Task() override;

    QString title() const { return m_title; }
    QString details() const { return m_details; }
    Status status() const { return m_status; }
    double progress() const; // 0.0..1.0, -1 if indeterminate
    qint64 received() const { return m_received; }
    qint64 total() const { return m_total; }
    QString errorString() const { return m_error; }
    bool canCancel() const { return m_cancellable; }
    int attempt() const { return m_attempt; }
    int maxRetries() const { return m_maxRetries; }
    void setMaxRetries(int n) { m_maxRetries = n; }
    bool isCancelled() const;

    // Subclasses implement the actual work. Return true on success.
    // Must poll ctx.isCancelled() and call ctx.report() periodically.
    class Context {
    public:
        virtual ~Context() = default;
        virtual void report(qint64 received, qint64 total, const QString &msg = {}) = 0;
        virtual bool isCancelled() const = 0;
        // Record a human-readable failure reason (thread-safe). Returning
        // false from execute() surfaces this via Task::errorString().
        virtual void fail(const QString &message) = 0;
    };

public slots:
    virtual void start();
    virtual void cancel();
    virtual void retry();

    // Run synchronously on the caller's thread (used by SequentialTaskGroup).
    // Returns true on success. Caller provides a Context for progress/cancel.
    bool runBlocking(Context &ctx) { return execute(ctx); }

signals:
    void progressChanged(qint64 received, qint64 total, const QString &message);
    void statusChanged(Status status);
    void finished(bool ok);

protected:
    virtual bool execute(Context &ctx) = 0;
    void setError(const QString &e) { m_error = e; }

    void setDetails(const QString &d);
    void setCancellable(bool c);

private:
    void runInBackground();
    void onWorkDone();

    class WatcherContext;

    QString m_title;
    QString m_details;
    Status m_status = Status::Pending;
    qint64 m_received = 0;
    qint64 m_total = -1;
    QString m_error;
    bool m_cancellable = true;
    bool m_cancelRequested = false;
    int m_attempt = 0;
    int m_maxRetries = 2;
    std::unique_ptr<QFutureWatcher<bool>> m_watcher;
};

// A Task built from a lambda.
class LambdaTask : public Task {
    Q_OBJECT
public:
    using Fn = std::function<bool(Context &)>;
    explicit LambdaTask(QString title, Fn fn, QObject *parent = nullptr);

protected:
    bool execute(Context &ctx) override;

private:
    Fn m_fn;
};

// Runs subtasks strictly in order; progress is the fraction of subtasks done
// blended with the running subtask's own progress. Cancelling cancels the rest.
class SequentialTaskGroup : public Task {
    Q_OBJECT
public:
    explicit SequentialTaskGroup(QString title, QObject *parent = nullptr);
    void addSubtask(Task *task); // takes ownership

protected:
    bool execute(Context &ctx) override;

private:
    QList<Task *> m_subtasks;
};
