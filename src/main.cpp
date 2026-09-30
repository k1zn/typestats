#include "ui/MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TypingStatisticsQt"));
    QApplication::setApplicationName(QStringLiteral("TypingStatistics"));
    QApplication::setApplicationVersion(QStringLiteral(TS_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    QLocale::setDefault(QLocale(QLocale::Russian)); // numbers as the original shows them, until i18n

    // The look of the original: a classic light window with compact native controls and the
    // 8 pt dialog font its layout was made for.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#ifdef Q_OS_WIN
    QApplication::setStyle(QStringLiteral("windowsvista"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft Sans Serif"), 8));
#endif

    MainWindow w;
    w.show();
    const QStringList args = app.arguments();
    if (args.size() > 1)
        w.openFile(args[1]);
    return app.exec();
}
