#pragma once

#include <QString>

// Shared on-disk layout (libraries/assets are shared across versions and
// instances to save disk space). All paths tolerate spaces/Unicode; no
// network needed.
//
//   <dataDir>/
//     meta/version_manifest_v2.json   cached Mojang manifest
//     meta/versions/<id>.json        cached per-version json
//     versions/<id>/<id>.jar         client jar (per version)
//     versions/<id>/.ready           install marker
//     libraries/...                  shared maven-layout jars
//     assets/indexes/<id>.json       shared asset indexes
//     assets/objects/<h2>/<hash>     shared asset objects
//     assets/virtual/<id>/...        materialized copy for "virtual" assets
//     assets/log_configs/<file>      logging xml
//     natives/<id>/                  extracted natives (per version)
//     gamedir/<id>/                  default game dir (saves/, resources/…)
//     java/<major>/...               Hearthlight-managed JVMs
//     cache/downloads/               DownloadManager scratch
struct GamePaths {
    QString dataDir;
    QString metaDir;
    QString versionsDir;
    QString librariesDir;
    QString assetsDir;
    QString assetsObjectsDir;
    QString assetsIndexesDir;
    QString logConfigsDir;
    QString javaDir;
    QString cacheDir;

    static GamePaths fromDataDir(const QString &dataDir);

    QString manifestFile() const;
    QString versionJsonFile(const QString &versionId) const;
    QString versionDir(const QString &versionId) const;
    QString clientJar(const QString &versionId) const;
    QString readyMarker(const QString &versionId) const;
    QString nativesDir(const QString &versionId) const;
    QString nativesMarker(const QString &versionId) const;
    QString gameDir(const QString &versionId) const;
    QString assetIndexFile(const QString &indexId) const;
    QString assetObjectFile(const QString &hash) const;
    QString logConfigFile(const QString &fileId) const;
    QString managedJavaDir(int major) const;

    // Maven coordinate -> relative path: group:artifact:version[:classifier]
    // e.g. "org.lwjgl:lwjgl:3.3.1" -> "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1.jar"
    static QString mavenPath(const QString &coord, const QString &classifier = {});
    // Asset hash -> "objects/ab/abcdef..."
    static QString assetRelPath(const QString &hash);

    void ensureBaseDirs() const;
    void ensureVersionDirs(const QString &versionId) const;
};
