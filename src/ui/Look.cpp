#include "Look.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleHints>

namespace Look {

void apply()
{
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#ifdef Q_OS_WIN
    QApplication::setStyle(QStringLiteral("windowsvista"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft Sans Serif"), 8));
#else
    QApplication::setStyle(QStringLiteral("Fusion"));
    // The system colours of Windows (COLOR_BTNFACE, COLOR_3DSHADOW, ...).
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
    QApplication::setPalette(p);
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
