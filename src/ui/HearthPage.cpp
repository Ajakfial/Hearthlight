#include "HearthPage.h"

#include "AccountStore.h"
#include "Embers.h"
#include "Hearthstones.h"
#include "IconProvider.h"
#include "InstanceManager.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "NetworkStatus.h"
#include "Task.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

HearthPage::HearthPage(AccountStore *accounts, InstanceManager *instances, Hearthstones *stones,
                       ModrinthApi *modrinth, ModManager *mods, Embers *embers, const QString &dataDir,
                       QWidget *parent)
    : QWidget(parent)
    , m_accounts(accounts)
    , m_instances(instances)
    , m_stones(stones)
    , m_modrinth(modrinth)
    , m_mods(mods)
    , m_embers(embers)
    , m_dataDir(dataDir)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 36, 40, 36);
    outer->setSpacing(16);

    m_greeting = new QLabel(this);
    QFont gf = m_greeting->font();
    gf.setPointSize(22);
    gf.setBold(true);
    m_greeting->setFont(gf);
    outer->addWidget(m_greeting);

    // Big Play card.
    auto *playCard = new QWidget(this);
    playCard->setObjectName(QStringLiteral("card"));
    auto *playLay = new QHBoxLayout(playCard);
    playLay->setContentsMargins(20, 18, 20, 18);
    auto *playText = new QVBoxLayout();
    m_playSub = new QLabel(playCard);
    m_playSub->setObjectName(QStringLiteral("secondary"));
    playText->addWidget(m_playSub);
    m_play = new QPushButton(IconProvider::instance().icon(QStringLiteral("play")), tr("Play"), playCard);
    m_play->setMinimumHeight(48);
    QFont pf = m_play->font();
    pf.setPointSize(14);
    pf.setBold(true);
    m_play->setFont(pf);
    m_play->setCursor(Qt::PointingHandCursor);
    connect(m_play, &QPushButton::clicked, this, &HearthPage::onPlayLast);
    playText->addWidget(m_play);
    playLay->addLayout(playText, 1);
    outer->addWidget(playCard);

    // Worlds + time row.
    auto *row = new QHBoxLayout();
    auto *worldCard = new QWidget(this);
    worldCard->setObjectName(QStringLiteral("card"));
    auto *worldLay = new QVBoxLayout(worldCard);
    auto *wh = new QLabel(tr("Recent worlds"), worldCard);
    QFont hf = wh->font();
    hf.setBold(true);
    wh->setFont(hf);
    worldLay->addWidget(wh);
    m_worlds = new QLabel(worldCard);
    m_worlds->setObjectName(QStringLiteral("secondary"));
    m_worlds->setWordWrap(true);
    worldLay->addWidget(m_worlds);
    row->addWidget(worldCard, 1);
    auto *timeCard = new QWidget(this);
    timeCard->setObjectName(QStringLiteral("card"));
    auto *timeLay = new QVBoxLayout(timeCard);
    auto *th = new QLabel(tr("Time at the hearth"), timeCard);
    th->setFont(hf);
    timeLay->addWidget(th);
    m_time = new QLabel(timeCard);
    m_time->setObjectName(QStringLiteral("secondary"));
    m_time->setWordWrap(true);
    timeLay->addWidget(m_time);
    row->addWidget(timeCard, 1);
    outer->addLayout(row);

    // Gentle suggestion + undo.
    auto *sugCard = new QWidget(this);
    sugCard->setObjectName(QStringLiteral("card"));
    auto *sugLay = new QHBoxLayout(sugCard);
    sugLay->setContentsMargins(20, 14, 20, 14);
    m_suggest = new QLabel(sugCard);
    m_suggest->setWordWrap(true);
    sugLay->addWidget(m_suggest, 1);
    m_suggestBtn = new QPushButton(tr("Review"), sugCard);
    m_suggestBtn->setVisible(false);
    connect(m_suggestBtn, &QPushButton::clicked, this, [this] {
        if (!m_updateInstance.isEmpty()) {
            emit reviewMods(m_updateInstance);
        }
    });
    sugLay->addWidget(m_suggestBtn);
    m_undoBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("undo")), tr("Undo last change"), sugCard);
    m_undoBtn->setVisible(false);
    connect(m_undoBtn, &QPushButton::clicked, this, &HearthPage::onUndo);
    sugLay->addWidget(m_undoBtn);
    outer->addWidget(sugCard);

    outer->addStretch(1);

    connect(m_instances, &InstanceManager::changed, this, &HearthPage::refresh);
    connect(m_accounts, &AccountStore::changed, this, &HearthPage::refresh);
    connect(m_accounts, &AccountStore::activeAccountChanged, this, &HearthPage::refresh);
    refresh();
}

QString HearthPage::playtimeText() const
{
    qint64 total = 0;
    int profiles = 0;
    for (const auto &in : m_instances->instances()) {
        total += in.playtimeSecs;
        ++profiles;
    }
    if (profiles == 0) {
        return tr("No adventures yet — your time here will add up quietly.");
    }
    const int hrs = (int)(total / 3600);
    const int mins = (int)((total % 3600) / 60);
    if (hrs == 0 && mins == 0) {
        return tr("%1 profile(s). Press Play and make some memories.").arg(profiles);
    }
    if (hrs == 0) {
        return tr("%1m around the fire across %2 profile(s).").arg(mins).arg(profiles);
    }
    return tr("%1h %2m around the fire across %3 profile(s).").arg(hrs).arg(mins).arg(profiles);
}

void HearthPage::refresh()
{
    const Account acc = m_accounts->activeAccount();
    m_greeting->setText(acc.isValid() ? tr("Welcome home, %1.").arg(acc.username) : tr("Welcome home."));
    const auto all = m_instances->instances();
    // Last played = most recent lastPlayed, else first by order.
    m_lastId.clear();
    QString lastName;
    QString lastMeta;
    QDateTime newest;
    for (const auto &in : all) {
        const QDateTime dt = QDateTime::fromString(in.lastPlayed, Qt::ISODate);
        if (m_lastId.isEmpty() || (dt.isValid() && (!newest.isValid() || dt > newest))) {
            m_lastId = in.id;
            lastName = in.name;
            lastMeta = in.versionId;
            newest = dt;
        }
    }
    if (m_lastId.isEmpty()) {
        m_play->setEnabled(false);
        m_playSub->setText(tr("No profiles yet — Kindling will set one up in a minute."));
        m_play->setText(tr("Play"));
    } else {
        m_play->setEnabled(true);
        m_playSub->setText(tr("Last played: %1 (Minecraft %2)").arg(lastName, lastMeta));
        m_play->setText(tr("Play %1").arg(lastName));
    }
    // Recent worlds across instances (folder names + relative time).
    struct W {
        QString profile;
        QString folder;
        qint64 mt;
    };
    QList<W> worlds;
    for (const auto &in : all) {
        for (const auto &w : m_stones->listWorlds(in.id)) {
            worlds.append({ in.name, w.folder, w.lastModifiedMs });
        }
    }
    std::sort(worlds.begin(), worlds.end(), [](const W &a, const W &b) { return a.mt > b.mt; });
    if (worlds.isEmpty()) {
        m_worlds->setText(tr("No worlds yet. They'll appear here after you play."));
    } else {
        QStringList lines;
        for (int i = 0; i < qMin(5, (int)worlds.size()); ++i) {
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(worlds.at(i).mt);
            const int days = (int)(QDateTime::currentDateTime().toMSecsSinceEpoch() - worlds.at(i).mt) / 86400000;
            const QString when = days <= 0 ? tr("today")
                : (days == 1                 ? tr("yesterday")
                                             : tr("%1 days ago").arg(days));
            Q_UNUSED(dt);
            lines.append(tr("• %1 — %2 (%3)").arg(worlds.at(i).folder, worlds.at(i).profile, when));
        }
        m_worlds->setText(lines.join(QLatin1Char('\n')));
    }
    m_time->setText(playtimeText());
    // Suggestion default; the update check refines it.
    m_suggest->setText(tr("Tip: right-click a profile for mods, backups, and settings."));
    m_suggestBtn->setVisible(false);
    // Undo availability.
    const QString lastTouched = m_embers ? m_embers->lastTouchedInstance() : QString();
    const bool canUndo = !lastTouched.isEmpty() && m_instances->has(lastTouched)
        && !m_embers->snapshots(lastTouched).isEmpty();
    m_undoBtn->setVisible(canUndo);
    if (canUndo) {
        m_undoBtn->setText(tr("Undo last change (%1)").arg(m_instances->get(lastTouched).name));
        m_undoBtn->setProperty("instanceId", lastTouched);
    }
    if (!m_checkingUpdates && NetworkStatus::instance().isOnline() && !all.isEmpty()) {
        onCheckUpdates();
    } else if (NetworkStatus::instance().isEffectivelyOffline()) {
        m_suggest->setText(tr("You're offline — everything installed keeps working."));
    }
}

void HearthPage::onPlayLast()
{
    if (!m_lastId.isEmpty()) {
        emit playRequested(m_lastId);
    } else {
        emit browseProfiles();
    }
}

void HearthPage::onUndo()
{
    const QString id = m_undoBtn->property("instanceId").toString();
    if (!id.isEmpty()) {
        emit undoRequested(id);
    }
}

void HearthPage::onCheckUpdates()
{
    if (m_checkingUpdates) {
        return;
    }
    m_checkingUpdates = true;
    auto *t = new LambdaTask(
        tr("Check for mod updates"),
        [this](Task::Context &ctx) {
            int total = 0;
            QString firstInst;
            for (const auto &in : m_instances->instances()) {
                if (ctx.isCancelled()) {
                    return false;
                }
                const auto mods = m_mods->listMods(in.id);
                bool hasTracked = false;
                for (const auto &m : mods) {
                    if (!m.manual) {
                        hasTracked = true;
                        break;
                    }
                }
                if (!hasTracked) {
                    continue;
                }
                const auto updates =
                    m_mods->checkUpdatesBlocking(in.id, in.versionId, in.loaderType, ctx, m_modrinth);
                if (!updates.isEmpty()) {
                    total += updates.size();
                    if (firstInst.isEmpty()) {
                        firstInst = in.id;
                    }
                }
            }
            m_updateCount = total;
            m_updateInstance = firstInst;
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        this);
    connect(t, &Task::finished, this, [this, t](bool ok) {
        m_checkingUpdates = false;
        t->deleteLater();
        if (!ok) {
            return; // stay quiet; updates are a suggestion, never an alarm
        }
        if (m_updateCount > 0) {
            m_suggest->setText(tr("%1 mod%2 ha%3 updates waiting.").arg(m_updateCount).arg(m_updateCount == 1 ? QString() : tr("s")).arg(m_updateCount == 1 ? tr("s") : tr("ve")));
            m_suggestBtn->setVisible(true);
        }
    });
    t->start();
}

#include "HearthPage.moc"
