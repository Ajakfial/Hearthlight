// Tests: Markdown renderer (offline, pure).
#include "Markdown.h"

#include <QTest>

class TstMarkdown : public QObject {
    Q_OBJECT
private slots:
    void headings();
    void spans();
    void lists();
    void code();
    void links();
    void escaping();
    void quotesAndRules();
};

void TstMarkdown::headings()
{
    QVERIFY(Markdown::toHtml(QStringLiteral("# Title")).contains(QStringLiteral("<h1>Title</h1>")));
    QVERIFY(Markdown::toHtml(QStringLiteral("### Deep")).contains(QStringLiteral("<h3>Deep</h3>")));
    // Not a heading without the space.
    QVERIFY(!Markdown::toHtml(QStringLiteral("#Nope")).contains(QStringLiteral("<h1>")));
}

void TstMarkdown::spans()
{
    const QString h = Markdown::toHtml(QStringLiteral("a **bold** and *em* word"));
    QVERIFY(h.contains(QStringLiteral("<b>bold</b>")));
    QVERIFY(h.contains(QStringLiteral("<i>em</i>")));
    QVERIFY(Markdown::toHtml(QStringLiteral("use `code` here")).contains(QStringLiteral("<code>code</code>")));
}

void TstMarkdown::lists()
{
    const QString h = Markdown::toHtml(QStringLiteral("- one\n- two\n\n1. first\n2. second"));
    QVERIFY(h.contains(QStringLiteral("<ul>")));
    QVERIFY(h.contains(QStringLiteral("<li>one</li>")));
    QVERIFY(h.contains(QStringLiteral("<ol>")));
    QVERIFY(h.contains(QStringLiteral("<li>second</li>")));
}

void TstMarkdown::code()
{
    const QString h = Markdown::toHtml(QStringLiteral("```\n<a>&\n```"));
    QVERIFY(h.contains(QStringLiteral("<pre><code>")));
    QVERIFY(h.contains(QStringLiteral("&lt;a&gt;&amp;")));
    // Code spans protect stars from emphasis.
    QVERIFY(Markdown::toHtml(QStringLiteral("`*x*`")).contains(QStringLiteral("<code>*x*</code>")));
}

void TstMarkdown::links()
{
    const QString h = Markdown::toHtml(QStringLiteral("[text](https://example.com)"));
    QVERIFY(h.contains(QStringLiteral("<a href=\"https://example.com\">text</a>")));
    const QString img = Markdown::toHtml(QStringLiteral("![alt](https://example.com/i.png)"));
    QVERIFY(img.contains(QStringLiteral("https://example.com/i.png")));
    QVERIFY(!img.contains(QStringLiteral("<img")));
}

void TstMarkdown::escaping()
{
    const QString h = Markdown::toHtml(QStringLiteral("<script>alert(1)</script>"));
    QVERIFY(!h.contains(QStringLiteral("<script>")));
    QVERIFY(h.contains(QStringLiteral("&lt;script&gt;")));
}

void TstMarkdown::quotesAndRules()
{
    QVERIFY(Markdown::toHtml(QStringLiteral("> wise")).contains(QStringLiteral("<blockquote>wise</blockquote>")));
    QVERIFY(Markdown::toHtml(QStringLiteral("---")).contains(QStringLiteral("<hr")));
}

QTEST_MAIN(TstMarkdown)
#include "tst_Markdown.moc"
