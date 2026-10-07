#include "ZipUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "miniz.h"

namespace ZipUtil {

bool isExcluded(const QString &relPath, const QStringList &excludes)
{
    QString norm = relPath;
    norm.replace(QLatin1Char('\\'), QLatin1Char('/'));
    for (const auto &pat : excludes) {
        if (pat.isEmpty()) {
            continue;
        }
        if (pat.endsWith(QLatin1Char('/'))) {
            if (norm.startsWith(pat) || norm == pat.chopped(1)) {
                return true;
            }
            continue;
        }
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pat));
        if (re.match(norm).hasMatch()) {
            return true;
        }
    }
    return false;
}

static bool extractImpl(mz_zip_archive &zip, const QString &destDir, const QStringList &excludes, QString *error,
                        int *extractedCount)
{
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    int extracted = 0;
    // Zip-slip guard: every entry must stay inside destDir.
    const QString base = QDir(destDir).absolutePath();
    for (mz_uint i = 0; i < count; ++i) {
        char nameBuf[1024];
        if (mz_zip_reader_get_filename(&zip, i, nameBuf, sizeof(nameBuf)) == 0) {
            continue;
        }
        QString rel = QString::fromUtf8(nameBuf).replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (rel.startsWith(QLatin1Char('/'))) {
            rel.remove(0, 1);
        }
        if (rel.isEmpty() || rel.endsWith(QLatin1Char('/'))) {
            continue; // directory entry
        }
        if (isExcluded(rel, excludes)) {
            continue;
        }
        const QString abs = QDir::cleanPath(QDir(base).filePath(rel));
        // Zip-slip guard: every entry must stay inside destDir.
        if (abs != base && !abs.startsWith(base + QLatin1Char('/'))) {
            continue;
        }
        QDir().mkpath(QFileInfo(abs).absolutePath());
        if (mz_zip_reader_extract_to_file(&zip, i, abs.toUtf8().constData(), 0) == 0) {
            if (error) {
                *error = QStringLiteral("Failed to extract %1").arg(rel);
            }
            return false;
        }
        ++extracted;
    }
    if (extractedCount) {
        *extractedCount = extracted;
    }
    return true;
}

bool extractZipFile(const QString &zipFile, const QString &destDir, const QStringList &excludes, QString *error,
                    int *extractedCount)
{
    QDir().mkpath(destDir);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_file(&zip, zipFile.toUtf8().constData(), 0) == 0) {
        if (error) {
            *error = QStringLiteral("Not a readable zip archive: %1").arg(zipFile);
        }
        return false;
    }
    const bool ok = extractImpl(zip, destDir, excludes, error, extractedCount);
    mz_zip_reader_end(&zip);
    return ok;
}

bool extractZipData(const QByteArray &zipData, const QString &destDir, const QStringList &excludes, QString *error,
                    int *extractedCount)
{
    QDir().mkpath(destDir);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_mem(&zip, zipData.constData(), static_cast<size_t>(zipData.size()), 0) == 0) {
        if (error) {
            *error = QStringLiteral("Not a readable zip archive (in-memory).");
        }
        return false;
    }
    const bool ok = extractImpl(zip, destDir, excludes, error, extractedCount);
    mz_zip_reader_end(&zip);
    return ok;
}

bool createZipFromEntries(const QString &zipPath, const QList<QPair<QString, QByteArray>> &entries,
                          QString *error)
{
    QDir().mkpath(QFileInfo(zipPath).absolutePath());
    QFile::remove(zipPath);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_writer_init_file(&zip, zipPath.toUtf8().constData(), 0) == 0) {
        if (error) {
            *error = QStringLiteral("Couldn't write %1.").arg(zipPath);
        }
        return false;
    }
    bool ok = true;
    for (const auto &e : entries) {
        const QByteArray arc = e.first.toUtf8();
        if (mz_zip_writer_add_mem(&zip, arc.constData(), e.second.constData(), e.second.size(),
                                  MZ_DEFAULT_COMPRESSION) == 0) {
            ok = false;
            break;
        }
    }
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok && error) {
        *error = QStringLiteral("Couldn't write %1.").arg(zipPath);
    }
    return ok;
}

QStringList listZipEntries(const QString &zipPath, QString *error)
{
    QStringList out;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0) == 0) {
        if (error) {
            *error = QStringLiteral("Not a readable zip archive: %1").arg(zipPath);
        }
        return out;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    char nameBuf[1024];
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_get_filename(&zip, i, nameBuf, sizeof(nameBuf)) != 0) {
            out.append(QString::fromUtf8(nameBuf));
        }
    }
    mz_zip_reader_end(&zip);
    return out;
}

} // namespace ZipUtil
