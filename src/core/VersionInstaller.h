#pragma once

#include "GamePaths.h"
#include "VersionModel.h"

#include "DownloadManager.h" // WorkPlan owns DownloadRequest values

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class DownloadManager;
class JavaManager;
class MojangApi;

// Full vanilla install as one Task (spec section 4): resolves the version +
// inheritsFrom chain, downloads and verifies the client jar, libraries
// (with native extraction + exclude rules), asset index + all objects
// (shared assets/objects hash layout, virtual/legacy support) and the
// logging XML. Shared libraries/assets keep disk use low. Everything cached
// stays usable offline; missing pieces offline fail with a clear message.
class VersionInstaller : public QObject {
    Q_OBJECT
public:
    VersionInstaller(const GamePaths &paths, MojangApi *api, DownloadManager *downloads, QObject *parent = nullptr);

    // One Task that does the whole install (or fast no-op when complete).
    Task *installTask(const QString &versionId, int maxParallel, QObject *owner = nullptr);

    // Blocking worker-thread entry point (also used by MainWindow's combined
    // prepare pipeline). Must only be called on a Task worker thread.
    bool installBlocking(const QString &versionId, int maxParallel, Task::Context &ctx);

    // Fast offline-friendly completeness check (no hashes, no network):
    // client jar + every required jar + natives marker + asset index +
    // every object present with non-zero size.
    bool isInstalled(const QString &versionId, QString *missing = nullptr) const;
    bool isInstalled(const ParsedVersion &merged, QString *missing = nullptr) const;
    // Lightweight list-view check: client jar + ready marker only.
    bool quickIsInstalled(const QString &versionId) const;

    // Required (post-rule-filter) jars + native classifier downloads.
    struct WorkPlan {
        QList<Library> activeLibs;
        QList<DownloadRequest> jarDownloads;
        struct NativeJob {
            QString coord;
            QString classifier;
            QString zipPath; // destination jar path (also the zip to extract)
            QString url;
            QString sha1;
            qint64 size = -1;
            QStringList extractExclude;
        };
        QList<NativeJob> natives;
    };
    static WorkPlan planLibraries(const ParsedVersion &merged, const OsInfo &os, const QSet<QString> &features,
                                  const GamePaths &paths);

signals:
    void installFinished(const QString &versionId, bool ok);

private:
    GamePaths m_paths;
    MojangApi *m_api = nullptr;
    DownloadManager *m_downloads = nullptr;
};
