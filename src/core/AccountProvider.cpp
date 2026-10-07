#include "AccountProvider.h"

#include "Constants.h"
#include "Logger.h"
#include "MicrosoftAuth.h"
#include "NetworkStatus.h"
#include "SecureTokenStore.h"
#include "Task.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

OfflineAccountProvider::OfflineAccountProvider(QObject *parent)
    : IAccountProvider(parent)
{
}

bool OfflineAccountProvider::isAvailable(QString *reason) const
{
    Q_UNUSED(reason);
    return true; // local only: always available, even offline
}

ProviderResult<Account> OfflineAccountProvider::createAccount(const CreateOptions &opts) const
{
    const QString name = opts.username.trimmed();
    if (!isValidMinecraftUsername(name)) {
        return ProviderResult<Account>::failure(tr("That username won't work."),
                                                usernameValidationHint());
    }
    Account a;
    a.type = AccountType::Offline;
    a.username = name;
    a.displayName = name;
    a.uuid = (opts.useCustomUuid && !opts.customUuid.isNull()) ? opts.customUuid : offlineUuidForUsername(name);
    a.id = a.uuid.toString(QUuid::WithoutBraces).toLower();
    a.avatarColor = opts.avatarColor.isEmpty() ? QStringLiteral("#8E8E93") : opts.avatarColor;
    return ProviderResult<Account>::success(a);
}

QStringList OfflineAccountProvider::presetAvatarColors()
{
    return { QStringLiteral("#FFB347"), QStringLiteral("#7FB069"), QStringLiteral("#6AAFE6"),
             QStringLiteral("#C58AF9"), QStringLiteral("#EF6461"), QStringLiteral("#8E8E93") };
}

MicrosoftAccountProvider::MicrosoftAccountProvider(const QString &dataDir, SecureTokenStore *tokens, QObject *parent)
    : IAccountProvider(parent)
    , m_dataDir(dataDir)
    , m_tokens(tokens)
{
    if (!m_tokens) {
        m_tokens = new SecureTokenStore(dataDir, this);
        m_ownsTokens = true;
    }
    m_auth = new MicrosoftAuth(this);
}

bool MicrosoftAccountProvider::isAvailable(QString *reason) const
{
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        if (reason) {
            *reason = tr("Microsoft sign-in needs an internet connection.");
        }
        return false;
    }
    return true;
}

QString MicrosoftAccountProvider::cachedMinecraftToken(const QString &accountId) const
{
    const auto it = m_sessions.find(accountId.toLower());
    if (it == m_sessions.end()) {
        return {};
    }
    // Refresh a little early (5 min margin).
    if (it->expiresAt <= QDateTime::currentSecsSinceEpoch() + 300) {
        return {};
    }
    return it->mcToken;
}

void MicrosoftAccountProvider::setCachedMinecraftToken(const QString &accountId, const QString &token,
                                                       qint64 expiresInSecs)
{
    if (token.isEmpty()) {
        m_sessions.remove(accountId.toLower());
        return;
    }
    CachedSession s;
    s.mcToken = token;
    s.expiresAt = QDateTime::currentSecsSinceEpoch() + qMax<qint64>(60, expiresInSecs);
    m_sessions.insert(accountId.toLower(), s);
}

void MicrosoftAccountProvider::forgetAccount(const QString &accountId)
{
    m_sessions.remove(accountId.toLower());
    if (m_tokens) {
        m_tokens->clearAccount(accountId.toLower());
    }
    // Extras file keeps the last-known profile (useful offline display);
    // it contains no secrets so leaving it is safe. Remove it too so a
    // deleted account fully disappears.
    QFile::remove(QDir(m_dataDir).filePath(QStringLiteral("accounts/microsoft/%1.json").arg(accountId.toLower())));
}

QString MicrosoftAccountProvider::refreshBlocking(const QString &accountId, Account *outAccount, QString *error,
                                                  QString *details)
{
    const QString id = accountId.toLower();
    const QString cached = cachedMinecraftToken(id);
    if (!cached.isEmpty()) {
        return cached;
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        if (error) {
            *error = tr("You're offline, and this Microsoft sign-in has expired. "
                        "Go online once to refresh it, or play an offline profile.");
        }
        return {};
    }
    const QString refresh = m_tokens ? m_tokens->token(id, QStringLiteral("msRefresh")) : QString();
    if (refresh.isEmpty()) {
        if (error) {
            *error = tr("This Microsoft account needs to sign in again (saved credentials are missing).");
        }
        if (details) {
            *details = tr("No refresh token in %1.").arg(m_tokens ? m_tokens->backendName() : QStringLiteral("?"));
        }
        return {};
    }
    QString newRefresh;
    MicrosoftLoginResult r = MicrosoftAuth::loginWithRefreshTokenBlocking(
        QString::fromLatin1(Hearthlight::kAzureClientId), refresh, &newRefresh);
    if (!newRefresh.isEmpty() && newRefresh != refresh && m_tokens) {
        m_tokens->setToken(id, QStringLiteral("msRefresh"), newRefresh);
    }
    if (!r.ok) {
        // Ownership failures still carry a rotated refresh token; surface the
        // real reason instead of pretending anything.
        if (error) {
            *error = r.error.isEmpty() ? tr("Microsoft sign-in failed.") : r.error;
        }
        if (details) {
            *details = Logger::redacted(r.errorDetails);
        }
        emit loginFinished(id, false, r.error);
        return {};
    }
    setCachedMinecraftToken(id, r.minecraftToken, r.minecraftExpiresIn);
    // Persist the non-secret profile extras for offline display + skins UI.
    MicrosoftExtras ex = loadMicrosoftExtras(m_dataDir, id);
    ex.accountId = id;
    ex.username = r.profile.username;
    ex.uuid = r.profile.uuid;
    ex.skins = r.profile.skins;
    ex.capes = r.profile.capes;
    ex.hasOwnership = r.hasOwnership;
    ex.lastChecked = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    saveMicrosoftExtras(m_dataDir, ex);
    if (outAccount) {
        outAccount->username = r.profile.username;
        outAccount->displayName = r.profile.username;
        outAccount->uuid = QUuid::fromString(r.profile.uuid);
        // id stays stable (local stable id); uuid may rarely change casing only.
    }
    Logger::info(QStringLiteral("Microsoft account refreshed: %1").arg(r.profile.username));
    emit accountRefreshed(id);
    emit loginFinished(id, true, {});
    return r.minecraftToken;
}

QString MicrosoftAccountProvider::refreshBlocking(const QString &accountId, Account *outAccount, Task::Context &ctx)
{
    QString err, det;
    const QString tok = refreshBlocking(accountId, outAccount, &err, &det);
    if (tok.isEmpty() && !err.isEmpty()) {
        ctx.fail(det.isEmpty() ? err : (err + QStringLiteral("\n\n") + det.left(600)));
    } else if (!tok.isEmpty()) {
        ctx.report(1, 1, tr("Done"));
    }
    return tok;
}

#include "AccountProvider.moc"
