#include "LogViewerDialog.h"

#include "Logger.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

LogViewerDialog::LogViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Hearthlight log"));
    resize(760, 520);

    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Search:"), this));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("filter…"));
    top->addWidget(m_search, 1);
    m_auto = new QCheckBox(tr("Auto-scroll"), this);
    m_auto->setChecked(true);
    top->addWidget(m_auto);
    auto *copyBtn = new QPushButton(tr("Copy"), this);
    connect(copyBtn, &QPushButton::clicked, this, &LogViewerDialog::onCopy);
    top->addWidget(copyBtn);
    lay->addLayout(top);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas, Menlo, monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_view->setFont(mono);
    lay->addWidget(m_view, 1);

    connect(m_search, &QLineEdit::textChanged, this, &LogViewerDialog::refresh);
    connect(&Logger::instance(), &Logger::lineAppended, this, [this](const QString &line) {
        if (m_auto->isChecked()) {
            const QString f = m_search->text();
            if (f.isEmpty() || line.contains(f, Qt::CaseInsensitive)) {
                m_view->appendPlainText(line);
                m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
            }
        }
    });
    refresh();
}

void LogViewerDialog::refresh()
{
    const QString f = m_search->text();
    QStringList out;
    for (const auto &line : Logger::recentLines(2000)) {
        if (f.isEmpty() || line.contains(f, Qt::CaseInsensitive)) {
            out.append(line);
        }
    }
    m_view->setPlainText(out.join(QLatin1Char('\n')));
    if (m_auto->isChecked()) {
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    }
}

void LogViewerDialog::onCopy()
{
    QApplication::clipboard()->setText(m_view->toPlainText());
}
