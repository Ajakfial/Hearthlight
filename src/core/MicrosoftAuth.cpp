#include "MicrosoftAuth.h"

#include "Constants.h"
#include "Logger.h"

#include <QDir>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {
QByteArray formBody(const QList<QPair<QString, QString>> &pairs)
{
    QUrlQuery q;
    for (const auto &p : pairs) {
        q.addQueryItem(p.first, p.second);
    }
    return q.query(QUrl::FullyEncoded).toUtf8();
}

QJsonObject postFormBlocking(const QUrl &url, const QByteArray &body, int *httpStatus, QString *rawOut,
                             int timeoutMs = 30000)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    QNetworkReply *rep = nam.post(req, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    QJsonObject out;
    if (httpStatus) {
        *httpStatus = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    const QByteArray raw = rep->readAll();
    if (rawOut) {
        *rawOut = QString::fromUtf8(raw.left(4000));
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error == QJsonParseError::NoError && doc.isObject()) {
        out = doc.object();
    }
    rep->deleteLater();
    return out;
}

QJsonObject postJsonBlocking(const QUrl &url, const QJsonObject &body, const QString &bearer, int *httpStatus,
                             QString *rawOut, int timeoutMs = 30000)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    if (!bearer.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());
    }
    QNetworkReply *rep = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    QJsonObject out;
    if (httpStatus) {
        *httpStatus = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    const QByteArray raw = rep->readAll();
    if (rawOut) {
        *rawOut = QString::fromUtf8(raw.left(6000));
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error == QJsonParseError::NoError && doc.isObject()) {
        out = doc.object();
    }
    // Some error payloads are arrays/strings; wrap for the mapper.
    if (out.isEmpty() && !raw.trimmed().isEmpty()) {
        out[QStringLiteral("_raw")] = QString::fromUtf8(raw.left(2000));
    }
    rep->deleteLater();
    return out;
}

QJsonObject getJsonBlocking(const QUrl &url, const QString &bearer, int *httpStatus, QString *rawOut,
                            int timeoutMs = 30000)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    if (!bearer.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());
    }
    QNetworkReply *rep = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    QJsonObject out;
    if (httpStatus) {
        *httpStatus = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    const QByteArray raw = rep->readAll();
    if (rawOut) {
        *rawOut = QString::fromUtf8(raw.left(6000));
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error == QJsonParseError::NoError && doc.isObject()) {
        out = doc.object();
    } else if (e.error == QJsonParseError::NoError && doc.isArray()) {
        out[QStringLiteral("_array")] = QString::fromUtf8(raw.left(4000));
        out[QStringLiteral("_isArray")] = true;
        // Keep the raw array for entitlements parsing via _raw.
        out[QStringLiteral("_rawArray")] = QString::fromUtf8(raw.left(6000));
    }
    rep->deleteLater();
    return out;
}
} // namespace

// --- Extras persistence (no tokens) ---

QJsonObject MicrosoftExtras::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("schemaVersion")] = 1;
    o[QStringLiteral("accountId")] = accountId;
    o[QStringLiteral("username")] = username;
    o[QStringLiteral("uuid")] = uuid;
    o[QStringLiteral("gamertag")] = gamertag;
    o[QStringLiteral("hasOwnership")] = hasOwnership;
    o[QStringLiteral("lastChecked")] = lastChecked;
    o[QStringLiteral("activeCapeId")] = activeCapeId;
    QJsonArray skins;
    for (const auto &s : this->skins) {
        QJsonObject so;
        so[QStringLiteral("id")] = s.id;
        so[QStringLiteral("url")] = s.url;
        so[QStringLiteral("variant")] = s.variant;
        so[QStringLiteral("state")] = s.state;
        skins.append(so);
    }
    o[QStringLiteral("skins")] = skins;
    QJsonArray capes;
    for (const auto &c : this->capes) {
        QJsonObject co;
        co[QStringLiteral("id")] = c.id;
        co[QStringLiteral("url")] = c.url;
        co[QStringLiteral("state")] = c.state;
        co[QStringLiteral("alias")] = c.alias;
        capes.append(co);
    }
    o[QStringLiteral("capes")] = capes;
    return o;
}

MicrosoftExtras MicrosoftExtras::fromJson(const QString &accountId, const QJsonObject &o)
{
    MicrosoftExtras e;
    e.accountId = accountId;
    e.username = o.value(QStringLiteral("username")).toString();
    e.uuid = o.value(QStringLiteral("uuid")).toString();
    e.gamertag = o.value(QStringLiteral("gamertag")).toString();
    e.hasOwnership = o.value(QStringLiteral("hasOwnership")).toBool(false);
    e.lastChecked = o.value(QStringLiteral("lastChecked")).toString();
    e.activeCapeId = o.value(QStringLiteral("activeCapeId")).toString();
    for (const auto &v : o.value(QStringLiteral("skins")).toArray()) {
        const QJsonObject so = v.toObject();
        MicrosoftSkin s;
        s.id = so.value(QStringLiteral("id")).toString();
        s.url = so.value(QStringLiteral("url")).toString();
        s.variant = so.value(QStringLiteral("variant")).toString();
        s.state = so.value(QStringLiteral("state")).toString();
        e.skins.append(s);
    }
    for (const auto &v : o.value(QStringLiteral("capes")).toArray()) {
        const QJsonObject co = v.toObject();
        MicrosoftCape c;
        c.id = co.value(QStringLiteral("id")).toString();
        c.url = co.value(QStringLiteral("url")).toString();
        c.state = co.value(QStringLiteral("state")).toString();
        c.alias = co.value(QStringLiteral("alias")).toString();
        e.capes.append(c);
    }
    return e;
}

QString microsoftExtrasDir(const QString &dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("accounts/microsoft"));
}

bool saveMicrosoftExtras(const QString &dataDir, const MicrosoftExtras &e)
{
    const QString dir = microsoftExtrasDir(dataDir);
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(e.accountId + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(e.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

MicrosoftExtras loadMicrosoftExtras(const QString &dataDir, const QString &accountId)
{
    QFile f(QDir(microsoftExtrasDir(dataDir)).filePath(accountId + QStringLiteral(".json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return MicrosoftExtras{};
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        return MicrosoftExtras{};
    }
    MicrosoftExtras ex = MicrosoftExtras::fromJson(accountId, doc.object());
    ex.accountId = accountId;
    return ex;
}

// --- Error mapping ---

QString MicrosoftErrors::xboxMessage(qint64 xerr)
{
    switch (xerr) {
    case 2148916233:
        return QObject::tr("No Xbox profile found for this Microsoft account. "
                           "Create one once at xbox.com (it is free), then sign in again.");
    case 2148916238:
        return QObject::tr("This is a child account that needs family approval. "
                           "Ask an organizer in your Microsoft family group to approve Xbox access, then try again.");
    case 2148916229:
        return QObject::tr("This account is banned from Xbox services and can't sign in here.");
    case 2148916235:
        return QObject::tr("Xbox Live isn't available in this account's region right now.");
    default:
        break;
    }
    return {};
}

QString MicrosoftErrors::friendlyFor(const QString &stage, int httpStatus, const QJsonObject &body, const QString &raw)
{
    const QString err = body.value(QStringLiteral("error")).toString();
    const QString desc = body.value(QStringLiteral("error_description")).toString();
    const qint64 xerr = body.value(QStringLiteral("XErr")).toVariant().toLongLong();
    if (xerr != 0) {
        const QString m = xboxMessage(xerr);
        if (!m.isEmpty()) {
            return m;
        }
        return QObject::tr("Xbox services refused sign-in (code %1). This is usually temporary — wait a minute and try again.").arg(xerr);
    }
    if (stage == QStringLiteral("token")) {
        if (err == QStringLiteral("authorization_pending")) {
            return QObject::tr("Waiting for you to approve the sign-in in your browser…");
        }
        if (err == QStringLiteral("authorization_declined") || err == QStringLiteral("bad_verification_code")) {
            return QObject::tr("That sign-in was declined or expired. Start again to get a fresh code.");
        }
        if (err == QStringLiteral("expired_token")) {
            return QObject::tr("That code expired. Start again to get a fresh one.");
        }
        if (err == QStringLiteral("invalid_grant")) {
            return QObject::tr("Your saved sign-in expired. Sign in again.");
        }
    }
    if (stage == QStringLiteral("ownership")) {
        if (httpStatus == 404) {
            return QObject::tr("No Minecraft purchase found on this account. "
                               "Buy Minecraft: Java Edition, or use an offline profile / demo for local play.");
        }
    }
    if (stage == QStringLiteral("profile") && httpStatus == 404) {
        return QObject::tr("No Minecraft profile exists on this account yet. "
                           "If you own the game, finish setting up your username at minecraft.net first.");
    }
    if (httpStatus == 401 || httpStatus == 403) {
        return QObject::tr("Your sign-in expired or was rejected. Sign in again.");
    }
    if (httpStatus == 429) {
        return QObject::tr("Microsoft is rate-limiting sign-ins right now. Wait a minute and try again.");
    }
    if (httpStatus >= 500 || httpStatus == 0) {
        return QObject::tr("Microsoft, Xbox, or Minecraft services seem to be having problems. "
                           "Try again in a little while; your saved accounts are untouched.");
    }
    if (!desc.isEmpty()) {
        return desc.left(300);
    }
    if (!raw.isEmpty()) {
        return QObject::tr("Sign-in failed during %1.").arg(stage);
    }
    return QObject::tr("Sign-in failed during %1. Check your connection and try again.").arg(stage);
}

// --- MicrosoftAuth ---

MicrosoftAuth::MicrosoftAuth(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

bool MicrosoftAuth::parseDeviceCode(const QJsonObject &o, DeviceCodeInfo *out)
{
    if (!o.contains(QStringLiteral("device_code")) || !o.contains(QStringLiteral("user_code"))) {
        return false;
    }
    DeviceCodeInfo d;
    d.deviceCode = o.value(QStringLiteral("device_code")).toString();
    d.userCode = o.value(QStringLiteral("user_code")).toString();
    d.verificationUri = o.value(QStringLiteral("verification_uri")).toString();
    d.verificationUriComplete = o.value(QStringLiteral("verification_uri_complete")).toString();
    d.expiresIn = o.value(QStringLiteral("expires_in")).toInt(900);
    d.interval = qMax(5, o.value(QStringLiteral("interval")).toInt(5));
    d.ok = !d.deviceCode.isEmpty() && !d.userCode.isEmpty();
    if (out) {
        *out = d;
    }
    return d.ok;
}

bool MicrosoftAuth::parseTokenResponse(const QJsonObject &o, QString *access, QString *refresh, qint64 *expiresIn,
                                       QString *error, QString *details)
{
    const QString err = o.value(QStringLiteral("error")).toString();
    if (!err.isEmpty()) {
        if (error) {
            *error = err;
        }
        if (details) {
            *details = o.value(QStringLiteral("error_description")).toString();
        }
        return false;
    }
    const QString at = o.value(QStringLiteral("access_token")).toString();
    if (at.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no_token");
        }
        if (details) {
            *details = QStringLiteral("Token endpoint returned no access_token.");
        }
        return false;
    }
    if (access) {
        *access = at;
    }
    if (refresh) {
        *refresh = o.value(QStringLiteral("refresh_token")).toString();
    }
    if (expiresIn) {
        *expiresIn = (qint64)o.value(QStringLiteral("expires_in")).toDouble(3600);
    }
    return true;
}

static bool parseXboxLike(const QJsonObject &o, QString *token, QString *uhs, QString *error, QString *details)
{
    const QString tok = o.value(QStringLiteral("Token")).toString();
    if (tok.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no_xbox_token");
        }
        if (details) {
            const qint64 xerr = o.value(QStringLiteral("XErr")).toVariant().toLongLong();
            *details = xerr ? QStringLiteral("XErr=%1").arg(xerr)
                            : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact).left(500));
        }
        return false;
    }
    if (token) {
        *token = tok;
    }
    if (uhs) {
        const QJsonObject claims = o.value(QStringLiteral("DisplayClaims")).toObject();
        const QJsonArray xui = claims.value(QStringLiteral("xui")).toArray();
        if (!xui.isEmpty()) {
            *uhs = xui.first().toObject().value(QStringLiteral("uhs")).toString();
        }
    }
    return true;
}

bool MicrosoftAuth::parseXblResponse(const QJsonObject &o, QString *token, QString *uhs, QString *error,
                                     QString *details)
{
    return parseXboxLike(o, token, uhs, error, details);
}

bool MicrosoftAuth::parseXstsResponse(const QJsonObject &o, QString *token, QString *uhs, QString *error,
                                      QString *details)
{
    return parseXboxLike(o, token, uhs, error, details);
}

bool MicrosoftAuth::parseMinecraftLogin(const QJsonObject &o, QString *token, qint64 *expiresIn, QString *error,
                                        QString *details)
{
    const QString tok = o.value(QStringLiteral("access_token")).toString();
    if (tok.isEmpty()) {
        if (error) {
            *error = o.value(QStringLiteral("error")).toString();
            if (error->isEmpty()) {
                *error = QStringLiteral("no_minecraft_token");
            }
        }
        if (details) {
            *details = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact).left(500));
        }
        return false;
    }
    if (token) {
        *token = tok;
    }
    if (expiresIn) {
        *expiresIn = (qint64)o.value(QStringLiteral("expires_in")).toDouble(86400);
    }
    return true;
}

MinecraftProfile MicrosoftAuth::parseMinecraftProfile(const QJsonObject &o)
{
    MinecraftProfile p;
    p.username = o.value(QStringLiteral("name")).toString();
    p.uuid = o.value(QStringLiteral("id")).toString();
    // Mojang compact id -> dashed for the Account model.
    if (!p.uuid.isEmpty() && !p.uuid.contains(QLatin1Char('-')) && p.uuid.size() == 32) {
        p.uuid = p.uuid.left(8) + QLatin1Char('-') + p.uuid.mid(8, 4) + QLatin1Char('-') + p.uuid.mid(12, 4)
            + QLatin1Char('-') + p.uuid.mid(16, 4) + QLatin1Char('-') + p.uuid.mid(20);
    }
    for (const auto &v : o.value(QStringLiteral("skins")).toArray()) {
        const QJsonObject so = v.toObject();
        MicrosoftSkin s;
        s.id = so.value(QStringLiteral("id")).toString();
        s.url = so.value(QStringLiteral("url")).toString();
        s.variant = so.value(QStringLiteral("variant")).toString();
        s.state = so.value(QStringLiteral("state")).toString();
        p.skins.append(s);
    }
    for (const auto &v : o.value(QStringLiteral("capes")).toArray()) {
        const QJsonObject co = v.toObject();
        MicrosoftCape c;
        c.id = co.value(QStringLiteral("id")).toString();
        c.url = co.value(QStringLiteral("url")).toString();
        c.state = co.value(QStringLiteral("state")).toString();
        c.alias = co.value(QStringLiteral("alias")).toString();
        p.capes.append(c);
    }
    p.ok = !p.username.isEmpty() && !p.uuid.isEmpty();
    return p;
}

static MicrosoftLoginResult chainFromMsAccess(const QString &msAccess)
{
    MicrosoftLoginResult r;
    // 1. Xbox Live
    QJsonObject xblBody;
    {
        QJsonObject props;
        props[QStringLiteral("AuthMethod")] = QStringLiteral("RPS");
        props[QStringLiteral("SiteName")] = QStringLiteral("user.auth.xboxlive.com");
        props[QStringLiteral("RpsTicket")] = QStringLiteral("d=") + msAccess;
        xblBody[QStringLiteral("Properties")] = props;
        xblBody[QStringLiteral("RelyingParty")] = QStringLiteral("http://auth.xboxlive.com");
        xblBody[QStringLiteral("TokenType")] = QStringLiteral("JWT");
    }
    int st = 0;
    QString raw;
    QJsonObject xbl = postJsonBlocking(QUrl(QStringLiteral("https://user.auth.xboxlive.com/user/authenticate")),
                                       xblBody, {}, &st, &raw);
    QString xblToken, uhs;
    QString e1, d1;
    if (!MicrosoftAuth::parseXblResponse(xbl, &xblToken, &uhs, &e1, &d1)) {
        r.error = MicrosoftErrors::friendlyFor(QStringLiteral("xbox"), st, xbl, raw);
        r.errorDetails = QStringLiteral("Xbox Live step (HTTP %1): %2").arg(st).arg(d1.isEmpty() ? raw : d1);
        return r;
    }
    // 2. XSTS
    QJsonObject xstsBody;
    {
        QJsonObject props;
        props[QStringLiteral("SandboxId")] = QStringLiteral("RETAIL");
        props[QStringLiteral("UserTokens")] = QJsonArray{ xblToken };
        xstsBody[QStringLiteral("Properties")] = props;
        xstsBody[QStringLiteral("RelyingParty")] = QStringLiteral("rp://api.minecraftservices.com/");
        xstsBody[QStringLiteral("TokenType")] = QStringLiteral("JWT");
    }
    QJsonObject xsts = postJsonBlocking(QUrl(QStringLiteral("https://xsts.auth.xboxlive.com/xsts/authorize")),
                                        xstsBody, {}, &st, &raw);
    QString xstsToken, xuhs;
    if (!MicrosoftAuth::parseXstsResponse(xsts, &xstsToken, &xuhs, &e1, &d1)) {
        r.error = MicrosoftErrors::friendlyFor(QStringLiteral("xsts"), st, xsts, raw);
        r.errorDetails = QStringLiteral("XSTS step (HTTP %1): %2").arg(st).arg(d1.isEmpty() ? raw : d1);
        return r;
    }
    if (xuhs.isEmpty()) {
        xuhs = uhs;
    }
    // 3. Minecraft login
    QJsonObject mcBody;
    mcBody[QStringLiteral("identityToken")] =
        QStringLiteral("XBL3.0 x=%1;%2").arg(xuhs, xstsToken);
    QJsonObject mc = postJsonBlocking(QUrl(QStringLiteral("https://api.minecraftservices.com/authentication/login_with_xbox")),
                                      mcBody, {}, &st, &raw);
    QString mcToken;
    qint64 mcExp = 0;
    if (!MicrosoftAuth::parseMinecraftLogin(mc, &mcToken, &mcExp, &e1, &d1)) {
        r.error = MicrosoftErrors::friendlyFor(QStringLiteral("minecraft-login"), st, mc, raw);
        r.errorDetails = QStringLiteral("Minecraft login (HTTP %1): %2").arg(st).arg(d1.isEmpty() ? raw : d1);
        return r;
    }
    r.minecraftToken = mcToken;
    r.minecraftExpiresIn = mcExp;
    // 4. Ownership
    QString oe, od;
    r.hasOwnership = MicrosoftAuth::checkOwnershipBlocking(mcToken, &oe, &od);
    if (!r.hasOwnership) {
        r.error = oe.isEmpty() ? MicrosoftErrors::friendlyFor(QStringLiteral("ownership"), 404, {}, {}) : oe;
        r.errorDetails = od;
        return r;
    }
    // 5. Profile
    QString pe, pd;
    r.profile = MicrosoftAuth::fetchProfileBlocking(mcToken, &pe, &pd);
    if (!r.profile.ok) {
        r.error = pe;
        r.errorDetails = pd;
        return r;
    }
    r.ok = true;
    return r;
}

MicrosoftLoginResult MicrosoftAuth::loginWithDeviceCodeBlocking(const QString &clientId, const DeviceCodeInfo &dc,
                                                                std::function<bool()> cancelled,
                                                                std::function<void(const QString &)> status,
                                                                int timeoutSecs)
{
    MicrosoftLoginResult r;
    const qint64 deadline = QDateTime::currentSecsSinceEpoch() + timeoutSecs;
    QString msAccess, msRefresh;
    // Poll the token endpoint until the user approves (or we time out).
    while (QDateTime::currentSecsSinceEpoch() < deadline) {
        if (cancelled && cancelled()) {
            r.error = QObject::tr("Sign-in was cancelled.");
            return r;
        }
        const QByteArray body = formBody({ { QStringLiteral("grant_type"),
                                             QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") },
                                           { QStringLiteral("client_id"), clientId },
                                           { QStringLiteral("device_code"), dc.deviceCode } });
        int st = 0;
        QString raw;
        const QJsonObject tok = postFormBlocking(
            QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token")), body, &st, &raw,
            30000);
        const QString err = tok.value(QStringLiteral("error")).toString();
        if (err == QStringLiteral("authorization_pending")) {
            if (status) {
                status(QObject::tr("Waiting for browser approval…"));
            }
            QEventLoop wait;
            QTimer::singleShot(dc.interval * 1000, &wait, &QEventLoop::quit);
            wait.exec();
            continue;
        }
        if (err == QStringLiteral("slow_down")) {
            QEventLoop wait;
            QTimer::singleShot((dc.interval + 5) * 1000, &wait, &QEventLoop::quit);
            wait.exec();
            continue;
        }
        if (!err.isEmpty()) {
            r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"), st, tok, raw);
            r.errorDetails = tok.value(QStringLiteral("error_description")).toString();
            if (r.errorDetails.isEmpty()) {
                r.errorDetails = raw;
            }
            return r;
        }
        qint64 exp = 0;
        QString terr, tdet;
        if (!parseTokenResponse(tok, &msAccess, &msRefresh, &exp, &terr, &tdet)) {
            r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"), st, tok, raw);
            r.errorDetails = tdet.isEmpty() ? raw : tdet;
            return r;
        }
        break;
    }
    if (msAccess.isEmpty()) {
        r.error = QObject::tr("That code expired before approval. Start again to get a fresh one.");
        return r;
    }
    r = chainFromMsAccess(msAccess);
    if (r.ok) {
        r.msRefreshToken = msRefresh;
    }
    return r;
}

MicrosoftLoginResult MicrosoftAuth::loginWithRefreshTokenBlocking(const QString &clientId,
                                                                  const QString &refreshToken,
                                                                  QString *newRefreshOut)
{
    MicrosoftLoginResult r;
    if (refreshToken.isEmpty()) {
        r.error = QObject::tr("Your saved sign-in expired. Sign in again.");
        return r;
    }
    const QByteArray body =
        formBody({ { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
                   { QStringLiteral("client_id"), clientId },
                   { QStringLiteral("refresh_token"), refreshToken },
                   { QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access") } });
    int st = 0;
    QString raw;
    const QJsonObject tok = postFormBlocking(
        QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token")), body, &st, &raw);
    QString msAccess, msRefresh;
    qint64 exp = 0;
    QString terr, tdet;
    if (!parseTokenResponse(tok, &msAccess, &msRefresh, &exp, &terr, &tdet)) {
        r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"), st, tok, raw);
        r.errorDetails = tok.value(QStringLiteral("error_description")).toString();
        if (r.errorDetails.isEmpty()) {
            r.errorDetails = raw;
        }
        return r;
    }
    r = chainFromMsAccess(msAccess);
    if (r.ok) {
        r.msRefreshToken = msRefresh.isEmpty() ? refreshToken : msRefresh;
        if (newRefreshOut) {
            *newRefreshOut = r.msRefreshToken;
        }
    } else if (!msRefresh.isEmpty() && newRefreshOut) {
        // Chain failed after a successful token refresh (e.g. no ownership):
        // still rotate the refresh token so the next attempt is silent.
        *newRefreshOut = msRefresh;
    }
    return r;
}

MicrosoftLoginResult MicrosoftAuth::redeemCodeBlocking(const QString &clientId, const QString &code,
                                                       const QString &redirectUri)
{
    MicrosoftLoginResult r;
    const QByteArray body = formBody({ { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
                                       { QStringLiteral("client_id"), clientId }, { QStringLiteral("code"), code },
                                       { QStringLiteral("redirect_uri"), redirectUri },
                                       { QStringLiteral("scope"),
                                         QStringLiteral("XboxLive.signin offline_access") } });
    int st = 0;
    QString raw;
    const QJsonObject tok = postFormBlocking(
        QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token")), body, &st, &raw);
    QString msAccess, msRefresh;
    qint64 exp = 0;
    QString terr, tdet;
    if (!parseTokenResponse(tok, &msAccess, &msRefresh, &exp, &terr, &tdet)) {
        r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"), st, tok, raw);
        r.errorDetails = tok.value(QStringLiteral("error_description")).toString();
        if (r.errorDetails.isEmpty()) {
            r.errorDetails = raw;
        }
        return r;
    }
    r = chainFromMsAccess(msAccess);
    if (r.ok) {
        r.msRefreshToken = msRefresh;
    }
    return r;
}

bool MicrosoftAuth::checkOwnershipBlocking(const QString &minecraftToken, QString *error, QString *details)
{
    if (minecraftToken.isEmpty()) {
        if (error) {
            *error = QObject::tr("Your sign-in expired. Sign in again.");
        }
        return false;
    }
    // Entitlements endpoint returns a bare JSON array; parse it manually.
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(QStringLiteral("https://api.minecraftservices.com/entitlements/mcstore")));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    req.setRawHeader("Authorization", ("Bearer " + minecraftToken).toUtf8());
    QNetworkReply *rep = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();
    const int st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = rep->readAll();
    rep->deleteLater();
    if (st == 401 || st == 403) {
        if (error) {
            *error = QObject::tr("Your sign-in expired. Sign in again.");
        }
        if (details) {
            *details = QStringLiteral("entitlements HTTP %1").arg(st);
        }
        return false;
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = MicrosoftErrors::friendlyFor(QStringLiteral("ownership"), st, {}, QString::fromUtf8(raw.left(500)));
        }
        if (details) {
            *details = QStringLiteral("entitlements HTTP %1: %2").arg(st).arg(QString::fromUtf8(raw.left(500)));
        }
        return false;
    }
    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    for (const auto &v : items) {
        const QString name = v.toObject().value(QStringLiteral("name")).toString().toLower();
        if (name.contains(QStringLiteral("minecraft")) && name.contains(QStringLiteral("product"))
            && name != QStringLiteral("product_minecraft_trial")) {
            return true;
        }
        if (name == QStringLiteral("game_minecraft")) {
            return true;
        }
    }
    if (error) {
        *error = QObject::tr("No Minecraft purchase found on this account. "
                             "Buy Minecraft: Java Edition, or use an offline profile / demo for local play.");
    }
    if (details) {
        *details = QStringLiteral("entitlements HTTP %1: %2 item(s), none granting Java Edition.")
                       .arg(st)
                       .arg(items.size());
    }
    return false;
}

MinecraftProfile MicrosoftAuth::fetchProfileBlocking(const QString &minecraftToken, QString *error, QString *details)
{
    MinecraftProfile empty;
    if (minecraftToken.isEmpty()) {
        if (error) {
            *error = QObject::tr("Your sign-in expired. Sign in again.");
        }
        return empty;
    }
    int st = 0;
    QString raw;
    const QJsonObject o = getJsonBlocking(QUrl(QStringLiteral("https://api.minecraftservices.com/minecraft/profile")),
                                          minecraftToken, &st, &raw);
    if (st == 404) {
        if (error) {
            *error = MicrosoftErrors::friendlyFor(QStringLiteral("profile"), st, o, raw);
        }
        if (details) {
            *details = raw;
        }
        return empty;
    }
    MinecraftProfile p = parseMinecraftProfile(o);
    if (!p.ok) {
        if (error) {
            *error = MicrosoftErrors::friendlyFor(QStringLiteral("profile"), st, o, raw);
        }
        if (details) {
            *details = raw;
        }
        return empty;
    }
    return p;
}

bool MicrosoftAuth::changeSkinByUrlBlocking(const QString &minecraftToken, const QString &url,
                                           const QString &variant, QString *error)
{
    if (minecraftToken.isEmpty() || url.isEmpty()) {
        if (error) {
            *error = QObject::tr("Pick a skin image URL first.");
        }
        return false;
    }
    QJsonObject body;
    body[QStringLiteral("variant")] = (variant == QStringLiteral("slim")) ? QStringLiteral("slim") : QStringLiteral("classic");
    body[QStringLiteral("url")] = url;
    int st = 0;
    QString raw;
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(QStringLiteral("https://api.minecraftservices.com/minecraft/profile/skins")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    req.setRawHeader("Authorization", ("Bearer " + minecraftToken).toUtf8());
    QNetworkReply *rep = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();
    st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    raw = QString::fromUtf8(rep->readAll().left(2000));
    rep->deleteLater();
    if (st < 200 || st >= 300) {
        if (error) {
            *error = QObject::tr("Official skin change failed (HTTP %1). Skins must be a public PNG URL.").arg(st);
        }
        Logger::warning(Logger::redacted(QStringLiteral("Skin change failed: %1 %2").arg(st).arg(raw)));
        return false;
    }
    return true;
}

bool MicrosoftAuth::setActiveCapeBlocking(const QString &minecraftToken, const QString &capeId, bool hide,
                                          QString *error)
{
    if (minecraftToken.isEmpty()) {
        if (error) {
            *error = QObject::tr("Your sign-in expired. Sign in again.");
        }
        return false;
    }
    int st = 0;
    QString raw;
    QUrl url(QStringLiteral("https://api.minecraftservices.com/minecraft/profile/capes/active"));
    if (hide) {
        // Hide: DELETE the active cape.
        QNetworkAccessManager nam;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
        req.setRawHeader("Authorization", ("Bearer " + minecraftToken).toUtf8());
        QNetworkReply *rep = nam.deleteResource(req);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(30000);
        loop.exec();
        st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        rep->deleteLater();
    } else {
        QJsonObject body;
        body[QStringLiteral("capeId")] = capeId;
        QJsonObject res = postJsonBlocking(url, body, minecraftToken, &st, &raw);
        Q_UNUSED(res);
    }
    if (st < 200 || st >= 300) {
        if (error) {
            *error = QObject::tr("Cape change failed (HTTP %1).").arg(st);
        }
        return false;
    }
    return true;
}

QString MicrosoftAuth::browserAuthorizeUrl(const QString &clientId, const QString &redirectUri, const QString &state)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), clientId);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    q.addQueryItem(QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access"));
    q.addQueryItem(QStringLiteral("state"), state);
    QUrl url(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"));
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

void MicrosoftAuth::startDeviceCode(const QString &clientId)
{
    m_clientId = clientId;
    m_cancelled = false;
    const QByteArray body = formBody({ { QStringLiteral("client_id"), clientId },
                                       { QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access") } });
    QNetworkRequest req(QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    QNetworkReply *rep = m_nam->post(req, body);
    connect(rep, &QNetworkReply::finished, this, [this, rep, clientId] {
        rep->deleteLater();
        if (m_cancelled) {
            return;
        }
        const QByteArray raw = rep->readAll();
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
        DeviceCodeInfo dc;
        if (e.error == QJsonParseError::NoError && doc.isObject() && parseDeviceCode(doc.object(), &dc)) {
            emit deviceCodeReady(dc);
            pollDeviceCode(clientId, dc);
        } else {
            MicrosoftLoginResult r;
            r.error = MicrosoftErrors::friendlyFor(QStringLiteral("devicecode"),
                                                   rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                                   {}, QString::fromUtf8(raw.left(500)));
            r.errorDetails = QString::fromUtf8(raw.left(1000));
            emit loginFinished(r);
        }
    });
}

void MicrosoftAuth::pollDeviceCode(const QString &clientId, const DeviceCodeInfo &dc)
{
    // GUI-thread polling with a timer: each tick tries the token endpoint once.
    auto *timer = new QTimer(this);
    timer->setInterval(dc.interval * 1000);
    const qint64 deadline = QDateTime::currentSecsSinceEpoch() + dc.expiresIn;
    connect(timer, &QTimer::timeout, this, [this, timer, clientId, dc, deadline] {
        if (m_cancelled) {
            timer->stop();
            timer->deleteLater();
            MicrosoftLoginResult r;
            r.error = tr("Sign-in was cancelled.");
            emit loginFinished(r);
            return;
        }
        if (QDateTime::currentSecsSinceEpoch() >= deadline) {
            timer->stop();
            timer->deleteLater();
            MicrosoftLoginResult r;
            r.error = tr("That code expired before approval. Start again to get a fresh one.");
            emit loginFinished(r);
            return;
        }
        const QByteArray body = formBody({ { QStringLiteral("grant_type"),
                                             QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") },
                                           { QStringLiteral("client_id"), clientId },
                                           { QStringLiteral("device_code"), dc.deviceCode } });
        QNetworkRequest req(QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
        QNetworkReply *rep = m_nam->post(req, body);
        connect(rep, &QNetworkReply::finished, this, [this, rep, timer] {
            rep->deleteLater();
            if (m_cancelled) {
                return;
            }
            const QByteArray raw = rep->readAll();
            QJsonParseError e{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
            const QJsonObject o = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject{};
            const QString err = o.value(QStringLiteral("error")).toString();
            if (err == QStringLiteral("authorization_pending")) {
                emit statusMessage(tr("Waiting for browser approval…"));
                return; // keep polling
            }
            if (err == QStringLiteral("slow_down")) {
                timer->setInterval(timer->interval() + 5000);
                return;
            }
            timer->stop();
            timer->deleteLater();
            if (!err.isEmpty()) {
                MicrosoftLoginResult r;
                r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"),
                                                       rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                                       o, QString::fromUtf8(raw.left(500)));
                r.errorDetails = o.value(QStringLiteral("error_description")).toString();
                emit loginFinished(r);
                return;
            }
            QString msAccess, msRefresh;
            qint64 exp = 0;
            QString terr, tdet;
            if (!parseTokenResponse(o, &msAccess, &msRefresh, &exp, &terr, &tdet)) {
                MicrosoftLoginResult r;
                r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"), 200, o,
                                                       QString::fromUtf8(raw.left(500)));
                r.errorDetails = tdet;
                emit loginFinished(r);
                return;
            }
            // Continue the remaining chain off the GUI thread so we never block it.
            emit statusMessage(tr("Checking Xbox and Minecraft…"));
            MicrosoftLoginResult r = chainFromMsAccess(msAccess);
            if (r.ok) {
                r.msRefreshToken = msRefresh;
            }
            emit loginFinished(r);
        });
    });
    timer->start();
    emit statusMessage(tr("Approve the sign-in in your browser…"));
}

void MicrosoftAuth::redeemAuthCode(const QString &clientId, const QString &code, const QString &redirectUri)
{
    m_cancelled = false;
    const QByteArray body = formBody({ { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
                                       { QStringLiteral("client_id"), clientId }, { QStringLiteral("code"), code },
                                       { QStringLiteral("redirect_uri"), redirectUri },
                                       { QStringLiteral("scope"),
                                         QStringLiteral("XboxLive.signin offline_access") } });
    QNetworkRequest req(QUrl(QStringLiteral("https://login.microsoftonline.com/consumers/oauth2/v2.0/token")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    QNetworkReply *rep = m_nam->post(req, body);
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const QByteArray raw = rep->readAll();
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &e);
        const QJsonObject o = (e.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject{};
        QString msAccess, msRefresh;
        qint64 exp = 0;
        QString terr, tdet;
        if (!parseTokenResponse(o, &msAccess, &msRefresh, &exp, &terr, &tdet)) {
            MicrosoftLoginResult r;
            r.error = MicrosoftErrors::friendlyFor(QStringLiteral("token"),
                                                   rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), o,
                                                   QString::fromUtf8(raw.left(500)));
            r.errorDetails = tdet;
            emit loginFinished(r);
            return;
        }
        emit statusMessage(tr("Checking Xbox and Minecraft…"));
        MicrosoftLoginResult r = chainFromMsAccess(msAccess);
        if (r.ok) {
            r.msRefreshToken = msRefresh;
        }
        emit loginFinished(r);
    });
}

void MicrosoftAuth::cancelPending()
{
    m_cancelled = true;
}

#include "MicrosoftAuth.moc"
