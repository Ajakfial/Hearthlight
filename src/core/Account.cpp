#include "Account.h"

#include <QCryptographicHash>
#include <QObject>
#include <QRegularExpression>

QString accountTypeToString(AccountType t)
{
    return t == AccountType::Microsoft ? QStringLiteral("microsoft") : QStringLiteral("offline");
}

AccountType accountTypeFromString(const QString &s, bool *ok)
{
    const QString v = s.trimmed().toLower();
    if (ok) {
        *ok = true;
    }
    if (v == QStringLiteral("microsoft") || v == QStringLiteral("msa")) {
        return AccountType::Microsoft;
    }
    if (v == QStringLiteral("offline") || v == QStringLiteral("local")) {
        return AccountType::Offline;
    }
    if (ok) {
        *ok = false;
    }
    return AccountType::Offline;
}

QString accountTypeBadge(AccountType t)
{
    return t == AccountType::Microsoft ? QStringLiteral("MICROSOFT") : QStringLiteral("OFFLINE");
}

QString Account::userTypeString() const
{
    return type == AccountType::Microsoft ? QStringLiteral("msa") : QStringLiteral("legacy");
}

QString Account::uuidCompact() const
{
    return uuid.toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
}

QJsonObject Account::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("type")] = accountTypeToString(type);
    o[QStringLiteral("username")] = username;
    o[QStringLiteral("uuid")] = uuid.toString(QUuid::WithoutBraces);
    o[QStringLiteral("displayName")] = displayName.isEmpty() ? username : displayName;
    if (!avatarColor.isEmpty()) {
        o[QStringLiteral("avatarColor")] = avatarColor;
    }
    if (!avatarPath.isEmpty()) {
        o[QStringLiteral("avatarPath")] = avatarPath;
    }
    o[QStringLiteral("schemaVersion")] = 1;
    // NOTE: no tokens of any kind are stored here by design.
    return o;
}

Account Account::fromJson(const QJsonObject &obj, bool *ok)
{
    Account a;
    bool good = true;
    a.id = obj.value(QStringLiteral("id")).toString();
    bool typeOk = false;
    a.type = accountTypeFromString(obj.value(QStringLiteral("type")).toString(), &typeOk);
    good = good && typeOk && !a.id.isEmpty();
    a.username = obj.value(QStringLiteral("username")).toString();
    a.uuid = QUuid::fromString(obj.value(QStringLiteral("uuid")).toString());
    a.displayName = obj.value(QStringLiteral("displayName")).toString(a.username);
    a.avatarColor = obj.value(QStringLiteral("avatarColor")).toString();
    a.avatarPath = obj.value(QStringLiteral("avatarPath")).toString();
    good = good && !a.username.isEmpty() && !a.uuid.isNull();
    // Defensive: refuse files that smuggle credentials into the account store.
    static const char *kForbidden[] = { "accessToken", "access_token", "refreshToken", "refresh_token", "xsts",
                                        "uhs", "msaToken" };
    for (const char *key : kForbidden) {
        if (obj.contains(QString::fromLatin1(key))) {
            good = false;
            break;
        }
    }
    if (ok) {
        *ok = good;
    }
    return a;
}

QUuid offlineUuidForUsername(const QString &username)
{
    // Java: UUID.nameUUIDFromBytes(("OfflinePlayer:" + name).getBytes(UTF_8))
    const QByteArray input = QStringLiteral("OfflinePlayer:").toUtf8() + username.toUtf8();
    QByteArray md5 = QCryptographicHash::hash(input, QCryptographicHash::Md5); // 16 bytes
    Q_ASSERT(md5.size() == 16);
    // Set version (3 = MD5 name-based) and RFC 4122 variant bits.
    md5[6] = static_cast<char>((md5[6] & 0x0f) | 0x30);
    md5[8] = static_cast<char>((md5[8] & 0x3f) | 0x80);
    return QUuid::fromRfc4122(md5);
}

bool isValidMinecraftUsername(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    return re.match(name).hasMatch();
}

QString usernameValidationHint()
{
    return QObject::tr("Usernames need 3–16 characters: letters, numbers and underscore only.");
}
