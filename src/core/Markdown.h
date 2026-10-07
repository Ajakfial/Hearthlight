#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Minimal Markdown → HTML renderer (Qt Widgets only, no web runtime).
// Supports: headings (#–######), bold (**), italic (*), inline `code`,
// ``` fenced blocks, links [t](u), images ![a](u) (rendered as links),
// unordered (-, *) and ordered (1.) lists, > quotes, --- rules,
// paragraphs. Everything HTML-escaped first. Output targets QTextBrowser
// (Qt rich-text subset).
namespace Markdown {
QString toHtml(const QString &markdown);
} // namespace Markdown
