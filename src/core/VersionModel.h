#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

// Parsing + evaluation of Mojang version JSON (spec section 4).
// Handles: inheritsFrom, libraries with OS/arch rules, legacy `natives`
// classifiers and modern classifier entries, downloads.client, assetIndex,
// logging config, mainClass, legacy `minecraftArguments` and modern
// `arguments` (game + jvm) with feature rules. Pure Qt, no widgets,
// fully unit-tested.

// Mojang OS/arch tokens: os name in {"windows","linux","osx"},
// arch in {"x86","x64","arm64"}.
struct OsInfo {
    QString name; // windows | linux | osx
    QString arch; // x86 | x64 | arm64
    QString osVersion; // kernel version, matched against rule regexes
};

OsInfo currentOsInfo();
// Legacy `${arch}` substitution inside native classifier templates:
// x64 -> "64", x86 -> "32", arm64 stays "arm64".
QString legacyArchToken(const QString &arch);

struct Rule {
    QString action; // "allow" | "disallow"
    QJsonObject os; // optional {name, arch, version(regex)}
    QStringList features; // optional; all must be active

    static Rule fromJson(const QJsonObject &o);
    bool applies(const OsInfo &os, const QSet<QString> &features) const;
};

// Mojang semantics: no rules -> allowed. Otherwise the LAST matching rule
// wins (Mojang pairs unconditional allow with conditional disallow, and
// single allow-os rules mean "only there").
bool rulesAllow(const QJsonArray &rules, const OsInfo &os, const QSet<QString> &features);

struct LibraryDownload {
    QString path; // maven-layout relative path
    QString url;
    QString sha1;
    qint64 size = 0;
    bool valid() const { return !url.isEmpty() && !path.isEmpty(); }
};

struct Library {
    QString name; // maven coordinate
    QJsonArray rules;
    LibraryDownload artifact;
    bool hasArtifact = false;
    QMap<QString, LibraryDownload> classifiers; // classifier -> download
    QMap<QString, QString> natives; // legacy: os name -> classifier template
    QStringList extractExclude;

    static Library fromJson(const QJsonObject &o);
    bool activeFor(const OsInfo &os, const QSet<QString> &features) const;
    // Resolved native classifier for this OS, or empty when not a native lib.
    QString nativeClassifierFor(const OsInfo &os) const;
    // Modern (>1.18-ish) native entries: the classifier is the 4th maven
    // part, e.g. "com.mojang:jtracy:1.14.38:natives-linux", rule-gated per OS.
    // These must be downloaded + extracted, never put on the classpath.
    bool isNativeJar() const;
    QString nameClassifier() const; // 4th maven part, or empty
    // Absolute jar path for the plain artifact (empty if none).
    QString artifactAbsPath(const QString &librariesDir) const;
    // Absolute path of a classifier jar (empty if unknown).
    QString classifierAbsPath(const QString &librariesDir, const QString &classifier) const;
};

// One arguments entry: literal value(s) gated by optional rules.
struct ArgItem {
    QStringList values;
    QJsonArray rules;
    static QList<ArgItem> listFromJson(const QJsonValue &v);
    bool activeFor(const OsInfo &os, const QSet<QString> &features) const
    {
        return rulesAllow(rules, os, features);
    }
};

struct AssetIndexRef {
    QString id;
    QString sha1;
    QString url;
    qint64 size = 0;
    qint64 totalSize = 0;
};

struct ClientDownload {
    QString sha1;
    QString url;
    qint64 size = 0;
};

struct LoggingRef {
    QString fileId;
    QString sha1;
    QString url;
    QString argument; // e.g. "-Dlog4j.configurationFile=${path}"
    qint64 size = 0;
};

struct ParsedVersion {
    QString id;
    QString inheritsFrom;
    QString mainClass;
    QString type; // release | snapshot | old_beta | old_alpha
    QString assetsRef; // e.g. "1.20", "legacy", "pre-1.6"
    QString releaseTime;
    QString minecraftArguments; // legacy format (empty when modern)
    QList<ArgItem> gameArgs; // modern format
    QList<ArgItem> jvmArgs; // modern format
    QList<Library> libraries;
    AssetIndexRef assetIndex;
    ClientDownload client;
    LoggingRef logging;
    bool hasLogging = false;
    int javaMajor = 0; // 0 = absent (assume 8)

    bool isModern() const { return !gameArgs.isEmpty() || !jvmArgs.isEmpty() || minecraftArguments.isEmpty(); }

    static ParsedVersion fromJson(const QJsonObject &o, QString *error = nullptr);
    // Deep merge for inheritsFrom chains: base provides defaults, overlay wins
    // on scalars and appends libraries/arguments.
    static ParsedVersion merge(const ParsedVersion &base, const ParsedVersion &overlay);
};

// Replace every ${key} with map[key] (missing keys become "" and are
// collected into *missing when non-null).
QString substitutePlaceholders(const QString &templ, const QMap<QString, QString> &map, QStringList *missing = nullptr);

// Flatten active ArgItems (modern format) into substituted argument strings.
QStringList buildArgs(const QList<ArgItem> &items, const OsInfo &os, const QSet<QString> &features,
                      const QMap<QString, QString> &vars);

// Split a legacy minecraftArguments string on spaces, respecting "quotes".
QStringList splitLegacyArguments(const QString &s);

// Feature flags understood by argument rules:
//   is_demo_user          --demo was requested (official demo flag)
//   has_custom_resolution -- width/height supplied
//   is_quick_play_singleplayer / multiplayer / realms (+ legacy has_quick_plays_support)
QSet<QString> activeFeatures(bool demo, bool customResolution, bool quickSingle = false, bool quickMulti = false,
                             bool quickRealms = false);
