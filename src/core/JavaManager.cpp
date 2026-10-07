#include "JavaManager.h"

#include "DownloadManager.h"
#include "Logger.h"
#include "NetworkStatus.h"
#include "ZipUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#else
#include <unistd.h>
#endif

JavaManager::JavaManager(const GamePaths &paths, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_downloads(downloads)
{
    Q_UNUSED(m_downloads);
}

int JavaManager::requiredMajorFor(int javaVersionMajor)
{
    return javaVersionMajor > 0 ? javaVersionMajor : 8;
}

QString JavaManager::adoptiumOs()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("mac");
#else
    return QStringLiteral("linux");
#endif
}

QString JavaManager::adoptiumArch()
{
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    if (cpu.contains(QStringLiteral("arm64")) || cpu.contains(QStringLiteral("aarch64"))) {
        return QStringLiteral("aarch64");
    }
    if (cpu.contains(QStringLiteral("64"))) {
        return QStringLiteral("x64");
    }
    return QStringLiteral("x86");
}

int JavaManager::parseMajor(const QString &versionOutput)
{
    // Matches: openjdk version "17.0.9" ..., java version "1.8.0_392" ...,
    // openjdk version "21-ea" ...
    static const QRegularExpression re(QStringLiteral("version \"([^\"]+)\""));
    const auto m = re.match(versionOutput);
    if (!m.hasMatch()) {
        return 0;
    }
    const QString v = m.captured(1);
    if (v.startsWith(QStringLiteral("1."))) {
        return v.mid(2).split(QLatin1Char('.')).first().toInt();
    }
    return v.split(QRegularExpression(QStringLiteral("[.\\-+]"))).first().toInt();
}

bool JavaManager::testJava(const QString &path, JavaInfo *out, QString *output)
{
    QString prog = path.trimmed();
    if (prog.isEmpty()) {
        prog = QStringLiteral("java");
    }
    QProcess p;
    p.start(prog, { QStringLiteral("-version") });
    if (!p.waitForFinished(15000)) {
        return false;
    }
    if (p.exitCode() != 0) {
        return false;
    }
    const QString text = QString::fromLocal8Bit(p.readAllStandardError() + p.readAllStandardOutput());
    if (output) {
        *output = text.trimmed().left(1000);
    }
    const int major = parseMajor(text);
    if (major <= 0) {
        return false;
    }
    if (out) {
        out->valid = true;
        out->major = major;
        out->path = prog;
        static const QRegularExpression re(QStringLiteral("version \"([^\"]+)\""));
        const auto m = re.match(text);
        out->version = m.hasMatch() ? m.captured(1) : QString::number(major);
    }
    return true;
}

static QString exeName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("java.exe");
#else
    return QStringLiteral("java");
#endif
}

QList<JavaInfo> JavaManager::detectAll(const QString &customPath) const
{
    QList<JavaInfo> out;
    QSet<QString> seen;
    auto consider = [&](const QString &path, bool managed) {
        if (path.isEmpty() || seen.contains(QFileInfo(path).absoluteFilePath())) {
            return;
        }
        JavaInfo info;
        if (testJava(path, &info, nullptr)) {
            info.path = path;
            info.managed = managed;
            seen.insert(QFileInfo(path).absoluteFilePath());
            out.append(info);
        }
    };

    if (!customPath.trimmed().isEmpty()) {
        consider(customPath.trimmed(), false);
    }
    // Hearthlight-managed runtimes first (known good for our versions).
    QDir jd(m_paths.javaDir);
    for (const auto &sub : jd.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numOk = false;
        sub.toInt(&numOk);
        if (!numOk) {
            continue;
        }
        const QString home = QDir(m_paths.managedJavaDir(sub.toInt())).absolutePath();
        // Layout: <major>/bin/java(.exe), or nested home one level down.
        consider(QDir(home).filePath(QStringLiteral("bin/%1").arg(exeName())), true);
        QDir h(home);
        for (const auto &inner : h.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            consider(QDir(home).filePath(QStringLiteral("%1/bin/%2").arg(inner).arg(exeName())), true);
        }
    }
    // JAVA_HOME.
    const QString jh = QString::fromLocal8Bit(qgetenv("JAVA_HOME")).trimmed();
    if (!jh.isEmpty()) {
        consider(QDir(jh).filePath(QStringLiteral("bin/%1").arg(exeName())), false);
    }
    // PATH.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!onPath.isEmpty()) {
        consider(onPath, false);
    }
    // Well-known system locations (no network, just stats).
    QStringList guesses;
#if defined(Q_OS_WIN)
    for (const auto &root :
         { QStringLiteral("C:/Program Files/Eclipse Adoptium"), QStringLiteral("C:/Program Files/Java"),
           QStringLiteral("C:/Program Files (x86)/Eclipse Adoptium"), QStringLiteral("C:/Program Files (x86)/Java") }) {
        QDir d(root);
        for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            guesses.append(QDir(root).filePath(QStringLiteral("%1/bin/java.exe").arg(sub)));
        }
    }
#elif defined(Q_OS_MACOS)
    for (const auto &root :
         { QStringLiteral("/Library/Java/JavaVirtualMachines"), QDir::home().filePath(QStringLiteral("Library/Java/JavaVirtualMachines")) }) {
        QDir d(root);
        for (const auto &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            guesses.append(QDir(root).filePath(QStringLiteral("%1/Contents/Home/bin/java").arg(sub)));
        }
    }
    guesses.append(QStringLiteral("/opt/homebrew/opt/openjdk/bin/java"));
    guesses.append(QStringLiteral("/usr/local/opt/openjdk/bin/java"));
#else
    QDir jvm(QStringLiteral("/usr/lib/jvm"));
    for (const auto &sub : jvm.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        guesses.append(QStringLiteral("/usr/lib/jvm/%1/bin/java").arg(sub));
    }
    guesses.append(QStringLiteral("/usr/lib64/jvm/java/bin/java"));
#endif
    for (const auto &g : guesses) {
        consider(g, false);
    }
    // Prefer newest major first (managed builds win ties).
    std::sort(out.begin(), out.end(), [](const JavaInfo &a, const JavaInfo &b) {
        if (a.major != b.major) {
            return a.major > b.major;
        }
        return a.managed && !b.managed;
    });
    return out;
}

JavaInfo JavaManager::findForMajor(int major, const QString &customPath) const
{
    for (const auto &j : detectAll(customPath)) {
        // Java is forwards compatible within reason: accept same major or
        // newer (e.g. 21 runs 17-era games). Never accept older.
        if (j.major >= major) {
            return j;
        }
    }
    return {};
}

Task *JavaManager::ensureJavaTask(int major, const QString &customPath, QObject *owner)
{
    auto *t = new LambdaTask(
        tr("Prepare Java %1").arg(major),
        [this, major, customPath](Task::Context &ctx) { return ensureBlocking(major, customPath, ctx); },
        owner ? owner : this);
    return t;
}

static bool extractTarGz(const QString &archive, const QString &destDir, QString *error)
{
    // System tar exists on virtually every Linux/macOS. Windows Temurin
    // ships .zip so this path is Unix-only by construction.
    QDir().mkpath(destDir);
    QProcess tar;
    tar.start(QStringLiteral("tar"), { QStringLiteral("-xzf"), archive, QStringLiteral("-C"), destDir });
    if (!tar.waitForFinished(120000)) {
        if (error) {
            *error = QStringLiteral("Archive extraction timed out.");
        }
        return false;
    }
    if (tar.exitCode() != 0) {
        if (error) {
            *error = QString::fromLocal8Bit(tar.readAllStandardError()).trimmed().left(500);
        }
        return false;
    }
    return true;
}

bool JavaManager::ensureBlocking(int major, const QString &customPath, Task::Context &ctx)
{
    // 1. Anything already installed (custom, managed, or system)?
    if (findForMajor(major, customPath).valid) {
        ctx.report(1, 1, tr("Done"));
        return true;
    }
    // 2. Offline: no download possible. Say exactly that.
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        ctx.fail(tr("No Java %1 found and you're offline. Go online once to let Hearthlight "
                    "download a runtime, or point Settings → Java at an existing one.")
                     .arg(major));
        return false;
    }
    // 3. Query the Adoptium assets API for a matching Temurin binary.
    ctx.report(0, 10, tr("Finding a Java %1 download…").arg(major));
    const QUrl assetsUrl(QStringLiteral("https://api.adoptium.net/v3/assets/latest/%1/hotspot").arg(major));
    DownloadRequest metaReq{ assetsUrl, QDir(m_paths.cacheDir).filePath(QStringLiteral("temurin-%1.json").arg(major)) };
    metaReq.resume = false;
    QString err;
    if (!DownloadManager::downloadManyBlocking({ metaReq }, 1, ctx, &err)) {
        ctx.fail(tr("Couldn't reach the Java download service: %1").arg(err));
        return false;
    }
    QFile mf(metaReq.destPath);
    if (!mf.open(QIODevice::ReadOnly)) {
        ctx.fail(tr("Couldn't read the Java download listing."));
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) {
        ctx.fail(tr("The Java download service returned an unexpected answer."));
        return false;
    }
    const QString wantOs = adoptiumOs();
    const QString wantArch = adoptiumArch();
    QString pkgLink, pkgChecksum;
    qint64 pkgSize = -1;
    bool isZip = false;
    const QJsonObject release = doc.array().first().toObject();
    int bestScore = -1;
    for (const auto &b : release.value(QStringLiteral("binaries")).toArray()) {
        const QJsonObject bin = b.toObject();
        if (bin.value(QStringLiteral("os")).toString() != wantOs) {
            continue;
        }
        if (bin.value(QStringLiteral("architecture")).toString() != wantArch) {
            continue;
        }
        const QString image = bin.value(QStringLiteral("image_type")).toString();
        int score = (image == QStringLiteral("jre")) ? 2 : (image == QStringLiteral("jdk") ? 1 : 0);
        if (score <= bestScore) {
            continue;
        }
        const QJsonObject pkg = bin.value(QStringLiteral("package")).toObject();
        const QString link = pkg.value(QStringLiteral("link")).toString();
        if (link.isEmpty()) {
            continue;
        }
        bestScore = score;
        pkgLink = link;
        pkgChecksum = pkg.value(QStringLiteral("checksum")).toString();
        pkgSize = static_cast<qint64>(pkg.value(QStringLiteral("size")).toDouble(-1));
        isZip = link.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
    }
    if (pkgLink.isEmpty()) {
        ctx.fail(tr("No Eclipse Temurin Java %1 for %2/%3. Install a JVM manually and point "
                    "Settings → Java at it.")
                     .arg(major)
                     .arg(wantOs)
                     .arg(wantArch));
        return false;
    }
    // 4. Download + verify.
    ctx.report(1, 10, tr("Downloading Java %1…").arg(major));
    const QString archiveName = pkgLink.split(QLatin1Char('/')).last();
    const QString archive = QDir(m_paths.cacheDir).filePath(archiveName);
    DownloadRequest dl{ QUrl(pkgLink), archive };
    dl.expectedSha256 = pkgChecksum.toLatin1();
    dl.expectedSize = pkgSize;
    if (!DownloadManager::downloadManyBlocking({ dl }, 1, ctx, &err)) {
        ctx.fail(err);
        return false;
    }
    // 5. Extract into java/<major>/.
    ctx.report(9, 10, tr("Installing Java %1…").arg(major));
    const QString home = m_paths.managedJavaDir(major);
    QDir(home).removeRecursively();
    QDir().mkpath(home);
    if (isZip) {
        if (!ZipUtil::extractZipFile(archive, home, {}, &err)) {
            ctx.fail(tr("Couldn't unpack Java %1 (%2).").arg(major).arg(err));
            return false;
        }
    } else {
#if defined(Q_OS_WIN)
        ctx.fail(tr("Unexpected archive format for Windows."));
        return false;
#else
        if (!extractTarGz(archive, home, &err)) {
            ctx.fail(tr("Couldn't unpack Java %1 (%2). Is 'tar' installed?").arg(major).arg(err));
            return false;
        }
#endif
    }
    // Temurin archives contain one top-level directory (jdk-17.x/...):
    // collapse a single-child layout so home/bin/java exists.
    auto javaAt = [&](const QString &dir) { return QDir(dir).filePath(QStringLiteral("bin/%1").arg(exeName())); };
    if (!QFile::exists(javaAt(home))) {
        QDir h(home);
        const QStringList kids = h.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        bool collapsed = false;
        if (kids.size() == 1 && QFile::exists(javaAt(QDir(home).filePath(kids.first())))) {
            const QString inner = QDir(home).filePath(kids.first());
            // Move inner/* up one level.
            for (const auto &e : QDir(inner).entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
                QFile::rename(QDir(inner).filePath(e), QDir(home).filePath(e));
            }
            QDir(inner).removeRecursively();
            collapsed = QFile::exists(javaAt(home));
        }
        if (!collapsed) {
            // Search one more level (some vendors nest twice).
            for (const auto &kid : kids) {
                const QString cand = javaAt(QDir(home).filePath(kid));
                if (QFile::exists(cand)) {
                    JavaInfo info;
                    if (testJava(cand, &info, nullptr) && info.major >= major) {
                        QFile marker(QDir(home).filePath(QStringLiteral(".installed")));
                        if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
                            marker.write(QByteArray::number(major));
                        }
                        ctx.report(1, 1, tr("Done"));
                        Logger::info(QStringLiteral("Installed managed Java %1 (%2)").arg(major).arg(cand));
                        return true;
                    }
                }
            }
            ctx.fail(tr("Java %1 unpacked but no working binary was found.").arg(major));
            return false;
        }
    }
#ifndef Q_OS_WIN
    QFile::setPermissions(javaAt(home),
                          QFile::permissions(javaAt(home)) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
#endif
    // 6. Sanity-test the result.
    JavaInfo info;
    if (!testJava(javaAt(home), &info, nullptr) || info.major < major) {
        ctx.fail(tr("The downloaded Java %1 didn't pass its self-test.").arg(major));
        return false;
    }
    QFile marker(QDir(home).filePath(QStringLiteral(".installed")));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
        marker.write(QByteArray::number(major));
    }
    QFile::remove(archive); // save disk; re-downloadable any time
    ctx.report(1, 1, tr("Done"));
    Logger::info(QStringLiteral("Installed managed Java %1").arg(major));
    return true;
}

qint64 JavaManager::systemRamMb()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) {
        return static_cast<qint64>(st.ullTotalPhys / 1024 / 1024);
    }
    return 4096;
#else
    // sysconf fallback for macOS/Linux.
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        return static_cast<qint64>(pages) * pageSize / 1024 / 1024;
    }
    return 4096;
#endif
}

int JavaManager::suggestMemoryMb(qint64 systemRamMb, int modCount)
{
    qint64 base = systemRamMb / 4;
    base = qBound(qint64(1024), base, qint64(8192));
    base += qMin<qint64>(2048, static_cast<qint64>(modCount) * 16);
    // Round to a friendly 256MB step.
    return static_cast<int>((base + 128) / 256 * 256);
}

bool JavaManager::isRecommendedMemory(int mb, qint64 systemRamMb, int modCount)
{
    return qAbs(mb - suggestMemoryMb(systemRamMb, modCount)) <= 256;
}
