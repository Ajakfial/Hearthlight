#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Hearthstones — manual and automatic backups of worlds and configs
// (spec 10.5). Plain copies (not zips: robust, browsable, restorable) under
// instances/<id>/hearthstones/<name>/ with a manifest.json.
// Auto snapshots run before launch when worlds changed; manual ones from the
// Worlds dialog. Only auto snapshots are pruned (last 3).
struct HearthstoneInfo {
    QString name;
    QString created; // ISO 8601
    QString gameVersion;
    QString loader;
    QStringList worlds;
    bool includesConfig = false;
    bool automatic = false;
    qint64 sizeBytes = 0;
};

struct WorldInfo {
    QString folder; // saves/<folder>
    qint64 lastModifiedMs = 0;
    qint64 sizeBytes = 0;
};

class Hearthstones : public QObject {
    Q_OBJECT
public:
    explicit Hearthstones(const QString &dataDir, QObject *parent = nullptr);

    static QString hearthstonesDir(const QString &dataDir, const QString &instanceId);
    static int maxAutoSnapshots() { return 3; }

    QList<WorldInfo> listWorlds(const QString &instanceId) const;
    QList<HearthstoneInfo> list(const QString &instanceId) const;

    // Manual backup. worlds empty = all worlds.
    QString create(const QString &instanceId, const QString &name, const QStringList &worlds, bool includeConfig,
                   bool automatic, QString *error = nullptr);
    bool restore(const QString &instanceId, const QString &name, QString *error = nullptr);
    bool remove(const QString &instanceId, const QString &name, QString *error = nullptr);

    // Auto snapshot before launch when something changed. Returns snapshot
    // name, or empty when nothing needed doing.
    QString autoSnapshotIfNeeded(const QString &instanceId);

signals:
    void changed(const QString &instanceId);

private:
    QString m_dataDir;
};
