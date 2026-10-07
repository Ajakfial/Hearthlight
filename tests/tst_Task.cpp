// Tests: Task progress/cancel/chained subtasks (offline, no network).
#include "Task.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

class TstTask : public QObject {
    Q_OBJECT
private slots:
    void lambdaSuccess();
    void cancel();
    void sequentialGroup();
};

void TstTask::lambdaSuccess()
{
    auto *t = new LambdaTask(QStringLiteral("work"), [](Task::Context &ctx) {
        ctx.report(1, 2, QStringLiteral("half"));
        return true;
    });
    QSignalSpy finished(t, &Task::finished);
    t->start();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(0).toBool(), true);
    QCOMPARE(t->status(), Task::Status::Succeeded);
    t->deleteLater();
}

void TstTask::cancel()
{
    auto *t = new LambdaTask(
        QStringLiteral("long"), [](Task::Context &ctx) {
            for (int i = 0; i < 100; ++i) {
                if (ctx.isCancelled()) {
                    return false;
                }
                ctx.report(i, 100);
                QThread::msleep(20);
            }
            return true;
        });
    QSignalSpy finished(t, &Task::finished);
    t->start();
    QTest::qWait(150);
    t->cancel();
    QVERIFY(finished.wait(5000));
    QCOMPARE(t->status(), Task::Status::Cancelled);
    t->deleteLater();
}

void TstTask::sequentialGroup()
{
    auto *g = new SequentialTaskGroup(QStringLiteral("group"));
    int ran = 0;
    for (int i = 0; i < 3; ++i) {
        g->addSubtask(new LambdaTask(QStringLiteral("sub"), [&ran](Task::Context &ctx) {
            ctx.report(1, 1);
            ++ran;
            return true;
        }));
    }
    QSignalSpy finished(g, &Task::finished);
    g->start();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(0).toBool(), true);
    QCOMPARE(ran, 3);
    g->deleteLater();
}

QTEST_MAIN(TstTask)
#include "tst_Task.moc"
