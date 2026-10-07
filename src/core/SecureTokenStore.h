#pragma once

#include <QMutex>
#include <QObject>
#include <QString>

// Secure OS-credential storage for Microsoft refresh tokens.
// Offline accounts never touch this store.
//
// Backends (chosen at runtime, reported by backendName()):
//  - Windows: Windows Credential Manager (CredWrite/CredRead/CredDelete)
//  - macOS: Keychain via SecItem API
//  - Linux: Secret Service via `secret-tool` CLI when available
//  - Fallback everywhere: restricted-permission file under
//    <dataDir>/accounts/.tokens.json (0600 on Unix). The fallback is
//    honest about what it is: file permissions, not encryption. Tokens
//    are never written to accounts.json or logs.
class SecureTokenStore : public QObject {
    Q_OBJECT
public:
    explicit SecureTokenStore(const QString &dataDir, QObject *parent = nullptr);

    virtual QString backendName() const;

    // All synchronous and thread-safe. Empty value means missing.
    virtual bool setToken(const QString &accountId, const QString &key, const QString &value);
    virtual QString token(const QString &accountId, const QString &key) const;
    virtual bool clearAccount(const QString &accountId);
    virtual bool clearToken(const QString &accountId, const QString &key);

    static QString tokenLabel(const QString &accountId, const QString &key);

private:
    bool fileSet(const QString &accountId, const QString &key, const QString &value) const;
    QString fileGet(const QString &accountId, const QString &key) const;
    bool fileClearAccount(const QString &accountId) const;
    QString filePath() const;

    QString m_dataDir;
    mutable QMutex m_mutex;
};
