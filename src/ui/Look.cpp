#include "Look.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

namespace Look {

namespace {

bool g_dark = false;

const QString kKey = QStringLiteral("DarkTheme");

// The original's colours.
const Colors kLight = {
    QColor(255, 255, 220), QColor(255, 255, 180), QColor(240, 240, 32),  QColor(175, 175, 175), QColor(0, 0, 0),
    QColor(0, 0, 0),       QColor(0, 0, 0),
    QColor(128, 128, 128), QColor(180, 255, 180), QColor(255, 230, 230), QColor(255, 255, 200), QColor(180, 240, 180),
    QColor(255, 0, 0),     QColor(0, 0, 255),     QColor(0, 128, 0),     QColor(255, 255, 0),
    QColor(192, 220, 192), QColor(255, 0, 0), // clMoneyGreen, red
    QColor(192, 192, 192), QColor(255, 255, 255),
    {QColor(255, 150, 150), QColor(255, 0, 0), QColor(160, 100, 100), QColor(202, 53, 53), QColor(150, 150, 255),
     QColor(0, 0, 255), QColor(128, 0, 0), QColor(0, 128, 0)},
    {{QColor(180, 240, 180), QColor(34, 172, 34)},
     {QColor(32, 209, 247), QColor(6, 149, 179)},
     {QColor(233, 154, 252), QColor(202, 18, 248)},
     {QColor(250, 139, 148), QColor(211, 10, 24)},
     {QColor(254, 180, 100), QColor(224, 118, 1)}},
};

// The same hues on a dark warm background: the dark series and text colours lighter, the key fills darker (their
// labels are light).
const Colors kDark = {
    QColor(36, 36, 28),    QColor(62, 62, 38),    QColor(92, 92, 24),    QColor(88, 88, 88),    QColor(222, 222, 222),
    QColor(16, 16, 12),    QColor(100, 100, 92),
    QColor(150, 150, 150), QColor(40, 92, 40),    QColor(96, 52, 52),    QColor(46, 46, 34),    QColor(56, 120, 56),
    QColor(255, 96, 96),   QColor(110, 150, 255), QColor(100, 200, 100), QColor(96, 96, 16),
    QColor(40, 72, 40),    QColor(255, 110, 110),
    QColor(64, 64, 64),    QColor(40, 40, 40),
    {QColor(255, 150, 150), QColor(255, 80, 80), QColor(205, 145, 145), QColor(235, 95, 95), QColor(150, 150, 255),
     QColor(100, 140, 255), QColor(215, 80, 80), QColor(90, 195, 90)},
    {{QColor(40, 105, 40), QColor(25, 140, 25)},
     {QColor(20, 100, 125), QColor(5, 125, 150)},
     {QColor(105, 55, 125), QColor(140, 20, 175)},
     {QColor(125, 50, 58), QColor(165, 15, 30)},
     {QColor(130, 85, 30), QColor(170, 95, 5)}},
};

// The system colours of Windows (COLOR_BTNFACE, COLOR_3DSHADOW, ...): the light palette outside Windows.
QPalette classicPalette()
{
    QPalette p;
    const QColor face(0xF0, 0xF0, 0xF0), gray(0x6D, 0x6D, 0x6D);
    p.setColor(QPalette::Window, face);
    p.setColor(QPalette::Button, face);
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xF7, 0xF7, 0xF7));
    p.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xE1));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::PlaceholderText, gray);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xE3, 0xE3, 0xE3));
    p.setColor(QPalette::Mid, QColor(0xA0, 0xA0, 0xA0));
    p.setColor(QPalette::Dark, QColor(0xA0, 0xA0, 0xA0));
    p.setColor(QPalette::Shadow, QColor(0x69, 0x69, 0x69));
    p.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xD7));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x00, 0x66, 0xCC));
    p.setColor(QPalette::LinkVisited, QColor(0x80, 0x00, 0x80));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text})
        p.setColor(QPalette::Disabled, role, gray);
    p.setColor(QPalette::Disabled, QPalette::Base, face);
    return p;
}

QPalette darkPalette()
{
    QPalette p;
    const QColor face(0x36, 0x36, 0x36), text(0xDE, 0xDE, 0xDE), gray(0x80, 0x80, 0x80);
    p.setColor(QPalette::Window, face);
    p.setColor(QPalette::Button, QColor(0x42, 0x42, 0x42));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Base, QColor(0x22, 0x22, 0x22));
    p.setColor(QPalette::AlternateBase, QColor(0x2A, 0x2A, 0x2A));
    p.setColor(QPalette::ToolTipBase, QColor(0x3C, 0x3C, 0x30));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, gray);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x55, 0x55, 0x55));
    p.setColor(QPalette::Midlight, QColor(0x48, 0x48, 0x48));
    p.setColor(QPalette::Mid, QColor(0x28, 0x28, 0x28));
    p.setColor(QPalette::Dark, QColor(0x1E, 0x1E, 0x1E));
    p.setColor(QPalette::Shadow, QColor(0x0F, 0x0F, 0x0F));
    p.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xD7));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x66, 0xA8, 0xFF));
    p.setColor(QPalette::LinkVisited, QColor(0xC0, 0x8C, 0xFF));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text})
        p.setColor(QPalette::Disabled, role, gray);
    p.setColor(QPalette::Disabled, QPalette::Base, face);
    return p;
}

void applyTheme(bool dark)
{
    g_dark = dark;
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#ifdef Q_OS_WIN
    if (!dark) {
        QApplication::setStyle(QStringLiteral("windowsvista"));
        QApplication::setPalette(QPalette()); // the system's
        return;
    }
#endif
    if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(dark ? darkPalette() : classicPalette());
}

} // namespace

void applySavedTheme()
{
    QSettings settings;
    if (!settings.contains(kKey)) {
        // The first start takes the theme of the system, and from then on the saved one counts.
        QGuiApplication::styleHints()->unsetColorScheme();
        settings.setValue(kKey, QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    }
    applyTheme(settings.value(kKey).toBool());
}

void apply()
{
    applySavedTheme();
#ifdef Q_OS_WIN
    QApplication::setFont(QFont(QStringLiteral("Microsoft Sans Serif"), 8));
#else
    // MS Sans Serif where there is one (macOS has it), else a font of the same width.
    const QStringList families = QFontDatabase::families();
    QString family = QStringLiteral("Sans Serif");
    for (const char *name : {"Microsoft Sans Serif", "Liberation Sans", "Arial", "Helvetica", "DejaVu Sans"})
        if (families.contains(QLatin1String(name), Qt::CaseInsensitive)) {
            family = QLatin1String(name);
            break;
        }
    QApplication::setFont(pointFont(family, 8));
#endif
}

void setDark(bool dark, bool save)
{
    if (save)
        QSettings().setValue(kKey, dark);
    if (dark != g_dark)
        applyTheme(dark);
}

bool isDark()
{
    return g_dark;
}

const Colors &colors()
{
    return g_dark ? kDark : kLight;
}

int pointsToPixels(int points)
{
    return qRound(points * 96.0 / 72.0);
}

QFont pointFont(const QString &family, int points)
{
    QFont f(family);
    f.setPixelSize(pointsToPixels(points));
    return f;
}

}
