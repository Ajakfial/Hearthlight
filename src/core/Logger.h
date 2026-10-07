#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

// Central logging: rotating log files + in-memory ring for the in-app viewer.
// Thread-safe. Installs a Qt message handler on init() so qDebug/qWarning etc.
// are captured as well. Never logs tokens (see Logger::redacted()).
class Logger : public QObject {
    Q_OBJECT
public:
    enum class Level { Debug = 0, Info, Warning, Error };

    static Logger &instance();

    // Must be called once at startup. Safe to call with no network.
    static void init(const QString &logDir);
    static void shutdown();

    static void debug(const QString &msg);
    static void info(const QString &msg);
    static void warning(const QString &msg);
    static void error(const QString &msg);

    // Redact anything that looks like an access/refresh token before logging.
    // Launch pipeline must pass all game args through this before writing logs.
    static QString redacted(const QString &text);

    static QString logDir();
    static QString currentLogFile();
    static QStringList recentLines(int maxLines = 500);

signals:
    void lineAppended(const QString &formattedLine);

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void append(Level level, const QString &msg);
    void rotateIfNeeded();

    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg);

    QMutex m_mutex;
    QString m_logDir;
    QString m_logFile;
    QStringList m_ring; // capped in-memory history for the viewer
    qint64 m_bytesWritten = 0;
    bool m_initialized = false;
};
