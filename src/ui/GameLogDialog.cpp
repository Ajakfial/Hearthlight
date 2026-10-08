#include "GameLogDialog.h"

#include "Constants.h"
#include "Logger.h"
#include "NetworkStatus.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSysInfo>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

GameLogDialog::GameLogDialog(const QString &versionId, QWidget *parent)
    : QDialog(parent)
    , m_version(versionId)
    , m_startMs(QDateTime::currentMSecsSinceEpoch())
{
    setWindowTitle(tr("Campfire — %1").arg(versionId));
    resize(840, 560);

    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Search:"), this));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("filter…"));
    connect(m_search, &QLineEdit::textChanged, this, &GameLogDialog::applyFilter);
    top->addWidget(m_search, 1);
    m_level = new QComboBox(this);
    m_level->addItem(tr("Everything"), 0);
    m_level->addItem(tr("Warnings & errors"), 1);
    m_level->addItem(tr("Errors only"), 2);
    m_level->setToolTip(tr("Show only the noisy lines."));
    connect(m_level, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GameLogDialog::applyFilter);
    top->addWidget(m_level);
    m_nextBtn = new QPushButton(tr("Next error"), this);
    m_nextBtn->setToolTip(tr("Jump to the next warning or error below the cursor."));
    connect(m_nextBtn, &QPushButton::clicked, this, &GameLogDialog::onNextError);
    top->addWidget(m_nextBtn);
    auto *autoBox = new QCheckBox(tr("Auto-scroll"), this);
    autoBox->setChecked(true);
    connect(autoBox, &QCheckBox::toggled, this, [this](bool v) { m_auto = v; });
    top->addWidget(autoBox);
    auto *copyBtn = new QPushButton(tr("Copy"), this);
    copyBtn->setToolTip(tr("Copy the whole log to the clipboard."));
    connect(copyBtn, &QPushButton::clicked, this, &GameLogDialog::onCopy);
    top->addWidget(copyBtn);
    auto *saveBtn = new QPushButton(tr("Save…"), this);
    saveBtn->setToolTip(tr("Save the (redacted) log to a file."));
    connect(saveBtn, &QPushButton::clicked, this, &GameLogDialog::onSave);
    top->addWidget(saveBtn);
    auto *sysBtn = new QPushButton(tr("System info"), this);
    sysBtn->setToolTip(tr("Copy OS + launcher details for bug reports."));
    connect(sysBtn, &QPushButton::clicked, this, &GameLogDialog::onCopySysInfo);
    top->addWidget(sysBtn);
    m_shareBtn = new QPushButton(tr("Share…"), this);
    m_shareBtn->setToolTip(tr("Upload a redacted copy to mclo.gs and get a link."));
    connect(m_shareBtn, &QPushButton::clicked, this, &GameLogDialog::onShare);
    top->addWidget(m_shareBtn);
    lay->addLayout(top);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas, Menlo, monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_view->setFont(mono);
    lay->addWidget(m_view, 1);

    auto *bottom = new QHBoxLayout();
    m_status = new QLabel(tr("Starting…"), this);
    m_status->setObjectName(QStringLiteral("secondary"));
    bottom->addWidget(m_status, 1);
    m_counts = new QLabel(this);
    m_counts->setObjectName(QStringLiteral("secondary"));
    bottom->addWidget(m_counts);
    auto *folderBtn = new QPushButton(tr("Open game folder"), this);
    connect(folderBtn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_gameDir)); });
    bottom->addWidget(folderBtn);
    auto *stopBtn = new QPushButton(tr("Stop game"), this);
    connect(stopBtn, &QPushButton::clicked, this, &GameLogDialog::onStop);
    bottom->addWidget(stopBtn);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(closeBtn);
    lay->addLayout(bottom);
}

void GameLogDialog::setGameDir(const QString &dir)
{
    m_gameDir = dir;
}

QString GameLogDialog::fullText() const
{
    return m_view ? m_view->toPlainText() : QString();
}

int GameLogDialog::severityOf(const QString &line)
{
    if (line.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("Exception"), Qt::CaseSensitive)
        || line.contains(QStringLiteral("FATAL"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("Crash"), Qt::CaseSensitive)) {
        return 2;
    }
    if (line.contains(QStringLiteral("WARN"), Qt::CaseInsensitive)) {
        return 1;
    }
    return 0;
}

void GameLogDialog::attach(QProcess *proc)
{
    m_proc = proc;
    if (!proc) {
        return;
    }
    connect(proc, &QProcess::readyReadStandardOutput, this, &GameLogDialog::appendStdout);
    connect(proc, &QProcess::readyReadStandardError, this, &GameLogDialog::appendStderr);
    connect(proc, &QProcess::finished, this, &GameLogDialog::onFinished);
    appendStdout();
    appendStderr();
    m_status->setText(tr("Running… grab a seat by the fire."));
}

void GameLogDialog::appendText(const QString &chunk)
{
    const QString clean = Logger::redacted(chunk);
    const QString f = m_search->text();
    const int minSev = m_level ? m_level->currentData().toInt() : 0;
    for (const auto &line : clean.split(QLatin1Char('\n'))) {
        if (line.isEmpty()) {
            continue;
        }
        const int sev = severityOf(line);
        if (sev == 2) {
            ++m_errors;
        } else if (sev == 1) {
            ++m_warnings;
        }
        if (sev < minSev) {
            continue;
        }
        if (!f.isEmpty() && !line.contains(f, Qt::CaseInsensitive)) {
            continue;
        }
        m_view->appendPlainText(line);
    }
    m_counts->setText((m_errors || m_warnings)
                          ? tr("%1 error(s), %2 warning(s)").arg(m_errors).arg(m_warnings)
                          : QString());
    if (m_auto) {
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    }
}

void GameLogDialog::appendStdout()
{
    if (m_proc) {
        appendText(QString::fromLocal8Bit(m_proc->readAllStandardOutput()));
    }
}

void GameLogDialog::appendStderr()
{
    if (m_proc) {
        appendText(QString::fromLocal8Bit(m_proc->readAllStandardError()));
    }
}

void GameLogDialog::onFinished(int code)
{
    appendStdout();
    appendStderr();
    if (code == 0) {
        m_status->setText(tr("Game exited normally (code 0)."));
    } else {
        m_status->setText(tr("Game exited with code %1 — see the message for what the Crash Doctor thinks.").arg(code));
        m_status->setStyleSheet(QStringLiteral("color: #EF6461;"));
    }
}

void GameLogDialog::onStop()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(5000)) {
            m_proc->kill();
        }
    }
}

void GameLogDialog::detach()
{
    m_proc = nullptr;
}

void GameLogDialog::onCopy()
{
    QApplication::clipboard()->setText(m_view->toPlainText());
    m_status->setText(tr("Copied the log to the clipboard."));
}

QString systemInfoBlock(const QString &version)
{
    return QStringLiteral("Hearthlight %1 | %2 | %3 | Qt %4 | %5")
        .arg(QString::fromLatin1(Hearthlight::kAppVersion), QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()), version);
}

void GameLogDialog::onSave()
{
    const QString p = QFileDialog::getSaveFileName(this, tr("Save log"), QStringLiteral("hearthlight-log.txt"),
                                                   tr("Text files (*.txt *.log)"));
    if (p.isEmpty()) {
        return;
    }
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Couldn't save"), tr("Couldn't write to that file."));
        return;
    }
    f.write((systemInfoBlock(m_version) + QStringLiteral("\n\n") + m_view->toPlainText()).toUtf8());
    m_status->setText(tr("Saved to %1.").arg(p));
}

void GameLogDialog::onCopySysInfo()
{
    QApplication::clipboard()->setText(systemInfoBlock(m_version));
    m_status->setText(tr("Copied system info — paste it into your bug report."));
}

void GameLogDialog::onShare()
{
    if (m_shareReply) {
        return; // upload already in flight
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        QMessageBox::information(this, tr("Offline"),
                                 tr("Sharing needs internet. Save the log instead — it works offline."));
        return;
    }
    const QString body = Logger::redacted(m_view->toPlainText()).trimmed();
    if (body.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to share"), tr("The log is empty so far."));
        return;
    }
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    QNetworkRequest req(QUrl(QStringLiteral("https://api.mclo.gs/1/log")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, Hearthlight::userAgent());
    const QByteArray payload = "content=" + QUrl::toPercentEncoding(body.left(1024 * 1024));
    m_shareReply = m_net->post(req, payload);
    connect(m_shareReply, &QNetworkReply::finished, this, &GameLogDialog::onShareFinished);
    m_shareBtn->setEnabled(false);
    m_status->setText(tr("Uploading a redacted copy to mclo.gs…"));
}

void GameLogDialog::onShareFinished()
{
    QNetworkReply *reply = m_shareReply;
    m_shareReply = nullptr;
    if (m_shareBtn) {
        m_shareBtn->setEnabled(true);
    }
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_status->setText(tr("Share failed (%1) — Save works offline.").arg(reply->errorString()));
        return;
    }
    QJsonParseError e{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &e);
    const QJsonObject o = doc.isObject() ? doc.object() : QJsonObject{};
    const bool ok = o.value(QStringLiteral("success")).toBool(false);
    const QString url = o.value(QStringLiteral("url")).toString();
    if (!ok || url.isEmpty()) {
        m_status->setText(tr("mclo.gs didn't accept that log — Save works instead."));
        return;
    }
    QApplication::clipboard()->setText(url);
    m_status->setText(tr("Shared! Link copied: %1").arg(url));
    auto rc = QMessageBox::question(this, tr("Log shared"), tr("Link copied:\n%1\n\nOpen it in the browser?").arg(url));
    if (rc == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void GameLogDialog::applyFilter()
{
    // Filters apply to newly arriving lines; the buffer stays intact.
    // (Simple, predictable, and honest about what it does.)
}

void GameLogDialog::onNextError()
{
    // Search forward from the cursor for the next warning/error line.
    QTextCursor cur = m_view->textCursor();
    const int from = cur.blockNumber() + 1;
    const int blocks = m_view->blockCount();
    for (int i = from; i < blocks; ++i) {
        const QString text = m_view->document()->findBlockByNumber(i).text();
        if (severityOf(text) > 0
            && (m_search->text().isEmpty() || text.contains(m_search->text(), Qt::CaseInsensitive))) {
            QTextCursor jump(m_view->document()->findBlockByNumber(i));
            m_view->setTextCursor(jump);
            m_view->ensureCursorVisible();
            return;
        }
    }
    // Wrap around once from the top.
    for (int i = 0; i < from && i < blocks; ++i) {
        const QString text = m_view->document()->findBlockByNumber(i).text();
        if (severityOf(text) > 0
            && (m_search->text().isEmpty() || text.contains(m_search->text(), Qt::CaseInsensitive))) {
            QTextCursor jump(m_view->document()->findBlockByNumber(i));
            m_view->setTextCursor(jump);
            m_view->ensureCursorVisible();
            return;
        }
    }
    m_status->setText(tr("No warnings or errors in the log — smooth sailing."));
}

#include "GameLogDialog.moc"
