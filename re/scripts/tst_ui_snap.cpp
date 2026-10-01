// Temporary: offscreen pictures of the windows, to compare two builds. Copied into tests/ only for the comparison.
#include "ui/MainWindow.h"
#include "ui/TextView.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>

class TstUiSnap : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;

private slots:
    void snap()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        const QString out = qEnvironmentVariable("TS_SNAP_OUT");
        QVERIFY(!out.isEmpty());
        QDir().mkpath(out);
        QStringList files;
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")}))
            files << QStringLiteral(TS_GOLDEN_DIR "/") + f;
        files << qEnvironmentVariable("TS_SNAP_EXTRA").split(u';', Qt::SkipEmptyParts);
        int n = 0;
        for (const QString &path : files) {
            MainWindow w;
            w.resize(876, 579);
            w.show();
            QVERIFY(w.openFile(path));
            QApplication::processEvents();
            const QString base = QDir(out).filePath(QString::number(n++) + QLatin1Char('_'));
            auto save = [&](const QString &name) {
                QApplication::processEvents();
                for (QWidget *top : QApplication::topLevelWidgets())
                    if (top->isVisible())
                        QVERIFY(top->grab().save(base + name + QLatin1Char('_') + top->metaObject()->className() + QStringLiteral(".png")));
            };
            save(QStringLiteral("open"));
            TextView *text = w.findChild<TextView *>();
            QTextCursor c = text->textCursor();
            const int len = text->document()->characterCount() - 1;
            c.setPosition(len / 3);
            c.setPosition(len / 3 + 60, QTextCursor::KeepAnchor);
            text->setTextCursor(c);
            save(QStringLiteral("sel"));
            w.showForm(QStringLiteral("extra"));
            save(QStringLiteral("extra"));
            w.showForm(QStringLiteral("hist"));
            save(QStringLiteral("hist"));
            w.showForm(QStringLiteral("hist-fingers"));
            save(QStringLiteral("histfingers"));
            c.setPosition(len / 2);
            text->setTextCursor(c);
            save(QStringLiteral("click"));
            w.showForm(QStringLiteral("hist-extra"));
            save(QStringLiteral("histextra"));
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top != &w)
                    top->hide();
        }
    }
};

QTEST_MAIN(TstUiSnap)
#include "tst_ui_snap.moc"
