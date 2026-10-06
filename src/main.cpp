#include "ui/Look.h"
#include "ui/MainWindow.h"
#include "ui/Presets.h"
#include "ui/Texts.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TypingStatisticsQt"));
    QApplication::setApplicationName(QStringLiteral("TypingStatistics"));
    QApplication::setApplicationVersion(QStringLiteral(TS_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    // Wayland finds the window's icon and name by the menu entry (resources/linux/*.desktop).
    QGuiApplication::setDesktopFileName(QStringLiteral("org.typingstatistics.TypingStatistics"));

    // The source texts are Russian, as in the original; "Language" (its preset key) picks a translation.
    // Without the key the system language decides. `--lang ru|en` overrides both.
    QStringList args = app.arguments();
    Presets::importFromOriginal(); // the first start takes over the original's settings
    QString language = Texts::currentLanguage();
    if (const qsizetype i = args.indexOf(QStringLiteral("--lang")); i > 0 && i + 1 < args.size()) {
        language = args[i + 1] == QLatin1String("ru") ? QStringLiteral("Russian") : QStringLiteral("English");
        args.remove(i, 2);
    }
    QTranslator translator;
    if (language != QLatin1String("Russian") && translator.load(QStringLiteral(":/i18n/typestats_en.qm")))
        app.installTranslator(&translator);

    // The look of the original: a classic light window with compact controls and the 8 pt dialog font
    // its layout was made for.
    Look::apply();

    MainWindow w;
    w.show();
    if (QSettings().value(QStringLiteral("AutoMinimize"), false).toBool())
        w.showMinimized();
    // `--no-capture`: no keyboard hook (screenshots and demonstrations: the typing of whoever sits at the
    // computer stays out of the window).
    if (const qsizetype i = args.indexOf(QStringLiteral("--no-capture")); i > 0)
        args.remove(i);
    else
        w.startCapture();
    QString form;
    if (const qsizetype i = args.indexOf(QStringLiteral("--show")); i > 0 && i + 1 < args.size()) {
        form = args[i + 1];
        args.remove(i, 2);
    }
    if (args.size() > 1)
        w.openFile(args[1]);
    if (!form.isEmpty())
        QTimer::singleShot(0, &w, [&w, form] { w.showForm(form); });
    return app.exec();
}
