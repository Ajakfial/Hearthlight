#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {
constexpr qint64 kMaxBytesPerFile = 1024 * 1024; // 1 MiB
constexpr int kMaxRotatedFiles = 5;
constexpr int kRingCapacity = 2000;
} // namespace

Logger &Logger::instance()
{
    static Logger s;
    return s;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
{
}

Logger::~Logger() = default;

void Logger::init(const QString &logDir)
{
    Logger &self = instance();
    QMutexLocker lock(&self.m_mutex);
    if (self.m_initialized) {
        return;
    }
    self.m_logDir = logDir;
    QDir().mkpath(logDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    self.m_logFile = QDir(logDir).filePath(QStringLiteral("hearthlight-%1.log").arg(stamp));
    self.m_bytesWritten = QFileInfo(self.m_logFile).size();
    self.m_initialized = true;
    lock.unlock();
    qInstallMessageHandler(&Logger::qtMessageHandler);
    info(QStringLiteral("Hearthlight log started: %1").arg(self.m_logFile));
}

void Logger::shutdown()
{
    qInstallMessageHandler(nullptr);
}

void Logger::debug(const QString &msg)
{
    instance().append(Level::Debug, msg);
}
void Logger::info(const QString &msg)
{
    instance().append(Level::Info, msg);
}
void Logger::warning(const QString &msg)
{
    instance().append(Level::Warning, msg);
}
void Logger::error(const QString &msg)
{
    instance().append(Level::Error, msg);
}

QString Logger::redacted(const QString &text)
{
    QString out = text;
    // --accessToken <value>, auth_access_token=<value>, "access_token":"..."
    static const QRegularExpression reAccess(
        QStringLiteral("(access[_-]?token[\"'\\s:=]+)([^\"'\\s]+)"), QRegularExpression::CaseInsensitiveOption);
    out.replace(reAccess, QStringLiteral("\\1<redacted>"));
    static const QRegularExpression reRefresh(
        QStringLiteral("(refresh[_-]?token[\"'\\s:=]+)([^\"'\\s]+)"), QRegularExpression::CaseInsensitiveOption);
    out.replace(reRefresh, QStringLiteral("\\1<redacted>"));
    static const QRegularExpression reXsts(
        QStringLiteral("(XBL3\\.0 x=[^;]+;)([^\\s]+)"));
    out.replace(reXsts, QStringLiteral("\\1<redacted>"));
    return out;
}

QString Logger::logDir()
{
    return instance().m_logDir;
}

QString Logger::currentLogFile()
{
    return instance().m_logFile;
}

QStringList Logger::recentLines(int maxLines)
{
    QMutexLocker lock(&instance().m_mutex);
    const QStringList &ring = instance().m_ring;
    if (ring.size() <= maxLines) {
        return ring;
    }
    return ring.mid(ring.size() - maxLines);
}

void Logger::append(Level level, const QString &msg)
{
    const QString clean = redacted(msg);
    const char *tag = "INFO";
    switch (level) {
    case Level::Debug: tag = "DEBUG"; break;
    case Level::Info: tag = "INFO"; break;
    case Level::Warning: tag = "WARN"; break;
    case Level::Error: tag = "ERROR"; break;
    }
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                             .arg(QString::fromLatin1(tag))
                             .arg(clean);

    QString copyForSignal;
    {
        QMutexLocker lock(&m_mutex);
        m_ring.append(line);
        while (m_ring.size() > kRingCapacity) {
            m_ring.removeFirst();
        }
        copyForSignal = line;
        if (m_initialized && !m_logFile.isEmpty()) {
            rotateIfNeeded();
            QFile f(m_logFile);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << line << '\n';
                m_bytesWritten += line.size() + 1;
            }
        }
    }
    emit lineAppended(copyForSignal);
}

void Logger::rotateIfNeeded()
{
    if (m_bytesWritten < kMaxBytesPerFile) {
        return;
    }
    // hearthlight-N.log rotation
    for (int i = kMaxRotatedFiles - 1; i >= 1; --i) {
        const QString from = QDir(m_logDir).filePath(QStringLiteral("hearthlight-%1.log").arg(i));
        const QString to = QDir(m_logDir).filePath(QStringLiteral("hearthlight-%1.log").arg(i + 1));
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    const QString first = QDir(m_logDir).filePath(QStringLiteral("hearthlight-1.log"));
    QFile::remove(first);
    QFile::rename(m_logFile, first);
    m_bytesWritten = 0;
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    QString full = msg;
    if (ctx.file) {
        full += QStringLiteral(" (%1:%2)").arg(QString::fromLatin1(ctx.file)).arg(ctx.line);
    }
    switch (type) {
    case QtDebugMsg: Logger::debug(full); break;
    case QtInfoMsg: Logger::info(full); break;
    case QtWarningMsg: Logger::warning(full); break;
    case QtCriticalMsg:
    case QtFatalMsg: Logger::error(full); break;
    }
}
