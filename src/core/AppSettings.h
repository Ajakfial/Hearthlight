#pragma once

#include "NetworkStatus.h"

#include <QObject>
#include <QString>

// Typed settings + data-folder resolution. Backed by QSettings (INI).
// Portable mode: <exe-dir>/HearthlightData when portable.txt sits next to
// the binary or --portable is passed; otherwise OS app-data location.
//
// All paths tolerate spaces/Unicode; no network needed.
//
// Lives in core/ so both ui/ (Settings page) and app/ (bootstrap) can use it
// without creating a ui<->app link cycle. Owned at runtime by Application.
class AppSettings : public QObject {
    Q_OBJECT
public:
    explicit AppSettings(const QString &filePath, QObject *parent = nullptr);

    static QString defaultDataDir(bool portable);
    static QString settingsFilePath(const QString &dataDir);

    QString dataDir() const { return m_dataDir; }
    void setDataDir(const QString &d);

    // UI language: "system" (default, follows the OS locale) or a Qt locale
    // code like "en", "de". Translations load from :/i18n/ then
    // <dataDir>/translations/ as hearthlight_<lang>.qm; missing files fall
    // back to built-in English. Takes effect on restart.
    QString language() const;
    void setLanguage(const QString &code);
    // Locale codes with a hearthlight_<code>.qm in either location,
    // sorted, deduplicated ("en" needs no file and is always implied).
    static QStringList availableLanguages(const QString &dataDir);
    static QString displayNameForLanguage(const QString &code);

    OfflineMode offlineMode() const;
    void setOfflineMode(OfflineMode m);

    QString accentColor() const;
    void setAccentColor(const QString &hex);

    double uiScale() const;
    void setUiScale(double s);

    QString javaPath() const;
    void setJavaPath(const QString &p);
    QString javaArgs() const;
    void setJavaArgs(const QString &a);

    int downloadParallelism() const;
    void setDownloadParallelism(int n);
    qint64 downloadCacheLimit() const;

    QString defaultAccountId() const;
    void setDefaultAccountId(const QString &id);

    // Version list filters (releases shown by default).
    bool showReleases() const;
    void setShowReleases(bool v);
    bool showSnapshots() const;
    void setShowSnapshots(bool v);
    bool showBeta() const;
    void setShowBeta(bool v);
    bool showAlpha() const;
    void setShowAlpha(bool v);

    // Launch defaults (per-instance overrides take precedence).
    int memoryMb() const; // 0 = automatic (recommended for this machine)
    void setMemoryMb(int mb);
    QString extraJvmArgs() const;
    void setExtraJvmArgs(const QString &a);
    // "keep" | "minimize" | "close": what the window does while the game runs.
    QString closeBehavior() const;
    void setCloseBehavior(const QString &b);
    QString lastVersionId() const;
    void setLastVersionId(const QString &id);
    // Persistent random id backing the ${clientid} launch placeholder.
    QString clientId();

    void sync();

private:
    QString m_file;
    QString m_dataDir;
};
