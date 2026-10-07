#include "Markdown.h"

#include <QRegularExpression>

namespace Markdown {

static QString escaped(const QString &s)
{
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    o.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    o.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return o;
}

// Inline spans on already-escaped text: code, bold, italic, images, links.
// Code spans are extracted to placeholders first so emphasis never touches
// their contents (e.g. `*x*` stays literal).
static QString inlineSpans(QString s)
{
    QList<QString> codeKept;
    {
        QRegularExpression codeRe(QStringLiteral("`([^`\\n]+)`"));
        auto it = codeRe.globalMatch(s);
        QString out;
        int pos = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.mid(pos, m.capturedStart() - pos);
            codeKept.append(QStringLiteral("<code>") + m.captured(1) + QStringLiteral("</code>"));
            out += QStringLiteral("\x01%1\x02").arg(codeKept.size() - 1);
            pos = m.capturedEnd();
        }
        out += s.mid(pos);
        s = out;
    }
    // ![alt](url) -> link (no remote images in the text browser).
    s.replace(QRegularExpression(QStringLiteral("!\\[([^\\]]*)\\]\\(([^\\)\\s]+)(?:\\s+\"[^\"]*\")?\\)")),
              QStringLiteral("<a href=\"\\2\">[image: \\1]</a>"));
    // [text](url)
    s.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\(([^\\)\\s]+)(?:\\s+\"[^\"]*\")?\\)")),
              QStringLiteral("<a href=\"\\2\">\\1</a>"));
    // **bold** (non-greedy, allows single * inside).
    s.replace(QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*")), QStringLiteral("<b>\\1</b>"));
    // *italic* (avoid list bullets: only inside a line, handled per-line).
    s.replace(QRegularExpression(QStringLiteral("(^|[^\\*\\w])\\*([^\\*\\n]+)\\*")),
              QStringLiteral("\\1<i>\\2</i>"));
    // Restore the protected code spans.
    for (int i = 0; i < codeKept.size(); ++i) {
        s.replace(QStringLiteral("\x01%1\x02").arg(i), codeKept.at(i));
    }
    return s;
}

QString toHtml(const QString &markdown)
{
    // Normalize: CRLF -> LF, tabs -> 4 spaces.
    QString src = markdown;
    src.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    src.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    src.replace(QLatin1Char('\t'), QStringLiteral("    "));

    QString html;
    bool inCode = false;
    QString codeBuf;
    bool inUl = false;
    bool inOl = false;
    QString para;

    auto closeLists = [&] {
        if (inUl) {
            html += QStringLiteral("</ul>\n");
            inUl = false;
        }
        if (inOl) {
            html += QStringLiteral("</ol>\n");
            inOl = false;
        }
    };
    auto flushPara = [&] {
        if (!para.isEmpty()) {
            html += QStringLiteral("<p>") + inlineSpans(escaped(para)) + QStringLiteral("</p>\n");
            para.clear();
        }
    };

    const QStringList lines = src.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        const QString trim = line.trimmed();

        if (trim.startsWith(QStringLiteral("```"))) {
            if (inCode) {
                html += QStringLiteral("<pre><code>") + escaped(codeBuf) + QStringLiteral("</code></pre>\n");
                codeBuf.clear();
                inCode = false;
            } else {
                flushPara();
                closeLists();
                inCode = true;
            }
            continue;
        }
        if (inCode) {
            codeBuf += line + QLatin1Char('\n');
            continue;
        }
        if (trim.isEmpty()) {
            flushPara();
            closeLists();
            continue;
        }
        // Headings.
        int hashes = 0;
        while (hashes < (int)trim.size() && trim.at(hashes) == QLatin1Char('#') && hashes < 6) {
            ++hashes;
        }
        if (hashes > 0 && hashes < (int)trim.size() && trim.at(hashes).isSpace()) {
            flushPara();
            closeLists();
            const QString text = trim.mid(hashes + 1).trimmed();
            html += QStringLiteral("<h%1>%2</h%1>\n").arg(hashes).arg(inlineSpans(escaped(text)));
            continue;
        }
        // Horizontal rule.
        if (trim == QStringLiteral("---") || trim == QStringLiteral("***") || trim == QStringLiteral("___")) {
            flushPara();
            closeLists();
            html += QStringLiteral("<hr/>\n");
            continue;
        }
        // Quote.
        if (trim.startsWith(QLatin1Char('>'))) {
            flushPara();
            closeLists();
            html += QStringLiteral("<blockquote>") + inlineSpans(escaped(trim.mid(1).trimmed()))
                + QStringLiteral("</blockquote>\n");
            continue;
        }
        // Unordered list.
        if ((trim.startsWith(QStringLiteral("- ")) || trim.startsWith(QStringLiteral("* ")))
            && trim.size() > 2) {
            flushPara();
            if (inOl) {
                html += QStringLiteral("</ol>\n");
                inOl = false;
            }
            if (!inUl) {
                html += QStringLiteral("<ul>\n");
                inUl = true;
            }
            html += QStringLiteral("<li>") + inlineSpans(escaped(trim.mid(2).trimmed())) + QStringLiteral("</li>\n");
            continue;
        }
        // Ordered list ("1. ...").
        static const QRegularExpression olRe(QStringLiteral("^(\\d+)\\.\\s+(.+)$"));
        const auto olMatch = olRe.match(trim);
        if (olMatch.hasMatch()) {
            flushPara();
            if (inUl) {
                html += QStringLiteral("</ul>\n");
                inUl = false;
            }
            if (!inOl) {
                html += QStringLiteral("<ol>\n");
                inOl = true;
            }
            html += QStringLiteral("<li>") + inlineSpans(escaped(olMatch.captured(2))) + QStringLiteral("</li>\n");
            continue;
        }
        // Plain paragraph line (joined with spaces until a blank line).
        if (!para.isEmpty()) {
            para += QLatin1Char(' ');
        }
        para += trim;
    }
    if (inCode) {
        html += QStringLiteral("<pre><code>") + escaped(codeBuf) + QStringLiteral("</code></pre>\n");
    }
    flushPara();
    closeLists();
    return html;
}

} // namespace Markdown
