// Tests: every spec-required icon exists in resources AND as a conforming
// hand-written SVG on disk (24x24 viewBox, currentColor, 1.75px strokes).
#include "IconProvider.h"

#include <QDir>
#include <QFile>
#include <QTest>

#ifndef HEARTHLIGHT_SOURCE_DIR
#define HEARTHLIGHT_SOURCE_DIR "."
#endif

class TstIcons : public QObject {
    Q_OBJECT
private slots:
    void namesCovered();
    void resourcesExist();
    void svgContract();
};

static QStringList requiredIcons()
{
    // Spec section 12 required set (plus a few Hearthlight extras).
    return {
        QStringLiteral("home"),     QStringLiteral("play"),     QStringLiteral("stop"),
        QStringLiteral("profile"),  QStringLiteral("plus"),     QStringLiteral("search"),
        QStringLiteral("download"), QStringLiteral("update"),   QStringLiteral("settings"),
        QStringLiteral("account"),  QStringLiteral("offline"),  QStringLiteral("mod"),
        QStringLiteral("resource"), QStringLiteral("shader"),   QStringLiteral("world"),
        QStringLiteral("folder"),   QStringLiteral("trash"),    QStringLiteral("copy"),
        QStringLiteral("export"),   QStringLiteral("import"),   QStringLiteral("clone"),
        QStringLiteral("edit"),     QStringLiteral("toggle"),   QStringLiteral("check"),
        QStringLiteral("info"),     QStringLiteral("warning"),  QStringLiteral("error"),
        QStringLiteral("undo"),     QStringLiteral("snapshot"), QStringLiteral("log"),
        QStringLiteral("crash"),    QStringLiteral("loader"),   QStringLiteral("logo"),
    };
}

void TstIcons::namesCovered()
{
    const QStringList have = IconProvider::allNames();
    for (const auto &name : requiredIcons()) {
        QVERIFY2(have.contains(name), qPrintable(name));
    }
}

void TstIcons::resourcesExist()
{
    for (const auto &name : requiredIcons()) {
        QVERIFY2(IconProvider::instance().hasIcon(name), qPrintable(name));
    }
}

void TstIcons::svgContract()
{
    const QDir icons(QStringLiteral(HEARTHLIGHT_SOURCE_DIR) + QStringLiteral("/assets/icons"));
    QVERIFY2(icons.exists(), qPrintable(icons.absolutePath()));
    for (const auto &name : IconProvider::allNames()) {
        QFile f(icons.filePath(name + QStringLiteral(".svg")));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
        const QString svg = QString::fromUtf8(f.readAll());
        QVERIFY2(svg.contains(QStringLiteral("viewBox=\"0 0 24 24\"")), qPrintable(name));
        QVERIFY2(svg.contains(QStringLiteral("currentColor")), qPrintable(name));
        QVERIFY2(svg.contains(QStringLiteral("1.75")), qPrintable(name));
        QVERIFY2(!svg.contains(QStringLiteral("Mojang"), Qt::CaseInsensitive), qPrintable(name));
    }
}

QTEST_MAIN(TstIcons)
#include "tst_Icons.moc"
