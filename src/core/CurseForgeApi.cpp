#include "CurseForgeApi.h"

#include "DownloadManager.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace CurseForgeMeta {

CurseForgeFile parseFileResponse(const QByteArray &json, int projectId, qint64 fileId)
{
    CurseForgeFile out;
    out.projectId = projectId;
    out.fileId = fileId;
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        return out;
    }
    const QJsonObject d = doc.object().value(QStringLiteral("data")).toObject();
    if (d.isEmpty()) {
        return out;
    }
    out.fileName = d.value(QStringLiteral("fileName")).toString();
    out.downloadUrl = d.value(QStringLiteral("downloadUrl")).toString();
    if (d.contains(QStringLiteral("fileLength"))) {
        out.size = (qint64)d.value(QStringLiteral("fileLength")).toDouble(-1);
    }
    out.serverPack = d.value(QStringLiteral("isServerPack")).toBool(false);
    if (out.fileId == 0) {
        out.fileId = (qint64)d.value(QStringLiteral("id")).toDouble(fileId);
    }
    if (out.projectId == 0) {
        out.projectId = d.value(QStringLiteral("modId")).toInt(projectId);
    }
    return out;
}

static QJsonObject manifestRoot(const QByteArray &manifestJson)
{
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(manifestJson, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

QString manifestMinecraftVersion(const QByteArray &manifestJson)
{
    const QJsonObject mc = manifestRoot(manifestJson).value(QStringLiteral("minecraft")).toObject();
    const QString v = mc.value(QStringLiteral("version")).toString();
    return v.isEmpty() ? QStringLiteral("1.20.1") : v;
}

QString manifestLoaderType(const QByteArray &manifestJson)
{
    const QJsonObject mc = manifestRoot(manifestJson).value(QStringLiteral("minecraft")).toObject();
    const QJsonArray loaders = mc.value(QStringLiteral("modLoaders")).toArray();
    if (loaders.isEmpty()) {
        return QStringLiteral("forge");
    }
    const QString lid = loaders.first().toObject().value(QStringLiteral("id")).toString().toLower();
    if (lid.startsWith(QStringLiteral("fabric"))) {
        return QStringLiteral("fabric");
    }
    if (lid.startsWith(QStringLiteral("quilt"))) {
        return QStringLiteral("quilt");
    }
    if (lid.startsWith(QStringLiteral("neoforge"))) {
        return QStringLiteral("neoforge");
    }
    return QStringLiteral("forge");
}

QList<QPair<int, qint64>> manifestFiles(const QByteArray &manifestJson)
{
    QList<QPair<int, qint64>> out;
    const QJsonArray files = manifestRoot(manifestJson).value(QStringLiteral("files")).toArray();
    for (const auto &v : files) {
        const QJsonObject fo = v.toObject();
        const int pid = fo.value(QStringLiteral("projectID")).toInt(0);
        const qint64 fid = (qint64)fo.value(QStringLiteral("fileID")).toDouble(0);
        if (pid > 0 && fid > 0) {
            out.append({ pid, fid });
        }
    }
    return out;
}

QString manifestName(const QByteArray &manifestJson)
{
    const QString n = manifestRoot(manifestJson).value(QStringLiteral("name")).toString();
    return n.isEmpty() ? QStringLiteral("CurseForge pack") : n;
}

} // namespace CurseForgeMeta

CurseForgeApi::CurseForgeApi(const QString &dataDir, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_downloads(downloads)
{
    QDir().mkpath(cacheDir());
}

QString CurseForgeApi::cacheDir() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("meta/curseforge"));
}

QByteArray CurseForgeApi::fetchJsonBlocking(const QString &path, const QString &cacheName, const QString &apiKey,
                                            Task::Context &ctx)
{
    const QString dest = QDir(cacheDir()).filePath(cacheName);
    if (apiKey.trimmed().isEmpty()) {
        ctx.fail(tr("Add your CurseForge API key first: Settings → Mod sources. "
                    "Get one free at console.curseforge.com."));
        // Still serve cache so a previously resolved pack stays usable offline.
        QFile f(dest);
        if (f.open(QIODevice::ReadOnly)) {
            Logger::info(QStringLiteral("CurseForge: serving cached %1 without a key.").arg(cacheName));
            return f.readAll();
        }
        return {};
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        QFile f(dest);
        if (f.open(QIODevice::ReadOnly)) {
            return f.readAll();
        }
        ctx.fail(tr("You're offline and this CurseForge file info isn't cached. "
                    "Go online once to resolve it."));
        return {};
    }
    DownloadRequest req{ QUrl(CurseForgeMeta::apiBase() + path), dest };
    req.resume = false;
    req.extraRequest.setRawHeader("x-api-key", apiKey.trimmed().toUtf8());
    req.extraRequest.setRawHeader("Accept", "application/json");
    QString err;
    if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
        const QString low = err.toLower();
        if (low.contains(QStringLiteral("401")) || low.contains(QStringLiteral("403"))) {
            ctx.fail(tr("CurseForge rejected the API key (401/403). Check Settings → Mod sources — "
                        "the key may be wrong or revoked."));
        } else if (low.contains(QStringLiteral("429"))) {
            ctx.fail(tr("CurseForge is rate-limiting requests right now. Wait a minute and try again."));
        } else {
            ctx.fail(err);
        }
        QFile f(dest);
        if (f.open(QIODevice::ReadOnly)) {
            Logger::warning(QStringLiteral("CurseForge fetch failed; serving cached %1.").arg(cacheName));
            return f.readAll();
        }
        return {};
    }
    QFile f(dest);
    if (!f.open(QIODevice::ReadOnly)) {
        ctx.fail(tr("Couldn't read the CurseForge answer."));
        return {};
    }
    return f.readAll();
}

CurseForgeFile CurseForgeApi::fileBlocking(int projectId, qint64 fileId, const QString &apiKey,
                                            Task::Context &ctx)
{
    const QString path = QStringLiteral("/mods/%1/files/%2").arg(projectId).arg(fileId);
    const QString cacheName =
        QStringLiteral("file-%1-%2.json").arg(projectId).arg(fileId);
    const QByteArray raw = fetchJsonBlocking(path, cacheName, apiKey, ctx);
    if (raw.isEmpty()) {
        return {};
    }
    return CurseForgeMeta::parseFileResponse(raw, projectId, fileId);
}

#include "CurseForgeApi.moc"
