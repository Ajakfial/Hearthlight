#include "Instance.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

QString instanceAccountModeToString(InstanceAccountMode m)
{
    switch (m) {
    case InstanceAccountMode::Specific:
        return QStringLiteral("specific");
    case InstanceAccountMode::Ask:
        return QStringLiteral("ask");
    case InstanceAccountMode::Current:
    default:
        return QStringLiteral("current");
    }
}

InstanceAccountMode instanceAccountModeFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QStringLiteral("specific")) {
        return InstanceAccountMode::Specific;
    }
    if (v == QStringLiteral("ask")) {
        return InstanceAccountMode::Ask;
    }
    return InstanceAccountMode::Current;
}

QString Instance::sanitizeId(const QString &name)
{
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9-_]+")), QStringLiteral("-"));
    id.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
    // Trim leading/trailing dashes without QLocale surprises.
    while (id.startsWith(QLatin1Char('-'))) {
        id.remove(0, 1);
    }
    while (id.endsWith(QLatin1Char('-'))) {
        id.chop(1);
    }
    if (id.isEmpty()) {
        id = QStringLiteral("instance");
    }
    return id.left(64);
}

QJsonObject Instance::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("schemaVersion")] = 2;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("versionId")] = versionId;
    o[QStringLiteral("loaderType")] = loaderType.isEmpty() ? QStringLiteral("vanilla") : loaderType;
    o[QStringLiteral("loaderVersion")] = loaderVersion;
    o[QStringLiteral("accountMode")] = instanceAccountModeToString(accountMode);
    o[QStringLiteral("accountId")] = accountId;
    o[QStringLiteral("javaPath")] = javaPath;
    o[QStringLiteral("memoryMb")] = memoryMb;
    o[QStringLiteral("extraJvmArgs")] = extraJvmArgs;
    o[QStringLiteral("width")] = width;
    o[QStringLiteral("height")] = height;
    o[QStringLiteral("fullscreen")] = fullscreen;
    QJsonArray pre;
    for (const auto &a : preLaunchCommand) {
        pre.append(a);
    }
    o[QStringLiteral("preLaunchCommand")] = pre;
    QJsonArray post;
    for (const auto &a : postExitCommand) {
        post.append(a);
    }
    o[QStringLiteral("postExitCommand")] = post;
    QJsonObject env;
    for (auto it = envVars.begin(); it != envVars.end(); ++it) {
        env[it.key()] = it.value();
    }
    o[QStringLiteral("envVars")] = env;
    o[QStringLiteral("gameDirOverride")] = gameDirOverride;
    o[QStringLiteral("group")] = group;
    o[QStringLiteral("quickPlayWorld")] = quickPlayWorld;
    o[QStringLiteral("quickPlayServer")] = quickPlayServer;
    o[QStringLiteral("quickPlayRealm")] = quickPlayRealm;
    o[QStringLiteral("playtimeSecs")] = (double)playtimeSecs;
    o[QStringLiteral("lastPlayed")] = lastPlayed;
    o[QStringLiteral("lastAccount")] = lastAccount;
    o[QStringLiteral("order")] = order;
    return o;
}

Instance Instance::fromJson(const QJsonObject &o, bool *ok)
{
    Instance in;
    bool good = true;
    in.id = o.value(QStringLiteral("id")).toString();
    in.name = o.value(QStringLiteral("name")).toString();
    in.versionId = o.value(QStringLiteral("versionId")).toString();
    good = good && !in.id.isEmpty() && !in.name.isEmpty() && !in.versionId.isEmpty();
    in.loaderType = o.value(QStringLiteral("loaderType")).toString(QStringLiteral("vanilla"));
    if (in.loaderType.isEmpty()) {
        in.loaderType = QStringLiteral("vanilla");
    }
    in.loaderVersion = o.value(QStringLiteral("loaderVersion")).toString();
    in.accountMode = instanceAccountModeFromString(o.value(QStringLiteral("accountMode")).toString());
    in.accountId = o.value(QStringLiteral("accountId")).toString();
    in.javaPath = o.value(QStringLiteral("javaPath")).toString();
    in.memoryMb = o.value(QStringLiteral("memoryMb")).toInt(0);
    in.extraJvmArgs = o.value(QStringLiteral("extraJvmArgs")).toString();
    in.width = o.value(QStringLiteral("width")).toInt(0);
    in.height = o.value(QStringLiteral("height")).toInt(0);
    in.fullscreen = o.value(QStringLiteral("fullscreen")).toBool(false);
    for (const auto &v : o.value(QStringLiteral("preLaunchCommand")).toArray()) {
        in.preLaunchCommand.append(v.toString());
    }
    for (const auto &v : o.value(QStringLiteral("postExitCommand")).toArray()) {
        in.postExitCommand.append(v.toString());
    }
    const QJsonObject env = o.value(QStringLiteral("envVars")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        in.envVars.insert(it.key(), it.value().toString());
    }
    in.gameDirOverride = o.value(QStringLiteral("gameDirOverride")).toString();
    in.group = o.value(QStringLiteral("group")).toString();
    in.quickPlayWorld = o.value(QStringLiteral("quickPlayWorld")).toString();
    in.quickPlayServer = o.value(QStringLiteral("quickPlayServer")).toString();
    in.quickPlayRealm = o.value(QStringLiteral("quickPlayRealm")).toString();
    in.playtimeSecs = (qint64)o.value(QStringLiteral("playtimeSecs")).toDouble(0);
    in.lastPlayed = o.value(QStringLiteral("lastPlayed")).toString();
    in.lastAccount = o.value(QStringLiteral("lastAccount")).toString();
    in.order = o.value(QStringLiteral("order")).toInt(0);
    if (ok) {
        *ok = good;
    }
    return in;
}

QString instanceRootDir(const QString &dataDir, const QString &instanceId)
{
    return QDir(dataDir).filePath(QStringLiteral("instances/%1").arg(instanceId));
}

QString instanceGameDir(const QString &dataDir, const Instance &inst)
{
    if (!inst.gameDirOverride.trimmed().isEmpty()) {
        return inst.gameDirOverride;
    }
    return QDir(instanceRootDir(dataDir, inst.id)).filePath(QStringLiteral("game"));
}

int instanceModCount(const QString &dataDir, const QString &instanceId)
{
    QDir mods(QDir(instanceRootDir(dataDir, instanceId)).filePath(QStringLiteral("game/mods")));
    if (!mods.exists()) {
        return 0;
    }
    return (int)mods.entryList({ QStringLiteral("*.jar") }, QDir::Files).size();
}
