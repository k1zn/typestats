// The main window driven without a screen (QT_QPA_PLATFORM=offscreen, set by ctest): opening a
// recording, editing, copying, the graph and its link with the klavogram.

#include "core/Editing.h"
#include "ui/ExtraStatsWindow.h"
#include "ui/FingerZonesDialog.h"
#include "ui/GraphPanels.h"
#include "ui/GraphWidget.h"
#include "ui/HistogramWindow.h"
#include "ui/KlavogramWidget.h"
#include "ui/MainWindow.h"
#include "ui/Presets.h"
#include "ui/TextInputWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TextView.h"

#include <QApplication>
#include <QClipboard>
#include <QListWidget>
#include <QRadioButton>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
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

    void extraStats()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        ExtraStatsWindow *x = w.m_extra;
        QVERIFY(x->rows().isEmpty()); // nothing is computed while hidden
        w.showExtraStats();
        QVERIFY(x->isVisible());
        const int words = int(x->rows().size());
        QVERIFY(words > 10);
        QCOMPARE(x->m_status->text(), QStringLiteral("Всего: %1").arg(words));
        QCOMPARE(x->m_list->model()->columnCount(), 2);
        QCOMPARE(x->m_list->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("▲Скорость"));
        QVERIFY(x->rows().first().speed <= x->rows().last().speed);
        QVERIFY(!x->grab().isNull());

        // A row takes the klavogram to its occurrence.
        const int element = x->rows().at(3).value;
        x->selectRow(3);
        QCOMPARE(w.m_graph->klavogramFrom(), element);

        // A click on the header reverses the order.
        x->m_sort.clickColumn(0, false);
        x->showRows();
        QVERIFY(x->rows().first().speed >= x->rows().last().speed);
        QCOMPARE(x->m_list->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("▼Скорость"));

        // The selection of the main window narrows the statistics.
        select(w, 0, 40);
        QVERIFY(x->rows().size() < words);
        select(w, 0, 0);
        QCOMPARE(int(x->rows().size()), words);

        // Locked: the source may change, the list stays; a kind is computed at once.
        x->m_lock->setChecked(true);
        select(w, 0, 40);
        QCOMPARE(int(x->rows().size()), words);
        x->m_kinds[ExtraStats::Pairs]->setChecked(true);
        QVERIFY(x->rows().size() != words);
        x->m_lock->setChecked(false);
        select(w, 0, 0);

        // Averages: a count column and the occurrences of the selected row below.
        x->m_averages->setChecked(true);
        QCOMPARE(x->m_list->model()->columnCount(), 3);
        QVERIFY(x->m_lowerList->isVisible());
        QCOMPARE(int(x->m_lower.size()), x->rows().first().value);
        x->m_averages->setChecked(false);
        QVERIFY(!x->m_lowerList->isVisible());

        // A template and the filter.
        x->m_kinds[ExtraStats::Template]->setChecked(true);
        x->m_template->setEditText(QStringLiteral("/б/б/б"));
        const int triples = int(x->rows().size());
        QVERIFY(triples > 0);
        x->m_filterText[2]->setText(QStringLiteral("о"));
        x->m_filterOn[2]->setChecked(true);
        QVERIFY(x->rows().size() < triples);
        for (const ExtraStats::Row &r : x->rows())
            QVERIFY(!r.text.contains(QLatin1Char(u'о')));

        x->m_list->selectAll();
        x->copy();
        QCOMPARE(QApplication::clipboard()->text().count(QLatin1Char(' ')), int(x->rows().size()) - 1);
    }

    void histograms()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        HistogramWindow *h = w.m_hist;
        w.showHistograms();
        QVERIFY(h->isVisible());
        QCOMPARE(h->m_title->text(), QStringLiteral("Все клавиши"));
        const int keys = int(h->m_page.bars.size());
        QVERIFY(keys > 20);
        QVERIFY(!h->grab().isNull());

        // All the bars fit: the last one ends inside the widget, the first starts at the axis.
        HistogramWidget *c = h->m_chart;
        QCOMPARE(c->barAt(c->m_axisWidth + 1), 0);
        QCOMPARE(c->barAt(c->width() - 2), keys - 1);
        QCOMPARE(c->barAt(3), -1);

        // A double click opens the page of the key, another one its pairs; "back" returns.
        h->drill(0);
        QCOMPARE(h->m_stack.size(), 2);
        QVERIFY(h->m_title->text().startsWith(QStringLiteral("Клавиша")));
        h->drill(0);
        QCOMPARE(h->m_stack.size(), 3);
        QVERIFY(!h->m_page.bars.isEmpty());
        QVERIFY(h->m_page.bars.first().rec >= 0);
        h->drill(0); // a single press: the klavogram goes there
        QCOMPARE(h->m_stack.size(), 3);
        QCOMPARE(w.m_graph->klavogramFrom(), w.m_model.elementOfRecord(h->m_page.bars.first().rec));
        h->back();
        h->back();
        h->back();
        QCOMPARE(h->m_stack.size(), 1);

        h->setRoot(Histograms::Node::AllFingers);
        QCOMPARE(int(h->m_page.bars.size()), 9);
        h->drill(3);
        QCOMPARE(int(h->m_page.bars.size()), 4);

        // A change of the selection returns to the root.
        select(w, 0, 60);
        QCOMPARE(h->m_stack.size(), 1);
        QCOMPARE(int(h->m_page.bars.size()), 9);
        select(w, 0, 0);

        // The page of the extra statistics follows the list of that window.
        h->setRoot(Histograms::Node::Extra);
        QVERIFY(h->m_page.bars.isEmpty());
        w.showExtraStats();
        QCOMPARE(h->m_page.bars.size(), w.m_extra->rows().size());
        QVERIFY(!h->m_page.bars.isEmpty());
        QCOMPARE(h->m_page.bars.first().label, w.m_extra->rows().first().text);
        h->drill(2);
        QCOMPARE(w.m_graph->klavogramFrom(), w.m_extra->rows().at(2).value);

        // The mouse: the left button drags, the right one zooms.
        const float zoom = c->m_zoomX;
        QTest::mousePress(c, Qt::RightButton, {}, QPoint(200, 100));
        QTest::mouseMove(c, QPoint(250, 100));
        QTest::mouseRelease(c, Qt::RightButton, {}, QPoint(250, 100));
        QVERIFY(c->m_zoomX > zoom);
        QVERIFY(!h->grab().isNull());
    }

    void fingerZonesEditor()
    {
        FingerZones zones = FingerZones::standard();
        {
            FingerZonesDialog d(QStringLiteral("Стандарт"), zones);
            QVERIFY(!d.grab().isNull());
            QWidget *keyboard = d.findChildren<QWidget *>().at(0);
            // The built-in layout is read-only: a click changes nothing.
            QTest::mouseClick(keyboard, Qt::LeftButton, {}, QPoint(30, 30));
            QVERIFY(d.zones() == zones);
        }
        zones.setReadOnly(false);
        FingerZonesDialog d(QStringLiteral("Моя"), zones);
        QWidget *keyboard = d.findChildren<QWidget *>().at(0), *palette = d.findChildren<QWidget *>().at(1);
        QCOMPARE(d.zones().finger(0x29), quint8(0)); // the key left of "1"
        QTest::mouseClick(palette, Qt::LeftButton, {}, QPoint(20, palette->height() * 5 / 9 + 3));
        QCOMPARE(d.m_finger, 5);
        QTest::mouseClick(keyboard, Qt::LeftButton, {}, QPoint(30, 30));
        QCOMPARE(d.zones().finger(0x29), quint8(5));
        QVERIFY(!d.zones().isHome(0x29));
        QTest::mouseClick(keyboard, Qt::RightButton, {}, QPoint(30, 30));
        QVERIFY(d.zones().isHome(0x29));
        // Enter: the left button only.
        const QPoint enter(keyboard->width() - 40, 100);
        QCOMPARE(d.zones().finger(0x1C), FingerZones::kNone);
        QTest::mouseClick(keyboard, Qt::RightButton, {}, enter);
        QCOMPARE(d.zones().finger(0x1C), FingerZones::kNone);
        QTest::mouseClick(keyboard, Qt::LeftButton, {}, enter);
        QCOMPARE(d.zones().finger(0x1C), quint8(5));
        // Between the keys: nothing.
        const FingerZones before = d.zones();
        QTest::mouseClick(keyboard, Qt::LeftButton, {}, QPoint(3, 3));
        QVERIFY(d.zones() == before);
    }

    void presets()
    {
        {
            MainWindow w;
            QCOMPARE(w.m_presets->count(), 0);
            w.m_pause->setValue(700);
            w.saveSettings();
            Presets::store(QStringLiteral("Быстрый"));
            w.m_pause->setValue(3000);
            w.saveSettings();
            Presets::store(QStringLiteral("Медленный"));
            Presets::setCurrent(QStringLiteral("Медленный"));
        }
        MainWindow w;
        QCOMPARE(w.m_presets->count(), 2);
        QCOMPARE(w.m_presets->currentText(), QStringLiteral("Медленный"));
        QCOMPARE(w.m_pause->value(), 3000);
        w.m_pause->setValue(3500); // changes go to the preset that is current
        w.selectPreset(QStringLiteral("Быстрый"));
        QCOMPARE(w.m_pause->value(), 700);
        QCOMPARE(Presets::current(), QStringLiteral("Быстрый"));
        w.selectPreset(QStringLiteral("Медленный"));
        QCOMPARE(w.m_pause->value(), 3500);
        // The window geometry and the language are not part of a preset.
        QSettings s;
        s.beginGroup(QStringLiteral("Presets/Быстрый"));
        QVERIFY(s.contains(QStringLiteral("Pause")));
        QVERIFY(!s.contains(QStringLiteral("WindowGeometry")));
        s.endGroup();
        Presets::remove(QStringLiteral("Быстрый"));
        QCOMPARE(Presets::names(), QStringList{QStringLiteral("Медленный")});
        QSettings().clear();
    }

    void captureOffReleasesKeys()
    {
        MainWindow w;
        HookEvent e;
        e.timeUs = 1000;
        e.flags = quint32(0x41) << 16 | 0x1E | KeyRecord::HasChar;
        e.ch = u'a';
        e.chars = 1;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        w.keyEvent(e);
        QCOMPARE(w.m_doc.records.size(), 1);
        w.m_capture->setChecked(false);
        QCOMPARE(w.m_doc.records.size(), 2);
        QVERIFY(w.m_doc.records.last().isUp());
        QCOMPARE(w.m_model.text, QStringLiteral("a"));
    }

private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
