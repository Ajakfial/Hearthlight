#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

AppSettings::AppSettings(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_file(filePath)
{
    QSettings s(filePath, QSettings::IniFormat);
    m_dataDir = s.value(QStringLiteral("general/dataDir"), defaultDataDir(false)).toString();
}

QString AppSettings::defaultDataDir(bool portable)
{
    if (portable) {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("HearthlightData"));
    }
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QDir::home().filePath(QStringLiteral(".hearthlight")) : base;
}

QString AppSettings::settingsFilePath(const QString &dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("hearthlight.ini"));
}

void AppSettings::setDataDir(const QString &d)
{
    m_dataDir = d;
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("general/dataDir"), d);
    s.sync();
}

QString AppSettings::language() const
{
    QSettings s(m_file, QSettings::IniFormat);
    const QString code = s.value(QStringLiteral("general/language"), QStringLiteral("system")).toString().trimmed();
    return code.isEmpty() ? QStringLiteral("system") : code;
}

void AppSettings::setLanguage(const QString &code)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("general/language"), code.trimmed().isEmpty() ? QStringLiteral("system") : code.trimmed());
    s.sync();
}

QStringList AppSettings::availableLanguages(const QString &dataDir)
{
    QSet<QString> codes;
    const QStringList roots = { QStringLiteral(":/i18n"), QDir(dataDir).filePath(QStringLiteral("translations")) };
    for (const auto &root : roots) {
        const QDir d(root);
        if (!d.exists()) {
            continue;
        }
        for (const auto &f : d.entryList({ QStringLiteral("hearthlight_*.qm") }, QDir::Files, QDir::Name)) {
            QString code = f.mid(QStringLiteral("hearthlight_").size());
            code.chop(QStringLiteral(".qm").size());
            code = code.trimmed().toLower();
            if (!code.isEmpty() && code != QStringLiteral("en")) {
                codes.insert(code);
            }
        }
    }
    QStringList out = codes.values();
    out.sort();
    return out;
}

QString AppSettings::displayNameForLanguage(const QString &code)
{
    static const QMap<QString, QString> kNames = {
        { QStringLiteral("de"), QStringLiteral("Deutsch") },
        { QStringLiteral("fr"), QStringLiteral("Français") },
        { QStringLiteral("es"), QStringLiteral("Español") },
        { QStringLiteral("it"), QStringLiteral("Italiano") },
        { QStringLiteral("pt"), QStringLiteral("Português") },
        { QStringLiteral("nl"), QStringLiteral("Nederlands") },
        { QStringLiteral("pl"), QStringLiteral("Polski") },
        { QStringLiteral("ru"), QStringLiteral("Русский") },
        { QStringLiteral("uk"), QStringLiteral("Українська") },
        { QStringLiteral("ja"), QStringLiteral("日本語") },
        { QStringLiteral("zh"), QStringLiteral("中文") },
        { QStringLiteral("ko"), QStringLiteral("한국어") },
        { QStringLiteral("sv"), QStringLiteral("Svenska") },
        { QStringLiteral("da"), QStringLiteral("Dansk") },
        { QStringLiteral("no"), QStringLiteral("Norsk") },
        { QStringLiteral("fi"), QStringLiteral("Suomi") },
        { QStringLiteral("cs"), QStringLiteral("Čeština") },
        { QStringLiteral("hu"), QStringLiteral("Magyar") },
        { QStringLiteral("tr"), QStringLiteral("Türkçe") },
    };
    const QString base = code.trimmed().toLower().left(code.indexOf(QLatin1Char('_')) < 0
                                                           ? code.size()
                                                           : code.indexOf(QLatin1Char('_')));
    return kNames.value(base, code);
}

OfflineMode AppSettings::offlineMode() const
{
    QSettings s(m_file, QSettings::IniFormat);
    bool ok = false;
    const auto m = offlineModeFromString(s.value(QStringLiteral("general/offlineMode"), QStringLiteral("automatic")).toString(), &ok);
    Q_UNUSED(ok);
    return m;
}

void AppSettings::setOfflineMode(OfflineMode m)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("general/offlineMode"), offlineModeToString(m));
    s.sync();
}

QString AppSettings::accentColor() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("appearance/accent"), QStringLiteral("#FFB347")).toString();
}

void AppSettings::setAccentColor(const QString &hex)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("appearance/accent"), hex);
    s.sync();
}

double AppSettings::uiScale() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return qBound(0.8, s.value(QStringLiteral("appearance/scale"), 1.0).toDouble(), 2.0);
}

void AppSettings::setUiScale(double v)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("appearance/scale"), qBound(0.8, v, 2.0));
    s.sync();
}

QString AppSettings::javaPath() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("java/path")).toString();
}

void AppSettings::setJavaPath(const QString &p)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("java/path"), p);
    s.sync();
}

QString AppSettings::javaArgs() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("java/args")).toString();
}

void AppSettings::setJavaArgs(const QString &a)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("java/args"), a);
    s.sync();
}

int AppSettings::downloadParallelism() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return qBound(1, s.value(QStringLiteral("downloads/parallelism"), 8).toInt(), 32);
}

void AppSettings::setDownloadParallelism(int n)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("downloads/parallelism"), qBound(1, n, 32));
    s.sync();
}

qint64 AppSettings::downloadCacheLimit() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("downloads/cacheLimitMB"), 1024).toLongLong() * 1024 * 1024;
}

QString AppSettings::defaultAccountId() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("accounts/defaultId")).toString();
}

void AppSettings::setDefaultAccountId(const QString &id)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("accounts/defaultId"), id);
    s.sync();
}

void AppSettings::sync()
{
    QSettings s(m_file, QSettings::IniFormat);
    s.sync();
}

bool AppSettings::showReleases() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("versions/showReleases"), true).toBool();
}

void AppSettings::setShowReleases(bool v)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("versions/showReleases"), v);
    s.sync();
}

bool AppSettings::showSnapshots() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("versions/showSnapshots"), false).toBool();
}

void AppSettings::setShowSnapshots(bool v)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("versions/showSnapshots"), v);
    s.sync();
}

bool AppSettings::showBeta() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("versions/showBeta"), false).toBool();
}

void AppSettings::setShowBeta(bool v)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("versions/showBeta"), v);
    s.sync();
}

bool AppSettings::showAlpha() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("versions/showAlpha"), false).toBool();
}

void AppSettings::setShowAlpha(bool v)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("versions/showAlpha"), v);
    s.sync();
}

int AppSettings::memoryMb() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return qBound(0, s.value(QStringLiteral("java/memoryMb"), 0).toInt(), 65536);
}

void AppSettings::setMemoryMb(int mb)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("java/memoryMb"), qBound(0, mb, 65536));
    s.sync();
}

QString AppSettings::extraJvmArgs() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("java/extraArgs")).toString();
}

void AppSettings::setExtraJvmArgs(const QString &a)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("java/extraArgs"), a);
    s.sync();
}

QString AppSettings::closeBehavior() const
{
    QSettings s(m_file, QSettings::IniFormat);
    const QString b = s.value(QStringLiteral("launcher/closeBehavior"), QStringLiteral("keep")).toString();
    return (b == QStringLiteral("minimize") || b == QStringLiteral("close")) ? b : QStringLiteral("keep");
}

void AppSettings::setCloseBehavior(const QString &b)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("launcher/closeBehavior"),
               (b == QStringLiteral("minimize") || b == QStringLiteral("close")) ? b : QStringLiteral("keep"));
    s.sync();
}

QString AppSettings::lastVersionId() const
{
    QSettings s(m_file, QSettings::IniFormat);
    return s.value(QStringLiteral("versions/lastPlayed")).toString();
}

void AppSettings::setLastVersionId(const QString &id)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("versions/lastPlayed"), id);
    s.sync();
}

QString AppSettings::clientId()
{
    QSettings s(m_file, QSettings::IniFormat);
    QString id = s.value(QStringLiteral("launcher/clientId")).toString();
    if (QUuid::fromString(id).isNull()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QStringLiteral("launcher/clientId"), id);
        s.sync();
    }
    return id;
}
