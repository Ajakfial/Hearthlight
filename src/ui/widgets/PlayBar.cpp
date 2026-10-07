#include "widgets/PlayBar.h"

#include "IconProvider.h"
#include "Task.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

PlayBar::PlayBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playbar"));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 10, 16, 10);
    lay->setSpacing(14);

    auto *left = new QVBoxLayout();
    m_profile = new QLabel(tr("No profiles yet"), this);
    m_profile->setObjectName(QStringLiteral("secondary"));
    left->addWidget(m_profile);
    m_account = new QLabel(tr("No account selected"), this);
    QFont af = m_account->font();
    af.setBold(true);
    m_account->setFont(af);
    left->addWidget(m_account);
    lay->addLayout(left, 1);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setMinimumWidth(220);
    m_progress->setVisible(false);
    lay->addWidget(m_progress);

    m_play = new QPushButton(IconProvider::instance().icon(QStringLiteral("play"), Qt::black), tr("Play"), this);
    m_play->setObjectName(QStringLiteral("playButton"));
    m_play->setToolTip(tr("Install (if needed) and play"));
    connect(m_play, &QPushButton::clicked, this, &PlayBar::playPressed);
    lay->addWidget(m_play);
}

void PlayBar::setAccountText(const QString &t)
{
    m_account->setText(t);
}

void PlayBar::setProfileText(const QString &t)
{
    m_profile->setText(t);
}

QPushButton *PlayBar::playButton() const
{
    return m_play;
}

void PlayBar::bindTask(Task *task)
{
    if (m_task) {
        disconnect(m_task, nullptr, this, nullptr);
        m_task = nullptr;
    }
    if (!task) {
        m_progress->setVisible(false);
        return;
    }
    m_task = task;
    m_progress->setRange(0, 1000);
    m_progress->setVisible(true);
    m_progress->setValue(0);
    connect(task, &Task::progressChanged, this, [this](qint64 r, qint64 t, const QString &) {
        if (t > 0) {
            m_progress->setRange(0, 1000);
            m_progress->setValue(static_cast<int>(r * 1000 / t));
        } else {
            m_progress->setRange(0, 0); // indeterminate
        }
    });
    connect(task, &Task::finished, this, [this] {
        m_task = nullptr;
        m_progress->setVisible(false);
        m_progress->setRange(0, 1000);
    });
}
