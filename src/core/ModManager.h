#pragma once

#include "ModrinthApi.h"
#include "Task.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// Per-profile installed-mods manager (spec 9, "Installed mods tab").
// Mods live in game/mods/*.jar; disabled mods are *.jar.disabled.
// Modrinth-installed mods carry a sidecar (mods/.hearth/<base>.json);
// jars without one are "manual" (drag-dropped or hand-placed).
struct InstalledMod {
    QString fileName; // e.g. "sodium-1.2.jar" (or "...jar.disabled")
    QString baseName; // fileName minus .jar / .jar.disabled
    QString projectId;
    QString slug;
    QString title; // sidecar title, else baseName
    QString versionId;
    QString versionNumber;
    QString fileUrl;
    QString sha512;
    qint64 size = 0;
    bool enabled = true;
    bool manual = true;
    QString disabledPath; // full path of the toggled-off twin, when relevant
};

struct ModUpdate {
    InstalledMod installed;
    ModrinthVersion newer;
};

class ModManager : public QObject {
    Q_OBJECT
public:
    explicit ModManager(const QString &dataDir, QObject *parent = nullptr);

    // Synchronous local reads (no network, GUI-safe).
    QList<InstalledMod> listMods(const QString &instanceId) const;
    static QString modsDir(const QString &dataDir, const QString &instanceId);
    static QString sidecarDir(const QString &dataDir, const QString &instanceId);
    static QString sidecarPath(const QString &dataDir, const QString &instanceId, const QString &baseName);

    // --- Content packs (resourcepacks / shaderpacks / datapacks) ---
    // Same on-disk conventions as mods: *.zip (+ *.zip.disabled to turn off),
    // listed synchronously with no network. Datapacks staged here can be
    // copied into a world's datapacks folder; resource/shader packs apply
    // directly from these folders.
    static QString contentDir(const QString &dataDir, const QString &instanceId, const QString &folder);
    QList<InstalledMod> listContent(const QString &instanceId, const QString &folder) const;
    bool setContentEnabled(const QString &instanceId, const QString &folder, const QString &fileName, bool enabled,
                           QString *error = nullptr);
    bool removeContent(const QString &instanceId, const QString &folder, const QString &fileName,
                       QString *error = nullptr);
    // extensions like {"*.zip"}; accepts a file path, copies it in.
    bool addExternalPack(const QString &instanceId, const QString &folder, const QString &filePath,
                         const QStringList &extensions, QString *error = nullptr, QString *addedName = nullptr);
    static QStringList contentExtensions(const QString &folder);

    // Enable/disable (rename) and remove (to trash when possible).
    bool setEnabled(const QString &instanceId, const QString &fileName, bool enabled, QString *error = nullptr);
    bool removeMod(const QString &instanceId, const QString &fileName, QString *error = nullptr);

    // Add an external .jar (drag-and-drop path) into mods/. Detects name
    // clashes honestly (fails instead of overwriting).
    bool addExternalJar(const QString &instanceId, const QString &jarPath, QString *error = nullptr,
                        QString *addedName = nullptr);

    // Download one resolved unit into the right folder + write its sidecar.
    // Blocking on a Task worker thread. SHA-512 verified when provided.
    bool installUnitBlocking(const QString &instanceId, const ModInstallPlan &unit, int maxParallel,
                             Task::Context &ctx);

    // Check every sidecar'd mod for a newer compatible version.
    // Blocking on a Task worker thread (network). Offline fails honestly.
    QList<ModUpdate> checkUpdatesBlocking(const QString &instanceId, const QString &mcVersion,
                                          const QString &loader, Task::Context &ctx, class ModrinthApi *api);

signals:
    void modsChanged(const QString &instanceId);

private:
    QString m_dataDir;
};
