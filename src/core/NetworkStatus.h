#pragma once

#include <QObject>

// Global offline-mode capability (spec section 6 "Offline mode").
//
//   Automatic    - detect via QNetworkInformation when available; online by default.
//   AlwaysOffline- never attempt network-dependent auth/optional requests.
//   NeverOffline - always attempt network requests (errors surface normally).
enum class OfflineMode { Automatic = 0, AlwaysOffline = 1, NeverOffline = 2 };

QString offlineModeToString(OfflineMode m);
OfflineMode offlineModeFromString(const QString &s, bool *ok = nullptr);

// Central network-availability probe. The launcher must open and remain
// useful with no network; this object lets UI show "unavailable offline"
// instead of failing the whole app.
class NetworkStatus : public QObject {
    Q_OBJECT
public:
    static NetworkStatus &instance();

    OfflineMode mode() const;
    void setMode(OfflineMode m);

    // True when the launcher should avoid optional network work.
    bool isEffectivelyOffline() const;
    // True when online work is allowed AND the OS reports reachability.
    bool isOnline() const;

signals:
    void changed();

private:
    explicit NetworkStatus(QObject *parent = nullptr);
    OfflineMode m_mode = OfflineMode::Automatic;
    bool m_osReportsOnline = true;
};
