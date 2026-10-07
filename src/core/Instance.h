#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

// One isolated profile (spec section 7):
//   instances/<id>/instance.json + mods/ config/ saves/ resourcepacks/
//   shaderpacks/ screenshots/ options.txt
//
// Per-instance settings: Java, memory, JVM args, window size, fullscreen,
// pre-launch/post-exit commands, environment variables, game-dir override,
// preferred account, Quick Play target. Loader resolution lives in
// ModLoader; this struct only records the choice.
enum class InstanceAccountMode { Current = 0, Specific = 1, Ask = 2 };

QString instanceAccountModeToString(InstanceAccountMode m);
InstanceAccountMode instanceAccountModeFromString(const QString &s);

struct Instance {
    QString id; // filesystem-safe, unique
    QString name;
    QString versionId; // vanilla Minecraft version
    QString loaderType = QStringLiteral("vanilla"); // vanilla|fabric|quilt|forge|neoforge
    QString loaderVersion; // resolved loader version (empty = none/latest-stable at install)
    InstanceAccountMode accountMode = InstanceAccountMode::Current;
    QString accountId; // only when accountMode == Specific
    QString javaPath; // empty = global default
    int memoryMb = 0; // 0 = automatic (Recommended)
    QString extraJvmArgs;
    int width = 0;
    int height = 0;
    bool fullscreen = false;
    QStringList preLaunchCommand; // argv, empty = none
    QStringList postExitCommand; // argv, empty = none
    QMap<QString, QString> envVars;
    QString gameDirOverride; // empty = instances/<id>/game
    QString group; // collection name, empty = ungrouped
    QString quickPlayWorld;
    QString quickPlayServer;
    QString quickPlayRealm;
    qint64 playtimeSecs = 0;
    QString lastPlayed; // ISO 8601, empty = never
    QString lastAccount; // username used last time
    int order = 0; // drag-reorder position

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && !versionId.isEmpty(); }

    QJsonObject toJson() const;
    static Instance fromJson(const QJsonObject &o, bool *ok = nullptr);

    static QString sanitizeId(const QString &name);
};

QString instanceGameDir(const QString &dataDir, const Instance &inst);
QString instanceRootDir(const QString &dataDir, const QString &instanceId);
int instanceModCount(const QString &dataDir, const QString &instanceId);
