#pragma once

#include "Account.h"
#include "Task.h"

#include <QMap>
#include <QObject>
#include <QString>

class MicrosoftAuth;
class SecureTokenStore;
class Task;

// Extensible authentication-provider interface (spec section 6):
//
//   IAccountProvider
//   ├── MicrosoftAccountProvider  (OAuth2 device-code flow + browser fallback)
//   └── OfflineAccountProvider    (local only, no network ever)
//
// The AccountStore talks to providers through this API; provider-specific
// auth logic stays isolated behind it.

// Simple value-or-error result without exceptions.
template <typename T> struct ProviderResult {
    bool ok = false;
    T value{};
    QString error;
    QString errorDetails;
    static ProviderResult<T> success(const T &v) { return { true, v, {}, {} }; }
    static ProviderResult<T> failure(const QString &e, const QString &d = {})
    {
        return { false, T{}, e, d };
    }
};

class IAccountProvider : public QObject {
    Q_OBJECT
public:
    explicit IAccountProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual QString providerId() const = 0; // "microsoft" | "offline"
    virtual QString displayName() const = 0;
    // Whether this provider can run right now (e.g. Microsoft needs network).
    virtual bool isAvailable(QString *reason = nullptr) const = 0;
};

// Offline accounts are local-only: no network, no Microsoft/Mojang/Xbox
// contact, no tokens.
class OfflineAccountProvider : public IAccountProvider {
    Q_OBJECT
public:
    explicit OfflineAccountProvider(QObject *parent = nullptr);

    QString providerId() const override { return QStringLiteral("offline"); }
    QString displayName() const override { return tr("Offline Account"); }
    bool isAvailable(QString *reason = nullptr) const override;

    struct CreateOptions {
        QString username;
        QString avatarColor; // optional "#RRGGBB"
        bool useCustomUuid = false;
        QUuid customUuid; // only when useCustomUuid (advanced toggle)
    };

    ProviderResult<Account> createAccount(const CreateOptions &opts) const;

    static QStringList presetAvatarColors();
};

// Microsoft authentication (device-code flow by default, browser
// auth-code flow as fallback).
// Refresh tokens live in SecureTokenStore under "msRefresh"; short-lived
// Minecraft access tokens are cached in memory only (never persisted).
class MicrosoftAccountProvider : public IAccountProvider {
    Q_OBJECT
public:
    // dataDir is where microsoft/*.json extras live. tokens may be shared
    // (owned elsewhere, e.g. Application) or created internally.
    explicit MicrosoftAccountProvider(const QString &dataDir, SecureTokenStore *tokens = nullptr,
                                      QObject *parent = nullptr);

    QString providerId() const override { return QStringLiteral("microsoft"); }
    QString displayName() const override { return tr("Microsoft"); }
    bool isAvailable(QString *reason = nullptr) const override;

    QString notImplementedMessage() const { return {}; } // kept for compat; always available online now

    MicrosoftAuth *auth() const { return m_auth; }
    SecureTokenStore *tokenStore() const { return m_tokens; }
    QString dataDir() const { return m_dataDir; }

    // In-memory Minecraft token (empty when unknown/expired).
    QString cachedMinecraftToken(const QString &accountId) const;
    // Silent refresh before launch. Blocking: call on a Task worker thread.
    // On success returns a fresh Minecraft access token and rotates the
    // stored refresh token; updates extras + outAccount username/uuid.
    QString refreshBlocking(const QString &accountId, Account *outAccount, Task::Context &ctx);
    // Same, but with explicit error strings instead of a Task context.
    QString refreshBlocking(const QString &accountId, Account *outAccount, QString *error, QString *details);

    void forgetAccount(const QString &accountId); // clears tokens + memory cache
    void setCachedMinecraftToken(const QString &accountId, const QString &token, qint64 expiresInSecs);

signals:
    void loginNotAvailable();
    void accountRefreshed(const QString &accountId);
    void loginFinished(const QString &accountId, bool ok, const QString &error);

private:
    QString m_dataDir;
    SecureTokenStore *m_tokens = nullptr;
    bool m_ownsTokens = false;
    MicrosoftAuth *m_auth = nullptr;
    struct CachedSession {
        QString mcToken;
        qint64 expiresAt = 0; // secs since epoch
    };
    QMap<QString, CachedSession> m_sessions;
};
