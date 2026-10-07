#include "Launcher.h"

#include "Constants.h"
#include "JavaManager.h"
#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcessEnvironment>

Launcher::Launcher(const GamePaths &paths, JavaManager *java, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_java(java)
{
}

QString Launcher::resolveAccessToken(const Account &account, QString *error)
{
    if (account.type == AccountType::Offline) {
        return account.offlineAccessTokenPlaceholder(); // "0": local only
    }
    if (error) {
        *error = QObject::tr("Sign in again with Microsoft first (Accounts page), "
                             "then launch. Offline profiles keep working without sign-in.");
    }
    return {};
}

QString Launcher::resolveToken(const Account &account, const LaunchOptions &opts, QString *error,
                               QString *details) const
{
    if (account.type == AccountType::Offline) {
        return account.offlineAccessTokenPlaceholder();
    }
    // A pre-refreshed token passed by the prepare pipeline wins (no network here).
    if (!opts.minecraftToken.isEmpty()) {
        return opts.minecraftToken;
    }
    if (m_msResolver) {
        return m_msResolver(account, error, details);
    }
    return resolveAccessToken(account, error);
}

bool Launcher::build(const ParsedVersion &merged, const LaunchOptions &opts, BuiltLaunch *out, QString *error) const
{
    if (!opts.account.isValid()) {
        if (error) {
            *error = QObject::tr("Pick an account first (Accounts page).");
        }
        return false;
    }
    if (merged.mainClass.isEmpty()) {
        if (error) {
            *error = QObject::tr("Version info has no main class. Reinstall the version.");
        }
        return false;
    }
    QString tokenErr, tokenDet;
    const QString token = resolveToken(opts.account, opts, &tokenErr, &tokenDet);
    if (token.isEmpty() && opts.account.type != AccountType::Offline) {
        if (error) {
            *error = tokenDet.isEmpty() ? tokenErr : (tokenErr + QStringLiteral("\n\n") + tokenDet.left(800));
        }
        return false;
    }

    const OsInfo os = currentOsInfo();
    const bool customRes = opts.width > 0 && opts.height > 0;
    const bool qpSingle = !opts.quickPlayWorld.isEmpty();
    const bool qpMulti = !opts.quickPlayServer.isEmpty();
    const bool qpRealms = !opts.quickPlayRealm.isEmpty();
    const QSet<QString> features = activeFeatures(opts.demo, customRes, qpSingle, qpMulti, qpRealms);

    // --- Libraries: filter, verify present, build classpath ---
    QStringList cpEntries;
    const QString clientJar = m_paths.clientJar(merged.id);
    if (!QFile::exists(clientJar) || QFileInfo(clientJar).size() <= 0) {
        if (error) {
            *error = QObject::tr("The game jar is missing. Go online once to install %1.").arg(merged.id);
        }
        return false;
    }
    for (const auto &lib : merged.libraries) {
        if (!lib.activeFor(os, features)) {
            continue;
        }
        if (!lib.nativeClassifierFor(os).isEmpty() || lib.isNativeJar()) {
            continue; // natives live in nativesDir, not on the classpath
        }
        const QString p = lib.artifactAbsPath(m_paths.librariesDir);
        if (p.isEmpty()) {
            continue; // metadata-only entry (shouldn't happen for active libs)
        }
        if (!QFile::exists(p) || QFileInfo(p).size() <= 0) {
            if (error) {
                *error = QObject::tr("Library is missing: %1. Go online once to install.").arg(lib.name);
            }
            return false;
        }
        cpEntries.append(p);
    }
    cpEntries.append(clientJar);
#if defined(Q_OS_WIN)
    const QString sep = QStringLiteral(";");
#else
    const QString sep = QStringLiteral(":");
#endif
    const QString classpath = cpEntries.join(sep);

    const QString nativesDir = m_paths.nativesDir(merged.id);
    const QString gameDir = opts.gameDirOverride.isEmpty() ? m_paths.gameDir(merged.id) : opts.gameDirOverride;
    QDir().mkpath(gameDir);
    QDir().mkpath(nativesDir);

    // --- Java ---
    const int wantMajor = JavaManager::requiredMajorFor(merged.javaMajor);
    JavaInfo java = m_java ? m_java->findForMajor(wantMajor, opts.javaPath) : JavaInfo{};
    if (!java.valid) {
        if (error) {
            *error = QObject::tr("No Java %1 found. Open the version, let Hearthlight fetch one "
                                 "(online), or set Settings → Java.")
                         .arg(wantMajor);
        }
        return false;
    }

    // --- Placeholders (spec 13.8; unknown keys become "" by design) ---
    QMap<QString, QString> vars;
    vars[QStringLiteral("auth_player_name")] = opts.account.username;
    vars[QStringLiteral("auth_uuid")] = opts.account.uuidCompact();
    vars[QStringLiteral("auth_access_token")] = token;
    vars[QStringLiteral("auth_session")] = token; // legacy alias some versions use
    vars[QStringLiteral("user_type")] = opts.account.userTypeString();
    vars[QStringLiteral("user_properties")] = QStringLiteral("{}");
    vars[QStringLiteral("version_name")] = merged.id;
    vars[QStringLiteral("version_type")] = merged.type.isEmpty() ? QStringLiteral("release") : merged.type;
    vars[QStringLiteral("game_directory")] = gameDir;
    vars[QStringLiteral("assets_root")] = m_paths.assetsDir;
    vars[QStringLiteral("assets_index_name")] =
        !merged.assetIndex.id.isEmpty() ? merged.assetIndex.id : merged.assetsRef;
    vars[QStringLiteral("natives_directory")] = nativesDir;
    vars[QStringLiteral("launcher_name")] = QString::fromLatin1(Hearthlight::kLauncherName);
    vars[QStringLiteral("launcher_version")] = QString::fromLatin1(Hearthlight::kAppVersion);
    vars[QStringLiteral("classpath")] = classpath;
    vars[QStringLiteral("classpath_separator")] = sep;
    vars[QStringLiteral("primary_jar")] = clientJar;
    vars[QStringLiteral("clientid")] = opts.clientId;
    vars[QStringLiteral("auth_xuid")] = QString(); // online-only; empty for offline
    vars[QStringLiteral("auth_uid")] = QString();
    // Very old versions address ${game_assets}: the materialized virtual
    // tree for legacy/pre-* assets, otherwise the shared assets root.
    {
        const QString idx = !merged.assetIndex.id.isEmpty() ? merged.assetIndex.id : merged.assetsRef;
        const bool oldAssets = merged.assetsRef == QStringLiteral("legacy") || merged.assetsRef.startsWith(QStringLiteral("pre-"));
        vars[QStringLiteral("game_assets")] = oldAssets
            ? QDir(m_paths.assetsDir).filePath(QStringLiteral("virtual/%1").arg(idx))
            : m_paths.assetsDir;
    }
    if (customRes) {
        vars[QStringLiteral("resolution_width")] = QString::number(opts.width);
        vars[QStringLiteral("resolution_height")] = QString::number(opts.height);
    }

    // --- JVM args ---
    QStringList jvm;
    if (!merged.jvmArgs.isEmpty() || merged.minecraftArguments.isEmpty()) {
        jvm = buildArgs(merged.jvmArgs, os, features, vars);
    }
    // Mojang's logging argument uses ${path}, not the full variable set.
    if (merged.hasLogging && !merged.logging.argument.isEmpty()) {
        const QString logPath = m_paths.logConfigFile(merged.logging.fileId);
        if (!QFile::exists(logPath)) {
            if (error) {
                *error = QObject::tr("Logging config is missing. Reinstall the version (online).");
            }
            return false;
        }
        QMap<QString, QString> logVars{ { QStringLiteral("path"), logPath } };
        jvm.append(substitutePlaceholders(merged.logging.argument, logVars));
    }
    const int memMb = opts.memoryMb > 0 ? opts.memoryMb : JavaManager::suggestMemoryMb(JavaManager::systemRamMb(), 0);
    jvm.append(QStringLiteral("-Xmx%1M").arg(memMb));
    if (!opts.extraJvmArgs.trimmed().isEmpty()) {
        jvm.append(splitLegacyArguments(opts.extraJvmArgs));
    }

    // --- Game args ---
    QStringList game;
    if (!merged.gameArgs.isEmpty()) {
        game = buildArgs(merged.gameArgs, os, features, vars);
    } else {
        // Legacy format (1.12 and older, betas/alphas).
        game = splitLegacyArguments(merged.minecraftArguments);
        for (auto &a : game) {
            a = substitutePlaceholders(a, vars);
        }
    }
    if (opts.demo && !game.contains(QStringLiteral("--demo"))) {
        game.append(QStringLiteral("--demo")); // official demo flag
    }
    if (customRes) {
        if (!game.contains(QStringLiteral("--width"))) {
            game.append({ QStringLiteral("--width"), QString::number(opts.width) });
        }
        if (!game.contains(QStringLiteral("--height"))) {
            game.append({ QStringLiteral("--height"), QString::number(opts.height) });
        }
    }
    if (opts.fullscreen && !game.contains(QStringLiteral("--fullscreen"))) {
        game.append(QStringLiteral("--fullscreen"));
    }
    // Quick Play (1.20.2+): jump straight into a world/server/realm.
    if (qpSingle) {
        game.append({ QStringLiteral("--quickPlaySingleplayer"), opts.quickPlayWorld });
    }
    if (qpMulti) {
        game.append({ QStringLiteral("--quickPlayMultiplayer"), opts.quickPlayServer });
    }
    if (qpRealms) {
        game.append({ QStringLiteral("--quickPlayRealms"), opts.quickPlayRealm });
    }
    // Drop empties left by unavailable placeholders (e.g. --xuid "").
    // Keep it conservative: only drop empty strings, never touch real args.
    game.removeAll(QString());

    BuiltLaunch built;
    built.javaExe = java.path;
    built.jvmArgs = jvm;
    built.mainClass = merged.mainClass;
    built.gameArgs = game;
    built.workDir = gameDir;
    built.classpath = classpath;
    built.versionName = merged.id;
    if (out) {
        *out = built;
    }
    return true;
}

QProcess *Launcher::spawn(const BuiltLaunch &built, const LaunchOptions &opts, QString *error)
{
    if (!QFile::exists(built.javaExe)) {
        if (error) {
            *error = QObject::tr("Java went missing: %1").arg(built.javaExe);
        }
        return nullptr;
    }
    // Optional pre-launch command (blocking, short timeout; failure aborts).
    if (!opts.preLaunchCommand.isEmpty()) {
        QProcess pre;
        pre.setProgram(opts.preLaunchCommand.first());
        if (opts.preLaunchCommand.size() > 1) {
            pre.setArguments(opts.preLaunchCommand.mid(1));
        }
        pre.setWorkingDirectory(built.workDir);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto it = opts.extraEnv.begin(); it != opts.extraEnv.end(); ++it) {
            env.insert(it.key(), it.value());
        }
        pre.setProcessEnvironment(env);
        pre.start();
        if (!pre.waitForFinished(60000) || pre.exitCode() != 0) {
            if (error) {
                *error = QObject::tr("Pre-launch command failed: %1").arg(opts.preLaunchCommand.join(QLatin1Char(' ')));
            }
            return nullptr;
        }
    }
    auto *proc = new QProcess(this);
    proc->setProgram(built.javaExe);
    proc->setArguments(built.jvmArgs + QStringList{ built.mainClass } + built.gameArgs);
    proc->setWorkingDirectory(built.workDir);
    if (!opts.extraEnv.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto it = opts.extraEnv.begin(); it != opts.extraEnv.end(); ++it) {
            env.insert(it.key(), it.value());
        }
        proc->setProcessEnvironment(env);
    }
    proc->setProgram(built.javaExe);
    proc->setArguments(built.jvmArgs + QStringList{ built.mainClass } + built.gameArgs);
    proc->setWorkingDirectory(built.workDir);
    Logger::info(Logger::redacted(
        QStringLiteral("Launch %1: %2").arg(built.versionName).arg(redactedCommand(built))));
    proc->start();
    if (!proc->waitForStarted(8000)) {
        if (error) {
            *error = QObject::tr("Couldn't start Java (%1).").arg(proc->errorString());
        }
        proc->deleteLater();
        return nullptr;
    }
    return proc;
}

QString Launcher::redactedCommand(const BuiltLaunch &built)
{
    const QStringList all = QStringList{ built.javaExe } + built.jvmArgs + QStringList{ built.mainClass }
        + built.gameArgs;
    return Logger::redacted(all.join(QLatin1Char(' ')));
}

void Launcher::recordPlaytime(const QString &versionId, qint64 seconds) const
{
    if (seconds <= 0 || versionId.isEmpty()) {
        return;
    }
    const QString path = QDir(m_paths.metaDir).filePath(QStringLiteral("playtime.json"));
    QJsonObject root;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) {
            root = doc.object();
        }
        f.close();
    }
    root[versionId] = root.value(versionId).toInteger(0) + seconds;
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

qint64 Launcher::playtimeSeconds(const GamePaths &paths, const QString &versionId)
{
    QFile f(QDir(paths.metaDir).filePath(QStringLiteral("playtime.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object().value(versionId).toInteger(0) : 0;
}
