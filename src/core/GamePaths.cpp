#include "GamePaths.h"

#include <QDir>

GamePaths GamePaths::fromDataDir(const QString &dataDir)
{
    GamePaths p;
    p.dataDir = dataDir;
    p.metaDir = QDir(dataDir).filePath(QStringLiteral("meta"));
    p.versionsDir = QDir(dataDir).filePath(QStringLiteral("versions"));
    p.librariesDir = QDir(dataDir).filePath(QStringLiteral("libraries"));
    p.assetsDir = QDir(dataDir).filePath(QStringLiteral("assets"));
    p.assetsObjectsDir = QDir(p.assetsDir).filePath(QStringLiteral("objects"));
    p.assetsIndexesDir = QDir(p.assetsDir).filePath(QStringLiteral("indexes"));
    p.logConfigsDir = QDir(p.assetsDir).filePath(QStringLiteral("log_configs"));
    p.javaDir = QDir(dataDir).filePath(QStringLiteral("java"));
    p.cacheDir = QDir(dataDir).filePath(QStringLiteral("cache/downloads"));
    return p;
}

QString GamePaths::manifestFile() const
{
    return QDir(metaDir).filePath(QStringLiteral("version_manifest_v2.json"));
}

QString GamePaths::versionJsonFile(const QString &versionId) const
{
    return QDir(metaDir).filePath(QStringLiteral("versions/%1.json").arg(versionId));
}

QString GamePaths::versionDir(const QString &versionId) const
{
    return QDir(versionsDir).filePath(versionId);
}

QString GamePaths::clientJar(const QString &versionId) const
{
    return QDir(versionDir(versionId)).filePath(QStringLiteral("%1.jar").arg(versionId));
}

QString GamePaths::readyMarker(const QString &versionId) const
{
    return QDir(versionDir(versionId)).filePath(QStringLiteral(".ready"));
}

QString GamePaths::nativesDir(const QString &versionId) const
{
    return QDir(QDir(dataDir).filePath(QStringLiteral("natives"))).filePath(versionId);
}

QString GamePaths::nativesMarker(const QString &versionId) const
{
    return QDir(nativesDir(versionId)).filePath(QStringLiteral(".ok"));
}

QString GamePaths::gameDir(const QString &versionId) const
{
    return QDir(QDir(dataDir).filePath(QStringLiteral("gamedir"))).filePath(versionId);
}

QString GamePaths::assetIndexFile(const QString &indexId) const
{
    return QDir(assetsIndexesDir).filePath(QStringLiteral("%1.json").arg(indexId));
}

QString GamePaths::assetObjectFile(const QString &hash) const
{
    return QDir(assetsObjectsDir).filePath(assetRelPath(hash));
}

QString GamePaths::logConfigFile(const QString &fileId) const
{
    const QString safe = QString(fileId).replace(QLatin1Char('/'), QLatin1Char('_'));
    return QDir(logConfigsDir).filePath(safe);
}

QString GamePaths::managedJavaDir(int major) const
{
    return QDir(javaDir).filePath(QString::number(major));
}

QString GamePaths::mavenPath(const QString &coord, const QString &classifier)
{
    const QStringList parts = coord.split(QLatin1Char(':'));
    if (parts.size() < 3) {
        return {};
    }
    const QString group = parts.at(0);
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    QString cls = classifier;
    if (parts.size() > 3 && cls.isEmpty()) {
        cls = parts.at(3);
    }
    QString groupPath = group;
    groupPath.replace(QLatin1Char('.'), QLatin1Char('/'));
    QString file = QStringLiteral("%1-%2").arg(artifact).arg(version);
    if (!cls.isEmpty()) {
        file += QLatin1Char('-') + cls;
    }
    file += QStringLiteral(".jar");
    return QStringLiteral("%1/%2/%3/%4").arg(groupPath).arg(artifact).arg(version).arg(file);
}

QString GamePaths::assetRelPath(const QString &hash)
{
    if (hash.size() < 2) {
        return hash;
    }
    return QStringLiteral("%1/%2").arg(hash.left(2)).arg(hash);
}

void GamePaths::ensureBaseDirs() const
{
    for (const auto &d : { metaDir, versionsDir, librariesDir, assetsObjectsDir, assetsIndexesDir, logConfigsDir, javaDir,
                           cacheDir }) {
        QDir().mkpath(d);
    }
    QDir().mkpath(QDir(metaDir).filePath(QStringLiteral("versions")));
}

void GamePaths::ensureVersionDirs(const QString &versionId) const
{
    QDir().mkpath(versionDir(versionId));
    QDir().mkpath(nativesDir(versionId));
    QDir().mkpath(gameDir(versionId));
}
