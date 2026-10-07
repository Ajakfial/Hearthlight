#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// Kindling — guided setup (spec 10.3).
// Curated starter sets live in code (built-ins, resolved live by Modrinth
// slug so they track the newest files) plus optional user JSON files in
// <dataDir>/kindling/*.json. Schema is documented in docs/kindling.md.
struct StarterPack {
    QString id; // e.g. "performance"
    QString title; // e.g. "Performance boost"
    QString summary; // one calm line
    QString loader; // vanilla | fabric | forge | quilt | neoforge
    QStringList slugs; // Modrinth project slugs, empty = just Minecraft
};

namespace Kindling {
QList<StarterPack> builtinPacks();
// Load user packs from <dataDir>/kindling/*.json (invalid files skipped,
// never fatal). Returns only schema-valid packs.
QList<StarterPack> loadCustomPacks(const QString &dataDir, QStringList *skipped = nullptr);
bool parsePackObject(const QJsonObject &o, StarterPack *out, QString *error);
QString kindlingDir(const QString &dataDir);
} // namespace Kindling
