#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class Task;

// Persistent bottom bar: selected profile, selected account, big Play button
// with progress while installing/launching.
class PlayBar : public QWidget {
    Q_OBJECT
public:
    explicit PlayBar(QWidget *parent = nullptr);

    void setAccountText(const QString &t);
    void setProfileText(const QString &t);
    void bindTask(Task *task); // shows progress; nullptr clears
    QPushButton *playButton() const;

signals:
    void playPressed();

private:
    QLabel *m_profile = nullptr;
    QLabel *m_account = nullptr;
    QPushButton *m_play = nullptr;
    QProgressBar *m_progress = nullptr;
    QPointer<Task> m_task; // currently bound task (never owned)
};
