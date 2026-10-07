#pragma once

#include "GamePaths.h"

#include "Task.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class DownloadManager;

struct JavaInfo {
    QString path; // full path to java(.exe)
    QString version; // raw version string, e.g. "17.0.9"
    int major = 0; // 8, 17, 21…
    bool managed = false; // Hearthlight-downloaded
    bool valid = false;
};

// Java management, beginner-invisible (spec section 5):
//  - required major comes from javaVersion.majorVersion (default 8)
//  - detects installed JVMs on all platforms; offline discovery never
//    needs network
//  - otherwise downloads Eclipse Temurin (Adoptium API) into our own
//    folder, SHA-256 verified
//  - memory suggestion from system RAM (+ mod count later)
class JavaManager : public QObject {
    Q_OBJECT
public:
    explicit JavaManager(const GamePaths &paths, DownloadManager *downloads, QObject *parent = nullptr);

    // All usable JVMs right now (custom path first if set and valid).
    // Never touches the network.
    QList<JavaInfo> detectAll(const QString &customPath = {}) const;
    JavaInfo findForMajor(int major, const QString &customPath = {}) const;

    // Ensure a compatible JVM exists, downloading a managed one if needed.
    // Offline with nothing cached: fails with a clear message.
    Task *ensureJavaTask(int major, const QString &customPath, QObject *owner = nullptr);

    // Blocking worker-thread entry point (also used by MainWindow's combined
    // prepare pipeline). Must only be called on a Task worker thread.
    bool ensureBlocking(int major, const QString &customPath, Task::Context &ctx);

    // Run `java -version` and parse the result. Real check, no guessing.
    static bool testJava(const QString &path, JavaInfo *out = nullptr, QString *output = nullptr);
    static int parseMajor(const QString &versionOutput);

    // Memory suggestion (MB): quarter of RAM clamped to [1024, 8192],
    // plus 16MB per mod capped at +2048. Vanilla on 8GB -> 2048.
    static int suggestMemoryMb(qint64 systemRamMb, int modCount = 0);
    static qint64 systemRamMb();
    static bool isRecommendedMemory(int mb, qint64 systemRamMb, int modCount = 0);

    static int requiredMajorFor(int javaVersionMajor);
    static QString adoptiumOs(); // windows | linux | mac
    static QString adoptiumArch(); // x64 | aarch64 | x86

private:
    GamePaths m_paths;
    DownloadManager *m_downloads = nullptr;
};
