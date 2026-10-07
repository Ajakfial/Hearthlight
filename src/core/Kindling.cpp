#include "Kindling.h"

#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace Kindling {

QList<StarterPack> builtinPacks()
{
    // Slugs (not numeric ids) so sets track current files; each slug is
    // resolved live and anything unresolvable is reported, never faked.
    return {
        { QStringLiteral("plain"), QObject::tr("Just Minecraft"), QObject::tr("The real game, nothing added."),
          QStringLiteral("vanilla"), {} },
        { QStringLiteral("performance"), QObject::tr("Performance boost"),
          QObject::tr("Same game, smoother: speed-up mods that change nothing about how it plays."),
          QStringLiteral("fabric"),
          { QStringLiteral("sodium"), QStringLiteral("lithium"), QStringLiteral("ferrite-core"),
            QStringLiteral("krypton") } },
        { QStringLiteral("adventure"), QObject::tr("Adventure & exploration"),
          QObject::tr("Better maps, waypoints, and a fuller HUD for long journeys."), QStringLiteral("fabric"),
          { QStringLiteral("xaeros-minimap"), QStringLiteral("xaeros-world-map"), QStringLiteral("waystones"),
            QStringLiteral("appleskin"), QStringLiteral("jei") } },
        { QStringLiteral("building"), QObject::tr("Building & decoration"),
          QObject::tr("Thousands of new blocks and furniture for cozy builders."), QStringLiteral("fabric"),
          { QStringLiteral("chipped"), QStringLiteral("macaws-bridges"), QStringLiteral("jei") } },
    };
}

QString kindlingDir(const QString &dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("kindling"));
}

bool parsePackObject(const QJsonObject &o, StarterPack *out, QString *error)
{
    StarterPack p;
    p.id = o.value(QStringLiteral("id")).toString().trimmed();
    p.title = o.value(QStringLiteral("title")).toString().trimmed();
    p.summary = o.value(QStringLiteral("summary")).toString().trimmed();
    p.loader = o.value(QStringLiteral("loader")).toString().trimmed().toLower();
    if (p.id.isEmpty() || p.title.isEmpty()) {
        if (error) {
            *error = QObject::tr("Pack needs at least an \"id\" and a \"title\".");
        }
        return false;
    }
    static const QSet<QString> kLoaders = { QStringLiteral("vanilla"), QStringLiteral("fabric"),
                                            QStringLiteral("quilt"), QStringLiteral("forge"),
                                            QStringLiteral("neoforge") };
    if (!kLoaders.contains(p.loader)) {
        if (error) {
            *error = QObject::tr("Unknown loader “%1” (vanilla, fabric, quilt, forge, neoforge).").arg(p.loader);
        }
        return false;
    }
    for (const auto &v : o.value(QStringLiteral("slugs")).toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) {
            p.slugs.append(s);
        }
    }
    if (out) {
        *out = p;
    }
    return true;
}

QList<StarterPack> loadCustomPacks(const QString &dataDir, QStringList *skipped)
{
    QList<StarterPack> out;
    const QDir d(kindlingDir(dataDir));
    if (!d.exists()) {
        return out;
    }
    for (const auto &f : d.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile file(d.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) {
            if (skipped) {
                skipped->append(QStringLiteral("%1 (unreadable)").arg(f));
            }
            continue;
        }
        QJsonParseError e{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &e);
        auto fail = [&](const QString &why) {
            if (skipped) {
                skipped->append(QStringLiteral("%1 (%2)").arg(f, why));
            }
            Logger::warning(QStringLiteral("Kindling pack skipped: %1 (%2)").arg(f, why));
        };
        if (e.error != QJsonParseError::NoError) {
            fail(e.errorString());
            continue;
        }
        const auto addPack = [&](const QJsonObject &o) {
            StarterPack p;
            QString err;
            if (!parsePackObject(o, &p, &err)) {
                fail(err);
                return;
            }
            out.append(p);
        };
        if (doc.isObject() && doc.object().contains(QStringLiteral("packs"))) {
            for (const auto &v : doc.object().value(QStringLiteral("packs")).toArray()) {
                if (v.isObject()) {
                    addPack(v.toObject());
                }
            }
        } else if (doc.isObject()) {
            addPack(doc.object());
        } else if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                if (v.isObject()) {
                    addPack(v.toObject());
                }
            }
        } else {
            fail(QObject::tr("not a JSON object or array"));
        }
    }
    return out;
}

} // namespace Kindling
