#include "ui/MainWindow.h"
#include "ui/Texts.h"

#include <QApplication>
#include <QIcon>
#include <QStyleHints>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TypingStatisticsQt"));
    QApplication::setApplicationName(QStringLiteral("TypingStatistics"));
    QApplication::setApplicationVersion(QStringLiteral(TS_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // The source texts are Russian, as in the original; "Language" (its preset key) picks a translation.
    // Without the key the system language decides. `--lang ru|en` overrides both.
    QStringList args = app.arguments();
    QString language = Texts::currentLanguage();
    if (const qsizetype i = args.indexOf(QStringLiteral("--lang")); i > 0 && i + 1 < args.size()) {
        language = args[i + 1] == QLatin1String("ru") ? QStringLiteral("Russian") : QStringLiteral("English");
        args.remove(i, 2);
    }
    QTranslator translator;
    if (language != QLatin1String("Russian") && translator.load(QStringLiteral(":/i18n/typestats_en.qm")))
        app.installTranslator(&translator);

    // The look of the original: a classic light window with compact native controls and the
    // 8 pt dialog font its layout was made for.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#ifdef Q_OS_WIN
    QApplication::setStyle(QStringLiteral("windowsvista"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft Sans Serif"), 8));
#endif

    MainWindow w;
    w.show();
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
