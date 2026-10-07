#pragma once

#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <functional>

class Task;

// Full Microsoft auth chain (spec 6.1):
//   Microsoft OAuth2 (device-code default, browser flow optional)
//   -> Xbox Live -> XSTS -> Minecraft services login
//   -> ownership check -> profile fetch (UUID, username, skins, capes).
//
// Refresh tokens live in SecureTokenStore, never in Account JSON.
// Minecraft access tokens are short-lived and kept in memory only
// (MicrosoftAccountProvider cache), never persisted.
//
// All blocking helpers are safe on Task worker threads (local
// QNetworkAccessManager + event loop, no GUI). The async device-code
// starter is for the login dialog on the GUI thread.
struct MicrosoftSkin {
    QString id;
    QString url;
    QString variant; // "classic" | "slim"
    QString state;
};

struct MicrosoftCape {
    QString id;
    QString url;
    QString state;
    QString alias;
};

struct MinecraftProfile {
    QString uuid; // dashed
    QString username;
    QList<MicrosoftSkin> skins;
    QList<MicrosoftCape> capes;
    bool ok = false;
};

struct DeviceCodeInfo {
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    QString verificationUriComplete;
    int expiresIn = 0;
    int interval = 5;
    bool ok = false;
};

struct MicrosoftLoginResult {
    bool ok = false;
    QString minecraftToken;
    qint64 minecraftExpiresIn = 0;
    QString msRefreshToken; // store in SecureTokenStore under "msRefresh"
    MinecraftProfile profile;
    bool hasOwnership = false;
    // Human-readable error for the UI + technical details for "Show details".
    QString error;
    QString errorDetails;
};

// Non-secret per-account profile cache (lives in
// <dataDir>/accounts/microsoft/<id>.json). No tokens here by design.
struct MicrosoftExtras {
    QString accountId;
    QString username;
    QString uuid;
    QString gamertag;
    QList<MicrosoftSkin> skins;
    QList<MicrosoftCape> capes;
    QString activeCapeId;
    bool hasOwnership = false;
    QString lastChecked; // ISO 8601

    QJsonObject toJson() const;
    static MicrosoftExtras fromJson(const QString &accountId, const QJsonObject &o);
};

QString microsoftExtrasDir(const QString &dataDir);
bool saveMicrosoftExtras(const QString &dataDir, const MicrosoftExtras &e);
MicrosoftExtras loadMicrosoftExtras(const QString &dataDir, const QString &accountId);

// Maps low-level failures to the spec's clear error messages.
namespace MicrosoftErrors {
QString friendlyFor(const QString &stage, int httpStatus, const QJsonObject &body, const QString &raw);
QString xboxMessage(qint64 xerr);
} // namespace MicrosoftErrors

class MicrosoftAuth : public QObject {
    Q_OBJECT
public:
    explicit MicrosoftAuth(QObject *parent = nullptr);

    // --- Async device-code flow (GUI thread, drives the login dialog) ---
    void startDeviceCode(const QString &clientId);
    void cancelPending();
    // Browser (authorization-code) flow: caller opens url in a browser and
    // pastes back the resulting code. We only build the URL + redeem the code.
    static QString browserAuthorizeUrl(const QString &clientId, const QString &redirectUri, const QString &state);
    void redeemAuthCode(const QString &clientId, const QString &code, const QString &redirectUri);

signals:
    void deviceCodeReady(const DeviceCodeInfo &info);
    void loginFinished(const MicrosoftLoginResult &result);
    void statusMessage(const QString &msg);

public:
    // --- Blocking full-chain helpers (worker threads / Task::Context) ---
    // Each returns a full login result (tokens + profile + ownership).
    static MicrosoftLoginResult loginWithDeviceCodeBlocking(const QString &clientId, const DeviceCodeInfo &dc,
                                                            std::function<bool()> cancelled,
                                                            std::function<void(const QString &)> status,
                                                            int timeoutSecs = 600);
    static MicrosoftLoginResult loginWithRefreshTokenBlocking(const QString &clientId, const QString &refreshToken,
                                                              QString *newRefreshOut = nullptr);
    static MicrosoftLoginResult redeemCodeBlocking(const QString &clientId, const QString &code,
                                                   const QString &redirectUri);
    static MinecraftProfile fetchProfileBlocking(const QString &minecraftToken, QString *error = nullptr,
                                                 QString *details = nullptr);
    static bool checkOwnershipBlocking(const QString &minecraftToken, QString *error = nullptr,
                                       QString *details = nullptr);
    static bool changeSkinByUrlBlocking(const QString &minecraftToken, const QString &url, const QString &variant,
                                        QString *error = nullptr);
    static bool setActiveCapeBlocking(const QString &minecraftToken, const QString &capeId, bool hide,
                                      QString *error = nullptr);

    // Pure-JSON step parsers (unit-tested, no network).
    static bool parseDeviceCode(const QJsonObject &o, DeviceCodeInfo *out);
    static bool parseTokenResponse(const QJsonObject &o, QString *access, QString *refresh, qint64 *expiresIn,
                                   QString *error, QString *details);
    static bool parseXblResponse(const QJsonObject &o, QString *token, QString *uhs, QString *error,
                                 QString *details);
    static bool parseXstsResponse(const QJsonObject &o, QString *token, QString *uhs, QString *error,
                                  QString *details);
    static bool parseMinecraftLogin(const QJsonObject &o, QString *token, qint64 *expiresIn, QString *error,
                                    QString *details);
    static MinecraftProfile parseMinecraftProfile(const QJsonObject &o);

private:
    void pollDeviceCode(const QString &clientId, const DeviceCodeInfo &dc);

    QNetworkAccessManager *m_nam = nullptr;
    bool m_cancelled = false;
    QString m_clientId;
};
