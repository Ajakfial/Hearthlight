#pragma once

#include "Instance.h"
#include "Task.h"

#include <QList>
#include <QObject>
#include <QString>

class Task;

// Owns all instances (spec section 7):
//   <dataDir>/instances/<id>/instance.json + game/{mods,config,saves,…}
//
// Create via wizard, clone/rename/export/delete (trash), drag-reorder,
// groups, per-instance settings, Quick Play, import/export (.hearthpack,
// .mrpack, CurseForge full import with a user API key). Offline profiles
// launch without internet as long as everything required is cached.
class InstanceManager : public QObject {
    Q_OBJECT
public:
    explicit InstanceManager(const QString &dataDir, QObject *parent = nullptr);

    QString instancesDir() const;
    bool load(); // scan disk; emits loaded()
    bool saveAll(); // persist order + all instance.json

    QList<Instance> instances() const; // sorted by order
    Instance get(const QString &id) const;
    bool has(const QString &id) const;
    QStringList groups() const;

    // Takes a mostly-filled Instance (name/version/loader/account) and
    // assigns a unique id + order, creates folders + instance.json.
    bool create(Instance in, QString *error = nullptr);
    bool update(const Instance &in, QString *error = nullptr);
    bool rename(const QString &id, const QString &newName, QString *error = nullptr);
    bool clone(const QString &id, const QString &newName, QString *newIdOut = nullptr, QString *error = nullptr);
    // toTrash=true uses QFile::moveToTrash (recoverable); false deletes.
    bool remove(const QString &id, bool toTrash = true);
    bool move(const QString &id, int newIndex); // drag-reorder
    bool setGroup(const QString &id, const QString &group);

    void recordPlay(const QString &id, qint64 seconds, const QString &accountName);

    // --- Import / export (blocking helpers for Task worker threads) ---
    // Hearthlight's own .hearthpack: zip with instance.json + overrides/.
    bool exportHearthpack(const QString &id, const QString &zipPath, QString *error);
    bool importHearthpack(const QString &zipPath, QString *newIdOut, QString *error);
    // Modrinth .mrpack: manifest + overrides, files downloaded by URL.
    // Blocking: downloads file list inside ctx. Offline fails honestly.
    bool importMrpackBlocking(const QString &zipPath, const QString &newName, Task::Context &ctx,
                              QString *newIdOut = nullptr);
    // Export this profile as a Modrinth .mrpack (modrinth.index.json +
    // overrides/). Modrinth-known mods become URL downloads; everything else
    // (configs, packs, hand-added jars) rides along in overrides/.
    bool exportMrpack(const QString &id, const QString &zipPath, QString *error);
    // CurseForge-style zip (manifest.json + overrides): best-effort local
    // extraction only (CF file CDN needs an API key we don't ship). Files
    // already in the zip are installed; remote files are listed as skipped.
    bool importCurseforgeZip(const QString &zipPath, const QString &newName, QString *newIdOut, QString *error,
                             QStringList *skippedRemote = nullptr);
    // Full CurseForge import with a user-supplied API key: resolves every
    // (projectID, fileID) via the CurseForge API and downloads the files.
    // Blocking on a Task worker thread. Empty apiKey falls back to the
    // best-effort local import above.
    bool importCurseforgeZipBlocking(const QString &zipPath, const QString &newName, const QString &apiKey,
                                      Task::Context &ctx, QString *newIdOut = nullptr,
                                      QStringList *skippedOut = nullptr);

    // Safety backup before loader/version switches: copies mods+config+saves.
    // Returns the backup dir (empty on failure).
    QString backupForSwitch(const QString &id, QString *error = nullptr);

    // Ensure the per-instance game dir tree exists.
    void ensureGameDirs(const QString &id) const;

signals:
    void loaded();
    void changed();
    void instanceAdded(const QString &id);
    void instanceRemoved(const QString &id);

private:
    QString uniqueIdFor(const QString &base) const;
    bool writeOne(const Instance &in) const;

    QString m_dataDir;
    QList<Instance> m_instances;
};
