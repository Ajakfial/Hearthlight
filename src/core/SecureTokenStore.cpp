#include "SecureTokenStore.h"

#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

SecureTokenStore::SecureTokenStore(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    QDir().mkpath(QDir(dataDir).filePath(QStringLiteral("accounts")));
}

QString SecureTokenStore::tokenLabel(const QString &accountId, const QString &key)
{
    return QStringLiteral("Hearthlight/%1/%2").arg(accountId, key);
}

QString SecureTokenStore::backendName() const
{
#if defined(Q_OS_WIN)
    return tr("Windows Credential Manager");
#elif defined(Q_OS_MACOS)
    return tr("macOS Keychain");
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty()) {
        return tr("Secret Service (secret-tool)");
    }
    return tr("Restricted file (fallback — no Secret Service found)");
#endif
}

QString SecureTokenStore::filePath() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("accounts/.tokens.json"));
}

namespace {
QJsonObject readTokenFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}
bool writeTokenFile(const QString &path, const QJsonObject &obj)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    f.close();
#if !defined(Q_OS_WIN)
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
#endif
    return true;
}
} // namespace

bool SecureTokenStore::fileSet(const QString &accountId, const QString &key, const QString &value) const
{
    const QString path = filePath();
    QJsonObject root = readTokenFile(path);
    QJsonObject acct = root.value(accountId).toObject();
    if (value.isEmpty()) {
        acct.remove(key);
    } else {
        acct[key] = QString::fromUtf8(value.toUtf8().toBase64());
    }
    if (acct.isEmpty()) {
        root.remove(accountId);
    } else {
        root[accountId] = acct;
    }
    return writeTokenFile(path, root);
}

QString SecureTokenStore::fileGet(const QString &accountId, const QString &key) const
{
    const QJsonObject root = readTokenFile(filePath());
    const QString b64 = root.value(accountId).toObject().value(key).toString();
    if (b64.isEmpty()) {
        return {};
    }
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

bool SecureTokenStore::fileClearAccount(const QString &accountId) const
{
    const QString path = filePath();
    QJsonObject root = readTokenFile(path);
    if (!root.contains(accountId)) {
        return true;
    }
    root.remove(accountId);
    return writeTokenFile(path, root);
}

bool SecureTokenStore::setToken(const QString &accountId, const QString &key, const QString &value)
{
    QMutexLocker lock(&m_mutex);
    if (accountId.isEmpty() || key.isEmpty()) {
        return false;
    }
#if defined(Q_OS_WIN)
    const QString target = tokenLabel(accountId, key);
    if (value.isEmpty()) {
        CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
        return fileSet(accountId, key, value);
    }
    const QByteArray blob = value.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = reinterpret_cast<LPWSTR>(const_cast<ushort *>(target.utf16()));
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&cred, 0)) {
        fileClearAccount(accountId); // don't leave a fallback copy behind
        // Best-effort: also drop any stale fallback entry for this key.
        fileSet(accountId, key, QString());
        return true;
    }
    Logger::warning(QStringLiteral("Credential Manager write failed (%1); using restricted file.").arg(GetLastError()));
    return fileSet(accountId, key, value);
#elif defined(Q_OS_MACOS)
    const QByteArray account = tokenLabel(accountId, key).toUtf8();
    const QByteArray service = QByteArray("Hearthlight");
    const QByteArray data = value.toUtf8();
    CFStringRef cfService =
        CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()), service.size(),
                                kCFStringEncodingUTF8, false);
    CFStringRef cfAccount =
        CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()), account.size(),
                                kCFStringEncodingUTF8, false);
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, cfService);
    CFDictionarySetValue(query, kSecAttrAccount, cfAccount);
    bool ok = false;
    if (value.isEmpty()) {
        ok = (SecItemDelete(query) == errSecSuccess);
        CFRelease(query);
        CFRelease(cfService);
        CFRelease(cfAccount);
        fileSet(accountId, key, value);
        return ok || true;
    }
    CFDataRef cfData = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(data.constData()), data.size());
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                              &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kSecValueData, cfData);
    OSStatus st = SecItemUpdate(query, attrs);
    if (st == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, cfData);
        st = SecItemAdd(query, nullptr);
    }
    ok = (st == errSecSuccess);
    CFRelease(attrs);
    CFRelease(cfData);
    CFRelease(query);
    CFRelease(cfService);
    CFRelease(cfAccount);
    if (!ok) {
        Logger::warning(QStringLiteral("Keychain write failed; using restricted file."));
        return fileSet(accountId, key, value);
    }
    fileSet(accountId, key, QString());
    return true;
#else
    // Linux / other: prefer Secret Service via secret-tool when present.
    if (!QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty() && !value.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("secret-tool"),
                { QStringLiteral("store"), QStringLiteral("--label"), tokenLabel(accountId, key),
                  QStringLiteral("application"), QStringLiteral("hearthlight"),
                  QStringLiteral("account"), accountId, QStringLiteral("key"), key });
        if (p.waitForStarted(5000)) {
            p.write(value.toUtf8());
            p.closeWriteChannel();
            if (p.waitForFinished(10000) && p.exitCode() == 0) {
                return true;
            }
        }
        Logger::warning(QStringLiteral("secret-tool store failed; using restricted file."));
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty() && value.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("secret-tool"),
                { QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("hearthlight"),
                  QStringLiteral("account"), accountId, QStringLiteral("key"), key });
        p.waitForFinished(8000);
    }
    return fileSet(accountId, key, value);
#endif
}

QString SecureTokenStore::token(const QString &accountId, const QString &key) const
{
    QMutexLocker lock(&m_mutex);
    if (accountId.isEmpty() || key.isEmpty()) {
        return {};
    }
#if defined(Q_OS_WIN)
    const QString target = tokenLabel(accountId, key);
    PCREDENTIALW cred = nullptr;
    if (CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &cred)) {
        const QString out = QString::fromUtf8(
            QByteArray(reinterpret_cast<const char *>(cred->CredentialBlob), (int)cred->CredentialBlobSize));
        CredFree(cred);
        if (!out.isEmpty()) {
            return out;
        }
    }
    return fileGet(accountId, key);
#elif defined(Q_OS_MACOS)
    const QByteArray account = tokenLabel(accountId, key).toUtf8();
    const QByteArray service = QByteArray("Hearthlight");
    CFStringRef cfService =
        CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()), service.size(),
                                kCFStringEncodingUTF8, false);
    CFStringRef cfAccount =
        CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()), account.size(),
                                kCFStringEncodingUTF8, false);
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, cfService);
    CFDictionarySetValue(query, kSecAttrAccount, cfAccount);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    QString out;
    if (SecItemCopyMatching(query, &result) == errSecSuccess && result) {
        CFDataRef d = (CFDataRef)result;
        out = QString::fromUtf8(
            QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(d)), (int)CFDataGetLength(d)));
        CFRelease(result);
    }
    CFRelease(query);
    CFRelease(cfService);
    CFRelease(cfAccount);
    return out.isEmpty() ? fileGet(accountId, key) : out;
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("secret-tool"),
                { QStringLiteral("lookup"), QStringLiteral("application"), QStringLiteral("hearthlight"),
                  QStringLiteral("account"), accountId, QStringLiteral("key"), key });
        if (p.waitForFinished(8000) && p.exitCode() == 0) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (!out.isEmpty()) {
                return out;
            }
        }
    }
    return fileGet(accountId, key);
#endif
}

bool SecureTokenStore::clearAccount(const QString &accountId)
{
    QMutexLocker lock(&m_mutex);
    if (accountId.isEmpty()) {
        return false;
    }
#if defined(Q_OS_WIN)
    for (const auto &k : { QStringLiteral("msRefresh"), QStringLiteral("mcToken") }) {
        CredDeleteW(reinterpret_cast<LPCWSTR>(tokenLabel(accountId, k).utf16()), CRED_TYPE_GENERIC, 0);
    }
    return fileClearAccount(accountId);
#elif defined(Q_OS_MACOS)
    // Delete any Hearthlight item for this account by re-issuing deletes per known key.
    for (const auto &k : { QStringLiteral("msRefresh"), QStringLiteral("mcToken") }) {
        const QByteArray account = tokenLabel(accountId, k).toUtf8();
        const QByteArray service = QByteArray("Hearthlight");
        CFStringRef cfService =
            CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()), service.size(),
                                    kCFStringEncodingUTF8, false);
        CFStringRef cfAccount =
            CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()), account.size(),
                                    kCFStringEncodingUTF8, false);
        CFMutableDictionaryRef query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, cfService);
        CFDictionarySetValue(query, kSecAttrAccount, cfAccount);
        SecItemDelete(query);
        CFRelease(query);
        CFRelease(cfService);
        CFRelease(cfAccount);
    }
    return fileClearAccount(accountId);
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty()) {
        for (const auto &k : { QStringLiteral("msRefresh"), QStringLiteral("mcToken") }) {
            QProcess p;
            p.start(QStringLiteral("secret-tool"),
                    { QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("hearthlight"),
                      QStringLiteral("account"), accountId, QStringLiteral("key"), k });
            p.waitForFinished(8000);
        }
    }
    return fileClearAccount(accountId);
#endif
}

bool SecureTokenStore::clearToken(const QString &accountId, const QString &key)
{
    return setToken(accountId, key, QString());
}
