#include "VersionModel.h"

#include "GamePaths.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSysInfo>

OsInfo currentOsInfo()
{
    OsInfo o;
#if defined(Q_OS_WIN)
    o.name = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    o.name = QStringLiteral("osx");
#else
    o.name = QStringLiteral("linux");
#endif
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    if (cpu.contains(QStringLiteral("arm64")) || cpu.contains(QStringLiteral("aarch64"))) {
        o.arch = QStringLiteral("arm64");
    } else if (cpu.contains(QStringLiteral("64"))) {
        o.arch = QStringLiteral("x64");
    } else {
        o.arch = QStringLiteral("x86");
    }
    o.osVersion = QSysInfo::kernelVersion();
    return o;
}

QString legacyArchToken(const QString &arch)
{
    if (arch == QStringLiteral("x64")) {
        return QStringLiteral("64");
    }
    if (arch == QStringLiteral("x86")) {
        return QStringLiteral("32");
    }
    return arch; // arm64 and anything future passes through unchanged
}

Rule Rule::fromJson(const QJsonObject &o)
{
    Rule r;
    r.action = o.value(QStringLiteral("action")).toString();
    r.os = o.value(QStringLiteral("os")).toObject();
    const QJsonObject feat = o.value(QStringLiteral("features")).toObject();
    for (auto it = feat.begin(); it != feat.end(); ++it) {
        if (it.value().toBool(false)) {
            r.features.append(it.key());
        }
    }
    return r;
}

bool Rule::applies(const OsInfo &os, const QSet<QString> &features) const
{
    const bool hasOs = !this->os.isEmpty();
    const bool hasFeatures = !this->features.isEmpty();
    if (!hasOs && !hasFeatures) {
        return true; // unconditional rule matches everything
    }
    if (hasOs) {
        const QString name = this->os.value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && name != os.name) {
            return false;
        }
        const QString arch = this->os.value(QStringLiteral("arch")).toString();
        if (!arch.isEmpty() && arch != os.arch) {
            return false;
        }
        const QString ver = this->os.value(QStringLiteral("version")).toString();
        if (!ver.isEmpty()) {
            const QRegularExpression re(ver);
            if (!re.isValid() || !re.match(os.osVersion).hasMatch()) {
                return false;
            }
        }
    }
    if (hasFeatures) {
        for (const auto &f : this->features) {
            if (!features.contains(f)) {
                return false;
            }
        }
    }
    return true;
}

bool rulesAllow(const QJsonArray &rules, const OsInfo &os, const QSet<QString> &features)
{
    if (rules.isEmpty()) {
        return true;
    }
    bool allowed = false;
    for (const auto &v : rules) {
        if (!v.isObject()) {
            continue;
        }
        const Rule r = Rule::fromJson(v.toObject());
        if (r.applies(os, features)) {
            allowed = (r.action == QStringLiteral("allow"));
        }
    }
    return allowed;
}

static LibraryDownload parseDownload(const QJsonObject &o)
{
    LibraryDownload d;
    d.path = o.value(QStringLiteral("path")).toString();
    d.url = o.value(QStringLiteral("url")).toString();
    d.sha1 = o.value(QStringLiteral("sha1")).toString();
    d.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
    return d;
}

Library Library::fromJson(const QJsonObject &o)
{
    Library lib;
    lib.name = o.value(QStringLiteral("name")).toString();
    lib.rules = o.value(QStringLiteral("rules")).toArray();
    const QJsonObject dl = o.value(QStringLiteral("downloads")).toObject();
    if (dl.contains(QStringLiteral("artifact"))) {
        lib.artifact = parseDownload(dl.value(QStringLiteral("artifact")).toObject());
        // Some entries omit path: derive it from the maven coordinate.
        if (lib.artifact.path.isEmpty() && !lib.name.isEmpty()) {
            lib.artifact.path = GamePaths::mavenPath(lib.name);
        }
        lib.hasArtifact = lib.artifact.valid() || !lib.artifact.path.isEmpty();
    }
    if (dl.contains(QStringLiteral("classifiers"))) {
        const QJsonObject cls = dl.value(QStringLiteral("classifiers")).toObject();
        for (auto it = cls.begin(); it != cls.end(); ++it) {
            LibraryDownload d = parseDownload(it.value().toObject());
            if (d.path.isEmpty() && !lib.name.isEmpty()) {
                d.path = GamePaths::mavenPath(lib.name, it.key());
            }
            lib.classifiers.insert(it.key(), d);
        }
    }
    const QJsonObject nat = o.value(QStringLiteral("natives")).toObject();
    for (auto it = nat.begin(); it != nat.end(); ++it) {
        // Legacy values can be string or {value: ...}; accept both.
        if (it.value().isString()) {
            lib.natives.insert(it.key(), it.value().toString());
        } else if (it.value().isObject()) {
            lib.natives.insert(it.key(), it.value().toObject().value(QStringLiteral("value")).toString());
        }
    }
    // Legacy osx key is sometimes absent while macos present (or vice versa);
    // Mojang standardized on osx in old files and macos nowhere — but be lenient.
    if (!lib.natives.contains(QStringLiteral("osx")) && lib.natives.contains(QStringLiteral("macos"))) {
        lib.natives.insert(QStringLiteral("osx"), lib.natives.value(QStringLiteral("macos")));
    }
    const QJsonObject ext = o.value(QStringLiteral("extract")).toObject();
    for (const auto &v : ext.value(QStringLiteral("exclude")).toArray()) {
        lib.extractExclude.append(v.toString());
    }
    return lib;
}

bool Library::activeFor(const OsInfo &os, const QSet<QString> &features) const
{
    // Convert stored raw rules back through evaluation.
    return rulesAllow(rules, os, features);
}

QString Library::nativeClassifierFor(const OsInfo &os) const
{
    // Mojang uses "osx" as the natives key on macOS.
    const QString key = os.name;
    if (!natives.contains(key)) {
        return {};
    }
    QString templ = natives.value(key);
    templ.replace(QStringLiteral("${arch}"), legacyArchToken(os.arch));
    return templ;
}

QString Library::nameClassifier() const
{
    const QStringList parts = name.split(QLatin1Char(':'));
    return parts.size() > 3 ? parts.at(3) : QString();
}

bool Library::isNativeJar() const
{
    if (!nameClassifier().isEmpty()) {
        return true;
    }
    // Fallback: artifact filenames containing "-natives-" (defensive; real
    // Mojang files always carry the 4th maven part).
    return artifact.path.contains(QStringLiteral("-natives-"));
}

QString Library::artifactAbsPath(const QString &librariesDir) const
{
    if (!hasArtifact || artifact.path.isEmpty()) {
        return {};
    }
    return QDir(librariesDir).filePath(artifact.path);
}

QString Library::classifierAbsPath(const QString &librariesDir, const QString &classifier) const
{
    const auto it = classifiers.find(classifier);
    QString rel;
    if (it != classifiers.end() && !it->path.isEmpty()) {
        rel = it->path;
    } else {
        rel = GamePaths::mavenPath(name, classifier);
    }
    if (rel.isEmpty()) {
        return {};
    }
    return QDir(librariesDir).filePath(rel);
}

QList<ArgItem> ArgItem::listFromJson(const QJsonValue &v)
{
    QList<ArgItem> out;
    if (v.isString()) {
        out.append({ { v.toString() }, {} });
        return out;
    }
    if (!v.isArray()) {
        return out;
    }
    for (const auto &e : v.toArray()) {
        if (e.isString()) {
            out.append({ { e.toString() }, {} });
        } else if (e.isObject()) {
            ArgItem item;
            const QJsonObject o = e.toObject();
            const QJsonValue val = o.value(QStringLiteral("value"));
            if (val.isString()) {
                item.values.append(val.toString());
            } else if (val.isArray()) {
                for (const auto &s : val.toArray()) {
                    item.values.append(s.toString());
                }
            }
            item.rules = o.value(QStringLiteral("rules")).toArray();
            out.append(item);
        }
    }
    return out;
}

ParsedVersion ParsedVersion::fromJson(const QJsonObject &o, QString *error)
{
    ParsedVersion v;
    v.id = o.value(QStringLiteral("id")).toString();
    if (v.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Version JSON has no id.");
        }
        return v;
    }
    v.inheritsFrom = o.value(QStringLiteral("inheritsFrom")).toString();
    v.mainClass = o.value(QStringLiteral("mainClass")).toString();
    v.type = o.value(QStringLiteral("type")).toString();
    v.assetsRef = o.value(QStringLiteral("assets")).toString();
    v.releaseTime = o.value(QStringLiteral("releaseTime")).toString();
    v.minecraftArguments = o.value(QStringLiteral("minecraftArguments")).toString();
    if (o.contains(QStringLiteral("arguments"))) {
        const QJsonObject args = o.value(QStringLiteral("arguments")).toObject();
        v.gameArgs = ArgItem::listFromJson(args.value(QStringLiteral("game")));
        v.jvmArgs = ArgItem::listFromJson(args.value(QStringLiteral("jvm")));
    }
    for (const auto &e : o.value(QStringLiteral("libraries")).toArray()) {
        if (e.isObject()) {
            v.libraries.append(Library::fromJson(e.toObject()));
        }
    }
    const QJsonObject ai = o.value(QStringLiteral("assetIndex")).toObject();
    v.assetIndex.id = ai.value(QStringLiteral("id")).toString();
    v.assetIndex.sha1 = ai.value(QStringLiteral("sha1")).toString();
    v.assetIndex.url = ai.value(QStringLiteral("url")).toString();
    v.assetIndex.size = static_cast<qint64>(ai.value(QStringLiteral("size")).toDouble(-1));
    v.assetIndex.totalSize = static_cast<qint64>(ai.value(QStringLiteral("totalSize")).toDouble(-1));
    const QJsonObject dl = o.value(QStringLiteral("downloads")).toObject();
    const QJsonObject cli = dl.value(QStringLiteral("client")).toObject();
    v.client.sha1 = cli.value(QStringLiteral("sha1")).toString();
    v.client.url = cli.value(QStringLiteral("url")).toString();
    v.client.size = static_cast<qint64>(cli.value(QStringLiteral("size")).toDouble(-1));
    const QJsonObject log = o.value(QStringLiteral("logging")).toObject();
    const QJsonObject logClient = log.value(QStringLiteral("client")).toObject();
    if (!logClient.isEmpty()) {
        v.hasLogging = true;
        const QJsonObject file = logClient.value(QStringLiteral("file")).toObject();
        v.logging.fileId = file.value(QStringLiteral("id")).toString();
        v.logging.sha1 = file.value(QStringLiteral("sha1")).toString();
        v.logging.url = file.value(QStringLiteral("url")).toString();
        v.logging.size = static_cast<qint64>(file.value(QStringLiteral("size")).toDouble(-1));
        v.logging.argument = logClient.value(QStringLiteral("argument")).toString();
    }
    const QJsonObject jv = o.value(QStringLiteral("javaVersion")).toObject();
    v.javaMajor = jv.value(QStringLiteral("majorVersion")).toInt(0);
    return v;
}

ParsedVersion ParsedVersion::merge(const ParsedVersion &base, const ParsedVersion &overlay)
{
    ParsedVersion v = base;
    if (!overlay.id.isEmpty()) {
        v.id = overlay.id;
    }
    if (!overlay.inheritsFrom.isEmpty()) {
        v.inheritsFrom = overlay.inheritsFrom;
    }
    if (!overlay.mainClass.isEmpty()) {
        v.mainClass = overlay.mainClass;
    }
    if (!overlay.type.isEmpty()) {
        v.type = overlay.type;
    }
    if (!overlay.assetsRef.isEmpty()) {
        v.assetsRef = overlay.assetsRef;
    }
    if (!overlay.releaseTime.isEmpty()) {
        v.releaseTime = overlay.releaseTime;
    }
    if (!overlay.minecraftArguments.isEmpty()) {
        v.minecraftArguments = overlay.minecraftArguments;
    }
    v.gameArgs.append(overlay.gameArgs);
    v.jvmArgs.append(overlay.jvmArgs);
    v.libraries.append(overlay.libraries);
    if (!overlay.assetIndex.id.isEmpty()) {
        v.assetIndex = overlay.assetIndex;
    }
    if (!overlay.client.url.isEmpty()) {
        v.client = overlay.client;
    }
    if (overlay.hasLogging) {
        v.logging = overlay.logging;
        v.hasLogging = true;
    }
    if (overlay.javaMajor != 0) {
        v.javaMajor = overlay.javaMajor;
    }
    return v;
}

QString substitutePlaceholders(const QString &templ, const QMap<QString, QString> &map, QStringList *missing)
{
    QString out;
    out.reserve(templ.size());
    qsizetype i = 0;
    while (i < templ.size()) {
        const qsizetype start = templ.indexOf(QStringLiteral("${"), i);
        if (start < 0) {
            out.append(templ.mid(i));
            break;
        }
        out.append(templ.mid(i, start - i));
        const qsizetype end = templ.indexOf(QLatin1Char('}'), start + 2);
        if (end < 0) {
            out.append(templ.mid(start)); // unterminated: keep literally
            break;
        }
        const QString key = templ.mid(start + 2, end - (start + 2));
        const auto it = map.find(key);
        if (it != map.end()) {
            out.append(it.value());
        } else {
            // Unknown/unavailable placeholder -> empty (documented). Callers
            // log `missing` at debug level; never fail the launch for these.
            if (missing && !missing->contains(key)) {
                missing->append(key);
            }
        }
        i = end + 1;
    }
    return out;
}

QStringList buildArgs(const QList<ArgItem> &items, const OsInfo &os, const QSet<QString> &features,
                      const QMap<QString, QString> &vars)
{
    QStringList out;
    for (const auto &item : items) {
        if (!item.activeFor(os, features)) {
            continue;
        }
        for (const auto &raw : item.values) {
            out.append(substitutePlaceholders(raw, vars));
        }
    }
    return out;
}

QStringList splitLegacyArguments(const QString &s)
{
    QStringList out;
    QString cur;
    bool inQuotes = false;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if (c == QLatin1Char(' ') && !inQuotes) {
            if (!cur.isEmpty()) {
                out.append(cur);
                cur.clear();
            }
            continue;
        }
        cur.append(c);
    }
    if (!cur.isEmpty()) {
        out.append(cur);
    }
    return out;
}

QSet<QString> activeFeatures(bool demo, bool customResolution, bool quickSingle, bool quickMulti,
                             bool quickRealms)
{
    QSet<QString> f;
    if (demo) {
        f.insert(QStringLiteral("is_demo_user"));
    }
    if (customResolution) {
        f.insert(QStringLiteral("has_custom_resolution"));
    }
    if (quickSingle || quickMulti || quickRealms) {
        f.insert(QStringLiteral("has_quick_plays_support"));
    }
    if (quickSingle) {
        f.insert(QStringLiteral("is_quick_play_singleplayer"));
    }
    if (quickMulti) {
        f.insert(QStringLiteral("is_quick_play_multiplayer"));
    }
    if (quickRealms) {
        f.insert(QStringLiteral("is_quick_play_realms"));
    }
    return f;
}
