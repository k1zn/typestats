// The main window driven without a screen (QT_QPA_PLATFORM=offscreen, set by ctest): opening a
// recording, editing, copying, the graph and its link with the klavogram.

#include "core/Editing.h"
#include "ui/GraphPanels.h"
#include "ui/GraphWidget.h"
#include "ui/KlavogramWidget.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TextView.h"

#include <QApplication>
#include <QClipboard>
#include <QListWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QComboBox>
#include <QToolButton>

class TstUi : public QObject
{
    Q_OBJECT

    static QString golden(const char *name) { return QStringLiteral(TS_GOLDEN_DIR "/") + QString::fromUtf8(name); }

    static void select(MainWindow &w, int start, int length)
    {
        QTextCursor c = w.m_text->textCursor();
        c.setPosition(start);
        c.setPosition(start + length, QTextCursor::KeepAnchor);
        w.m_text->setTextCursor(c);
    }

private slots:
    void initTestCase()
    {
        // Settings of the test must not touch the user's.
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QCoreApplication::setOrganizationName(QStringLiteral("TypingStatisticsTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_ui"));
    }

    void opensAndPaints()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        QCOMPARE(w.m_model.size(), 269);
        QCOMPARE(w.m_stats->rowCount(), 17);
        QCOMPARE(w.m_stats->item(0, 1)->text(), QStringLiteral("269 (269)"));
        QVERIFY(w.m_keys->rowCount() > 5);
        QVERIFY(!w.grab().isNull()); // every widget paints itself
        QVERIFY(w.m_legend->isVisible());
    }

    void deleteUndoCopy()
    {
        MainWindow w;
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        const QString text = w.m_model.text;
        QVERIFY(!w.m_deleteButton->isEnabled());

        select(w, 9, 5); // "этому"
        QVERIFY(w.m_deleteButton->isEnabled());
        w.copy(0);
        QCOMPARE(QApplication::clipboard()->text(), text.mid(9, 5));
        w.copy(2); // nothing is erased in this recording: no tags
        QCOMPARE(QApplication::clipboard()->text(), text.mid(9, 5));

        w.deleteSelection();
        QCOMPARE(w.m_model.text, text.left(9) + text.mid(14));
        QCOMPARE(w.m_model.size(), 264);
        w.undo();
        QCOMPARE(w.m_model.text, text);
        w.undo(); // and back again
        QCOMPARE(w.m_model.size(), 264);
    }

    void labels()
    {
        MainWindow w;
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        w.normalizeRecords();
        const int from = w.m_model.recordAt(9), to = w.m_model.recordAt(14);
        const int label = Editing::markRange(w.m_doc.records, from, to);
        QVERIFY(label > 0);
        w.m_doc.records[label].comment = QStringLiteral("label");
        w.recalculate();
        // The marked text is underlined and the label is found from any of its records.
        w.textHovered(11, QPoint());
        QCOMPARE(w.m_labelRecord, label);
        w.removeLabel(label);
        w.textHovered(11, QPoint());
        QCOMPARE(w.m_labelRecord, -1);
    }

    void graphFollowsKlavogram()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("824.tsf")));
        GraphWidget *g = w.m_graph;
        const GraphWidget::ScrollParams before = g->scrollParams();
        QVERIFY(before.max >= before.min);
        QCOMPARE(w.m_graphScroll->value(), before.value);

        // The klavogram scrolled far to the right: the graph brings the visible part into sight.
        w.m_klav->setScrollMs(15000.0f);
        w.klavogramMoved();
        QVERIFY(g->klavogramFrom() > 100);
        QVERIFY(g->scrollParams().value > before.value);

        // The scroll bar of the graph moved back: the klavogram follows.
        const auto span = w.m_klav->visibleSpanUs();
        w.m_graphScroll->setValue(before.value);
        QVERIFY(w.m_klav->visibleSpanUs().first < span.first);

        // Dragging with the left button scrolls, the legend toggles a series.
        const QPoint p(g->width() / 2, g->height() / 3);
        const int value = g->scrollParams().value;
        QTest::mousePress(g, Qt::LeftButton, {}, p);
        QTest::mouseMove(g, p - QPoint(200, 0));
        QTest::mouseRelease(g, Qt::LeftButton, {}, p - QPoint(200, 0));
        QVERIFY(g->scrollParams().value > value);
        QVERIFY(!g->seriesVisible(GraphWidget::CurSpeed));
        g->setSeriesVisible(GraphWidget::Pause, true);
        QVERIFY(!g->seriesVisible(GraphWidget::Arrhythmia));
        QVERIFY(!w.grab().isNull());
    }

    void recordsKeys()
    {
        MainWindow w;
        // The window is not shown, let alone active: the keys count as typed elsewhere.
        auto key = [&w](qint64 ms, quint8 vk, bool down, char16_t ch) {
            HookEvent e;
            e.timeUs = ms * 1000;
            e.flags = quint32(vk) << 16 | (vk & 0x7f) | (down ? (ch ? KeyRecord::HasChar : KeyRecord::NoChar)
                                                               : KeyRecord::KeyUp | KeyRecord::NoChar);
            e.ch = ch;
            e.chars = ch ? 1 : 0;
            w.keyEvent(e);
        };
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        key(0, 'A', true, u'a');
        key(80, 'A', false, 0);
        key(200, 'B', true, u'b');
        key(290, 'B', false, 0);
        QCOMPARE(w.m_doc.records.size(), 4);
        QVERIFY(w.m_needRecalc);
        w.recalculate();
        QCOMPARE(w.m_model.text, QStringLiteral("ab"));
    }

    void settings()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        const QString duration = w.m_keys->item(0, 1)->text();
        QVERIFY(duration.contains(QLocale().decimalPoint()));
        {
            SettingsDialog d(&w);
            QCOMPARE(d.m_textFont->value(), 12);
            QCOMPARE(d.m_mainStats->count(), 17);
            QVERIFY(!d.m_journal->isChecked());
            QVERIFY(!d.grab().isNull());
            d.m_textFont->setValue(20);
            d.m_klavFont->setValue(14);
            d.m_digits->setValue(0);
            d.m_mainStats->item(0)->setCheckState(Qt::Unchecked);
            d.m_mainStats->item(16)->setCheckState(Qt::Unchecked);
            d.m_hiSpeed->setText(QStringLiteral("700"));
            d.m_journal->setChecked(true);
            QVERIFY(!d.languageChanged());
            d.save();
        }
        const int listHeight = w.m_stats->height();
        w.applySettings();
        QCOMPARE(w.m_text->font().pixelSize(), 27);
        QCOMPARE(w.m_stats->rowCount(), 15);
        QCOMPARE(w.m_stats->item(0, 0)->text(), QStringLiteral("Общее время"));
        QVERIFY(w.m_stats->height() < listHeight);
        QVERIFY(!w.m_keys->item(0, 1)->text().contains(QLocale().decimalPoint()));
        QVERIFY(QSettings().value(QStringLiteral("JournalOn")).toBool());
        QCOMPARE(QSettings().value(QStringLiteral("opHiSpeed")).toInt(), 700);
        {
            SettingsDialog d(&w); // shows what was stored
            QCOMPARE(d.m_digits->value(), 0);
            QCOMPARE(d.m_mainStats->item(16)->checkState(), Qt::Unchecked);
            d.m_language->setCurrentIndex(1 - d.m_language->currentIndex());
            QVERIFY(d.languageChanged());
        }
        QSettings().clear(); // the other tests run with the defaults
    }

private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
