#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Minimal zip extraction for Minecraft natives (and later mod loaders).
// Backed by miniz (fetched at configure time; no Qt dependency, no tools).
// Excludes follow Mojang `extract.exclude` semantics: a trailing "/" means
// "everything under this prefix", otherwise shell wildcards (* ?) apply.
namespace ZipUtil {

bool isExcluded(const QString &relPath, const QStringList &excludes);

// Extracts zipFile into destDir, skipping excluded entries.
// Creates parent dirs, overwrites existing files. Returns false + error.
bool extractZipFile(const QString &zipFile, const QString &destDir, const QStringList &excludes,
                    QString *error = nullptr, int *extractedCount = nullptr);

bool extractZipData(const QByteArray &zipData, const QString &destDir, const QStringList &excludes,
                    QString *error = nullptr, int *extractedCount = nullptr);

// Creates a zip archive from in-memory files (used for .hearthpack export).
// Each entry is (archiveName, data). Returns false + error.
bool createZipFromEntries(const QString &zipPath, const QList<QPair<QString, QByteArray>> &entries,
                          QString *error = nullptr);

// Lists archive member names (no extraction). For import validation.
QStringList listZipEntries(const QString &zipPath, QString *error = nullptr);

} // namespace ZipUtil
