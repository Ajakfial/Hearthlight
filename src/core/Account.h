#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>

// Account abstraction (spec section 6).
//
//  Account            common data for every account type
//    ├── id           stable local id (uuid string without braces)
//    ├── type         Microsoft | Offline
//    ├── username     display username
//    ├── uuid         player UUID (dashed form)
//    ├── displayName  UI display name (defaults to username)
//    └── avatar/color metadata (local only)
//
// Online-specific data (access tokens, XSTS tokens, …) is NEVER stored here.
// See AccountProvider.h / SecureTokenStore.

enum class AccountType { Microsoft = 0, Offline = 1 };

QString accountTypeToString(AccountType t);
AccountType accountTypeFromString(const QString &s, bool *ok = nullptr);
QString accountTypeBadge(AccountType t); // "MICROSOFT" / "OFFLINE"

struct Account {
    QString id; // QUuid without braces, lowercase
    AccountType type = AccountType::Offline;
    QString username;
    QUuid uuid; // player UUID
    QString displayName;
    QString avatarColor; // "#RRGGBB" local avatar tint for offline accounts
    QString avatarPath; // optional local skin/avatar file (offline, local-only)

    bool isValid() const { return !id.isEmpty() && !username.isEmpty() && !uuid.isNull(); }

    // "msa" for Microsoft, "legacy" for Offline. Matches the user_type field
    // the vanilla game expects in launch arguments.
    QString userTypeString() const;
    // 32-char lowercase hex without dashes, as ${auth_uuid} expects.
    QString uuidCompact() const;
    // Offline placeholder token. This is intentionally the constant "0":
    // clearly NOT a Microsoft JWT, never sent to any online service, only
    // used to fill the legacy launch template for local profiles.
    // Microsoft accounts always use a real token from SecureTokenStore.
    QString offlineAccessTokenPlaceholder() const { return QStringLiteral("0"); }

    QJsonObject toJson() const;
    static Account fromJson(const QJsonObject &obj, bool *ok = nullptr);
};

// Deterministic offline UUID, compatible with the Java ecosystem:
//   UUID.nameUUIDFromBytes(("OfflinePlayer:" + username).getBytes(UTF_8))
// i.e. MD5 of the UTF-8 bytes, then version nibble = 3, variant = RFC 4122.
// Documented in README ("how offline accounts work").
QUuid offlineUuidForUsername(const QString &username);

// Minecraft-compatible username rules used for validation:
// 3–16 chars, ASCII letters/digits/underscore. Offline names follow the same
// rule so skins/worlds behave like the Java ecosystem expects.
bool isValidMinecraftUsername(const QString &name);
QString usernameValidationHint();
