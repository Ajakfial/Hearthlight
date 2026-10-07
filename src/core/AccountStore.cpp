#include "AccountStore.h"

#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

AccountStore::AccountStore(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    QDir().mkpath(QDir(dataDir).filePath(QStringLiteral("accounts/avatars")));
}

QString AccountStore::accountsFilePath() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("accounts/accounts.json"));
}

bool AccountStore::load()
{
    m_accounts.clear();
    m_activeId.clear();
    const QString path = accountsFilePath();
    QByteArray raw;
    {
        QFile f(path);
        if (!f.exists()) {
            emit loaded();
            return true; // first run: no file yet is fine
        }
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Logger::warning(QStringLiteral("Cannot read accounts file: %1").arg(path));
            emit loaded();
            return false;
        }
        raw = f.readAll();
    } // closed before any quarantine rename below (Windows locks open files)
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        // Quarantine the corrupt file, start clean, never crash.
        const QString backup =
            path + QStringLiteral(".corrupt-%1.bak").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
        QFile::rename(path, backup);
        Logger::warning(
            QStringLiteral("Corrupt accounts file quarantined to %1 (%2). Starting with no accounts.")
                .arg(backup)
                .arg(perr.errorString()));
        emit loaded();
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(1) > 2) {
        Logger::warning(QStringLiteral("Accounts file has newer schema; ignoring."));
        emit loaded();
        return false;
    }
    const QJsonArray arr = root.value(QStringLiteral("accounts")).toArray();
    for (const auto &v : arr) {
        if (!v.isObject()) {
            continue;
        }
        bool ok = false;
        const Account a = Account::fromJson(v.toObject(), &ok);
        if (!ok) {
            Logger::warning(QStringLiteral("Skipping invalid account entry in accounts.json"));
            continue;
        }
        if (hasDuplicate(a)) {
            Logger::warning(QStringLiteral("Skipping duplicate account entry: %1").arg(a.username));
            continue;
        }
        m_accounts.append(a);
    }
    const QString active = root.value(QStringLiteral("activeId")).toString();
    if (hasAccount(active)) {
        m_activeId = active;
    } else if (!m_accounts.isEmpty()) {
        m_activeId = m_accounts.first().id;
    }
    Logger::info(QStringLiteral("Loaded %1 account(s) from %2").arg(m_accounts.size()).arg(path));
    emit loaded();
    return true;
}

bool AccountStore::save()
{
    const QString path = accountsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray arr;
    for (const auto &a : m_accounts) {
        arr.append(a.toJson());
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("activeId")] = m_activeId;
    root[QStringLiteral("accounts")] = arr;
    QSaveFile f(path); // atomic write
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::error(QStringLiteral("Cannot write accounts file: %1").arg(path));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        Logger::error(QStringLiteral("Atomic commit failed for accounts file: %1").arg(path));
        return false;
    }
    emit changed();
    return true;
}

Account AccountStore::accountById(const QString &id) const
{
    for (const auto &a : m_accounts) {
        if (a.id.compare(id, Qt::CaseInsensitive) == 0) {
            return a;
        }
    }
    return {};
}

bool AccountStore::hasAccount(const QString &id) const
{
    return !id.isEmpty() && accountById(id).isValid();
}

bool AccountStore::hasDuplicate(const Account &a) const
{
    for (const auto &e : m_accounts) {
        if (e.id.compare(a.id, Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (e.uuid == a.uuid && !a.uuid.isNull()) {
            return true;
        }
        // Offline usernames collide case-insensitively (matches game behavior).
        if (e.type == AccountType::Offline && a.type == AccountType::Offline
            && e.username.compare(a.username, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool AccountStore::addAccount(const Account &a, QString *error)
{
    if (!a.isValid()) {
        if (error) {
            *error = tr("That profile is incomplete.");
        }
        return false;
    }
    if (hasDuplicate(a)) {
        if (error) {
            *error = a.type == AccountType::Offline
                ? tr("An offline account called “%1” already exists.").arg(a.username)
                : tr("That account is already added.");
        }
        return false;
    }
    m_accounts.append(a);
    if (m_activeId.isEmpty()) {
        m_activeId = a.id;
        emit activeAccountChanged(m_activeId);
    }
    save();
    emit accountAdded(a.id);
    emit changed();
    Logger::info(QStringLiteral("Account added: %1 (%2)").arg(a.username).arg(accountTypeBadge(a.type)));
    return true;
}

bool AccountStore::updateAccount(const Account &a, QString *error)
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts.at(i).id.compare(a.id, Qt::CaseInsensitive) == 0) {
            // Type must never silently change (Microsoft <-> Offline).
            if (m_accounts.at(i).type != a.type) {
                if (error) {
                    *error = tr("Account type cannot change. Create a new account instead.");
                }
                return false;
            }
            m_accounts[i] = a;
            save();
            emit changed();
            return true;
        }
    }
    if (error) {
        *error = tr("Account not found.");
    }
    return false;
}

bool AccountStore::removeAccount(const QString &id)
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts.at(i).id.compare(id, Qt::CaseInsensitive) == 0) {
            m_accounts.removeAt(i);
            if (m_activeId.compare(id, Qt::CaseInsensitive) == 0) {
                m_activeId = m_accounts.isEmpty() ? QString() : m_accounts.first().id;
                emit activeAccountChanged(m_activeId);
            }
            save();
            emit accountRemoved(id);
            emit changed();
            Logger::info(QStringLiteral("Account removed: %1").arg(id));
            return true;
        }
    }
    return false;
}

void AccountStore::setActiveAccountId(const QString &id)
{
    if (id == m_activeId) {
        return;
    }
    if (!id.isEmpty() && !hasAccount(id)) {
        return;
    }
    m_activeId = id;
    save();
    emit activeAccountChanged(m_activeId);
}

QList<Account> AccountStore::offlineAccounts() const
{
    QList<Account> out;
    for (const auto &a : m_accounts) {
        if (a.type == AccountType::Offline) {
            out.append(a);
        }
    }
    return out;
}

QList<Account> AccountStore::microsoftAccounts() const
{
    QList<Account> out;
    for (const auto &a : m_accounts) {
        if (a.type == AccountType::Microsoft) {
            out.append(a);
        }
    }
    return out;
}
