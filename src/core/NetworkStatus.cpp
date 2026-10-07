#include "NetworkStatus.h"

#include <QNetworkInformation>

QString offlineModeToString(OfflineMode m)
{
    switch (m) {
    case OfflineMode::Automatic: return QStringLiteral("automatic");
    case OfflineMode::AlwaysOffline: return QStringLiteral("always-offline");
    case OfflineMode::NeverOffline: return QStringLiteral("never-offline");
    }
    return QStringLiteral("automatic");
}

OfflineMode offlineModeFromString(const QString &s, bool *ok)
{
    const QString v = s.trimmed().toLower();
    if (ok) {
        *ok = true;
    }
    if (v == QStringLiteral("always-offline") || v == QStringLiteral("always") || v == QStringLiteral("offline")) {
        return OfflineMode::AlwaysOffline;
    }
    if (v == QStringLiteral("never-offline") || v == QStringLiteral("never") || v == QStringLiteral("online")) {
        return OfflineMode::NeverOffline;
    }
    if (v == QStringLiteral("automatic") || v == QStringLiteral("auto") || v.isEmpty()) {
        return OfflineMode::Automatic;
    }
    if (ok) {
        *ok = false;
    }
    return OfflineMode::Automatic;
}

NetworkStatus &NetworkStatus::instance()
{
    static NetworkStatus s;
    return s;
}

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent)
{
    if (QNetworkInformation::loadDefaultBackend()) {
        if (auto *info = QNetworkInformation::instance()) {
            auto update = [this, info] {
                const bool online =
                    info->reachability() != QNetworkInformation::Reachability::Disconnected;
                if (online != m_osReportsOnline) {
                    m_osReportsOnline = online;
                    emit changed();
                }
            };
            update();
            connect(info, &QNetworkInformation::reachabilityChanged, this,
                    [this, info](QNetworkInformation::Reachability) {
                        const bool online =
                            info->reachability() != QNetworkInformation::Reachability::Disconnected;
                        if (online != m_osReportsOnline) {
                            m_osReportsOnline = online;
                            emit changed();
                        }
                    });
        }
    }
}

OfflineMode NetworkStatus::mode() const
{
    return m_mode;
}

void NetworkStatus::setMode(OfflineMode m)
{
    if (m != m_mode) {
        m_mode = m;
        emit changed();
    }
}

bool NetworkStatus::isEffectivelyOffline() const
{
    if (m_mode == OfflineMode::AlwaysOffline) {
        return true;
    }
    if (m_mode == OfflineMode::NeverOffline) {
        return false;
    }
    return !m_osReportsOnline;
}

bool NetworkStatus::isOnline() const
{
    return !isEffectivelyOffline();
}
