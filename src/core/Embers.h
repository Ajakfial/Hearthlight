#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Embers — safe updates (spec 10.2).
// Before any mod/loader/version change, Hearthlight silently snapshots the
// profile (mods + config + instance.json, optionally saves). One click
// ("Undo last change") restores the newest snapshot. Snapshots are plain
// copies under instances/<id>/.embers/<stamp>-<reason>/, pruned to a small
// count so they never eat the disk.
struct EmberSnapshot {
    QString name; // dir name: "<stamp>-<reason>"
    QString reason;
    QString created; // ISO 8601
    qint64 sizeBytes = 0;
};

class Embers : public QObject {
    Q_OBJECT
public:
    explicit Embers(const QString &dataDir, QObject *parent = nullptr);

    static QString embersDir(const QString &dataDir, const QString &instanceId);
    static int maxSnapshots() { return 5; }

    QList<EmberSnapshot> snapshots(const QString &instanceId) const;
    // Snapshot now. includeSaves=true for loader/version switches.
    // Returns the snapshot dir (empty on failure).
    QString snapshot(const QString &instanceId, const QString &reason, bool includeSaves = false,
                     QString *error = nullptr);
    // Restore a snapshot over the live profile (current state is preserved
    // first as a pre-undo snapshot, so nothing is ever lost).
    bool restore(const QString &instanceId, const QString &snapshotName, QString *error = nullptr);
    bool undoLast(const QString &instanceId, QString *error = nullptr);

    // Most recently snapshotted instance (for the Hearth "Undo" shortcut).
    // Stored in meta/embers-last.json.
    QString lastTouchedInstance() const;
    QString lastSnapshotName() const;

signals:
    void changed(const QString &instanceId);

private:
    void touchLast(const QString &instanceId, const QString &snapshotName) const;
    QString m_dataDir;
};
