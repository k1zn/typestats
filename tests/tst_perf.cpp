// The performance bench (re/perf.md). Not part of ctest: it needs the large recordings of
// re/scripts/gen_big.py and minutes to run.
//
//   python re/scripts/gen_big.py $TEMP/tsperf
//   cmake --build build-release --target tst_perf
//   TS_PERF_DIR=$TEMP/tsperf QT_QPA_PLATFORM=offscreen ./build-release/tst_perf.exe [test]
//
// TS_PERF_SIZES=10000,100000 limits the sizes. Every scenario runs several times; the median, the
// minimum and the maximum go to $TS_PERF_DIR/perf.txt (appended) and to the test log.

#include "core/ExtraStats.h"
#include "core/FingerZones.h"
#include "core/Graphs.h"
#include "core/Histograms.h"
#include "core/Journal.h"
#include "core/KeyList.h"
#include "core/MainStats.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"
#include "core/Cp1251.h"
#include "export/TableExport.h"
#include "ui/ExtraStatsWindow.h"
#include "ui/StringTableModel.h"
#include "ui/GraphPanels.h"
#include "ui/GraphWidget.h"
#include "ui/HistogramWindow.h"
#include "ui/KlavogramWidget.h"
#include "ui/MainWindow.h"
#include "ui/Presets.h"
#include "ui/TextView.h"
#include "ui/Texts.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

class TstPerf : public QObject
{
    Q_OBJECT

    QString m_dir;
    QList<int> m_sizes;
    QTemporaryDir m_settings;
    QTemporaryDir m_tmp;
    int m_shift = 0;

    QString tsf(int n) const { return QDir(m_dir).filePath(QStringLiteral("big_%1.tsf").arg(n)); }
    QString tsj(int n) const { return QDir(m_dir).filePath(QStringLiteral("big_%1.tsj").arg(n)); }

    void log(const QString &line)
    {
        qInfo().noquote() << line;
        QFile f(QDir(m_dir).filePath(QStringLiteral("perf.txt")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write(line.toUtf8() + '\n');
    }

    // Runs f several times (fewer if it is slow) and logs median / min / max in ms.
    template <class F>
    double measure(const QString &scenario, int size, F &&f, int runs = 5)
    {
        QVector<double> t;
        for (int i = 0; i < runs; ++i) {
            QElapsedTimer e;
            e.start();
            f();
            t << double(e.nsecsElapsed()) / 1e6;
            if (i == 0 && t[0] > 3000.0)
                runs = std::min(runs, 3);
        }
        std::sort(t.begin(), t.end());
        const double med = t[t.size() / 2];
        log(QStringLiteral("%1\t%2\t%3\t%4\t%5")
                .arg(scenario, -40)
                .arg(size, 7)
                .arg(med, 10, 'f', 3)
                .arg(t.first(), 10, 'f', 3)
                .arg(t.last(), 10, 'f', 3));
        return med;
    }

    static qint64 privateBytes()
    {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof pmc))
            return qint64(pmc.PrivateUsage);
#endif
        return 0;
    }

    // Every call moves the selection by one character, or it would not change.
    void select(MainWindow &w, int start, int length)
    {
        start = std::max(0, start - (++m_shift & 1));
        QTextCursor c = w.m_text->textCursor();
        c.setPosition(start);
        c.setPosition(start + length, QTextCursor::KeepAnchor);
        w.m_text->setTextCursor(c);
    }

private slots:
    void initTestCase()
    {
        m_dir = qEnvironmentVariable("TS_PERF_DIR");
        if (m_dir.isEmpty())
            QSKIP("TS_PERF_DIR is not set (python re/scripts/gen_big.py DIR)");
        const QString sizes = qEnvironmentVariable("TS_PERF_SIZES", QStringLiteral("10000,100000,500000"));
        for (const QString &s : sizes.split(u','))
            if (QFileInfo::exists(tsf(s.toInt())))
                m_sizes << s.toInt();
        QVERIFY(!m_sizes.isEmpty());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        log(QStringLiteral("# %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    }

    // Reading and writing files.
    void files()
    {
        for (int n : m_sizes) {
            QByteArray bytes;
            measure(QStringLiteral("file.readAll"), n, [&] {
                QFile f(tsf(n));
                QVERIFY(f.open(QIODevice::ReadOnly));
                bytes = f.readAll();
            });
            QString text;
            measure(QStringLiteral("file.cp1251.decode"), n, [&] { text = Cp1251::decode(bytes); });
            QStringList lines;
            measure(QStringLiteral("file.split"), n, [&] { lines = text.split(QStringLiteral("\r\n")); });
            measure(QStringLiteral("file.parse"), n, [&] { Tsf::parse(lines); });
            TsfDocument doc;
            measure(QStringLiteral("tsf.read"), n, [&] { QVERIFY(Tsf::read(tsf(n), doc) == Tsf::ReadError::None); });
            QCOMPARE(doc.records.size(), n);
            measure(QStringLiteral("tsf.serialize"), n, [&] { Tsf::serialize(doc, true); });
            measure(QStringLiteral("tsf.write (signed)"), n, [&] { QVERIFY(Tsf::write(m_tmp.filePath(QStringLiteral("w.tsf")), doc, true)); });
            KeyRecords recs;
            measure(QStringLiteral("tsj.read"), n, [&] { QVERIFY(Journal::read(tsj(n), recs)); });
            QCOMPARE(recs.size(), n);
        }
    }

    // TS_PERF_DUMP=name: every .tsf of TS_PERF_DIR and the golden ones read, dumped as text and written
    // back, to compare the file code before and after a change.
    void dumpFiles()
    {
        const QString tag = qEnvironmentVariable("TS_PERF_DUMP");
        if (tag.isEmpty())
            QSKIP("TS_PERF_DUMP is not set");
        QStringList paths;
        for (const QString &f : QDir(m_dir).entryList({QStringLiteral("*.tsf")}))
            paths << QDir(m_dir).filePath(f);
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")}))
            paths << QStringLiteral(TS_GOLDEN_DIR "/") + f;
        const QString out = QDir(m_dir).filePath(QStringLiteral("dump_") + tag);
        QDir().mkpath(out);
        for (const QString &path : paths) {
            if (QFileInfo(path).fileName().startsWith(QLatin1String("w_")))
                continue;
            TsfDocument d;
            const int err = int(Tsf::read(path, d));
            QString text = QStringLiteral("err=%1 version=%2 author=%3|comment=%4|date=%5|zones=%6|fingers=%7|video=%8|shift=%9|")
                               .arg(err).arg(d.version).arg(d.author, d.comment, d.date, d.fingerZonesName,
                                                            d.fingers.join(u'/'), d.attachedVideo)
                               .arg(d.videoTimeShiftMs);
            text += QStringLiteral("signed=%1 valid=%2\n").arg(d.signed_).arg(d.signatureValid);
            for (const KeyRecord &r : d.records)
                text += QStringLiteral("%1 %2 %3 %4\n").arg(r.dtUs).arg(r.flags).arg(int(r.ch)).arg(r.comment);
            const QString name = QFileInfo(path).completeBaseName();
            QFile f(QDir(out).filePath(name + QStringLiteral(".txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text.toUtf8());
            QVERIFY(Tsf::write(QDir(out).filePath(name + QStringLiteral("_signed.tsf")), d, true));
            QVERIFY(Tsf::write(QDir(out).filePath(name + QStringLiteral("_plain.tsf")), d, false));
        }
    }

    // The core on the whole recording.
    void core()
    {
        for (int n : m_sizes) {
            TsfDocument doc;
            Tsf::read(tsf(n), doc);
            const RecalcOptions opt;
            KeyRecords norm;
            measure(QStringLiteral("recalc.normalized"), n, [&] { norm = Recalc::normalized(doc.records); });
            measure(QStringLiteral("recalc.erasedRecords"), n, [&] { Recalc::erasedRecords(norm); });
            TextModel m;
            measure(QStringLiteral("recalc.run"), n, [&] { m = Recalc::run(doc.records, opt); });
            RecalcOptions byPauses = opt;
            byPauses.byPauses = true;
            measure(QStringLiteral("recalc.run byPauses"), n, [&] { Recalc::run(doc.records, byPauses); });
            RecalcOptions all = opt;
            all.onlyText = false;
            measure(QStringLiteral("recalc.run onlyText=0"), n, [&] { Recalc::run(doc.records, all); });
            MainStats s;
            measure(QStringLiteral("stats.compute all"), n, [&] { s = Stats::compute(m, 0, m.size(), opt.splitMs, false); });
            measure(QStringLiteral("stats.format"), n, [&] { Stats::format(s, QLocale()); });
            measure(QStringLiteral("stats.compute 100 chars"), n,
                    [&] { Stats::compute(m, m.size() / 2, m.size() / 2 + 100, opt.splitMs, false); });
            measure(QStringLiteral("graphs.compute"), n, [&] { Graphs::compute(m); });
            const FingerZones zones = FingerZones::standard();
            QVector<quint8> fingers;
            measure(QStringLiteral("fingerSeries"), n, [&] { fingers = fingerSeries(m, zones); });
            measure(QStringLiteral("hist.labelsFromRecords"), n, [&] { Histograms::labelsFromRecords(m.records); });
            measure(QStringLiteral("keylist.rows all (export)"), n, [&] { KeyList::rows(m.klav, QLocale()); });
            const double end = double(m.klav.last().tDraw);
            measure(QStringLiteral("keylist.rows window at end"), n,
                    [&] { KeyList::rows(m.klav, QLocale(), end - 3e6, end, 40); });

            for (int k = 0; k < ExtraStats::KindCount; ++k) {
                const auto kind = ExtraStats::Kind(k);
                QVector<ExtraStats::Occurrence> occ;
                measure(QStringLiteral("extra.collect kind %1").arg(k), n, [&] {
                    occ = ExtraStats::collect(m, fingers, 0, m.size(), kind, QStringLiteral("/б/б"));
                });
                measure(QStringLiteral("extra.rows kind %1 plain").arg(k), n, [&] { ExtraStats::rows(occ, false, 0); });
                measure(QStringLiteral("extra.rows kind %1 averages").arg(k), n, [&] { ExtraStats::rows(occ, true, 0); });
            }

            Histograms::Source src;
            src.model = &m;
            std::tie(src.recBegin, src.recEnd) = Histograms::recordRange(m, 0, m.size());
            src.zones = zones;
            src.label = Histograms::labelsFromRecords(m.records);
            for (auto kind : {Histograms::Node::AllKeys, Histograms::Node::AllFingers}) {
                Histograms::Node node;
                node.kind = kind;
                Histograms::Page page;
                measure(QStringLiteral("hist.build kind %1").arg(int(kind)), n, [&] { page = Histograms::build(src, node); });
                if (const auto next = Histograms::drill(node, page, 0))
                    measure(QStringLiteral("hist.build drill of kind %1").arg(int(kind)), n,
                            [&] { Histograms::build(src, *next); });
            }
        }
    }

    // The text view by parts: plain text, styles, the first layout.
    void textView()
    {
        for (int n : m_sizes) {
            TsfDocument doc;
            Tsf::read(tsf(n), doc);
            const TextModel m = Recalc::run(doc.records, RecalcOptions());
            log(QStringLiteral("# text %1 chars, %2 runs").arg(m.text.size()).arg(m.runs.size()));
            TextView v;
            v.resize(650, 120);
            v.show();
            QVERIFY(QTest::qWaitForWindowExposed(&v));
            measure(QStringLiteral("text.setPlainText shown"), n, [&] { v.setPlainText(m.text); });
            measure(QStringLiteral("text.setModel shown"), n, [&] { v.setModel(m); });
            measure(QStringLiteral("text.setModel + processEvents"), n, [&] {
                v.setModel(m);
                QApplication::processEvents();
            });
            measure(QStringLiteral("text.setModel + paint"), n, [&] {
                v.setModel(m);
                v.grab();
            });
            v.hide();
            measure(QStringLiteral("text.setModel hidden"), n, [&] { v.setModel(m); });
            // TS_PERF_HTML=name: the document as HTML, to compare the styles before and after a change.
            if (const QString html = qEnvironmentVariable("TS_PERF_HTML"); !html.isEmpty()) {
                QFile f(QDir(m_dir).filePath(QStringLiteral("%1_%2.html").arg(html).arg(n)));
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(v.document()->toHtml().toUtf8());
            }
        }
    }

    // The main window: opening, the parts of recalculate(), selection, scrolling, painting.
    void window()
    {
        MainWindow w;
        w.resize(1000, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        for (int n : m_sizes) {
            measure(QStringLiteral("window.openFile tsf"), n, [&] { QVERIFY(w.openFile(tsf(n))); });
            measure(QStringLiteral("window.openFile tsj"), n, [&] { QVERIFY(w.openFile(tsj(n))); });
            measure(QStringLiteral("window.recalculate"), n, [&] { w.recalculate(); });
            measure(QStringLiteral("  Recalc::run"), n, [&] { w.m_model = Recalc::run(w.m_doc.records, w.options()); });
            measure(QStringLiteral("  text.setModel"), n, [&] { w.m_text->setModel(w.m_model); });
            measure(QStringLiteral("  klav.setModel"), n, [&] { w.m_klav->setModel(&w.m_model); });
            measure(QStringLiteral("  graph.setModel"), n, [&] { w.m_graph->setModel(&w.m_model); });
            measure(QStringLiteral("  updateStats"), n, [&] { w.updateStats(); });
            measure(QStringLiteral("  klavogramMoved"), n, [&] { w.klavogramMoved(); });
            measure(QStringLiteral("window.processEvents after recalc"), n, [&] {
                w.recalculate();
                QApplication::processEvents();
            });

            const int len = int(w.m_model.text.size());
            measure(QStringLiteral("select 100 chars in the middle"), n, [&] { select(w, len / 2, 100); });
            measure(QStringLiteral("select 100 chars at the end"), n, [&] { select(w, len - 101, 100); });
            measure(QStringLiteral("select all"), n, [&] { select(w, 0, len); });
            measure(QStringLiteral("cursor at the end (no selection)"), n, [&] { select(w, len, 0); });

            const float endMs = float(w.m_model.klav.last().tDraw / 1000);
            for (const auto &[name, ms] : {std::pair{"start", 0.0f}, {"middle", endMs / 2}, {"end", endMs - 2000}}) {
                measure(QStringLiteral("scroll klavogram to %1").arg(QLatin1String(name)), n, [&] {
                    w.m_klav->setScrollMs(ms);
                    w.klavogramMoved();
                });
                measure(QStringLiteral("paint klavogram at %1").arg(QLatin1String(name)), n, [&] { w.m_klav->grab(); });
            }
            w.m_klav->setZoom(0.04f);
            w.m_klav->setScrollMs(endMs / 2);
            measure(QStringLiteral("paint klavogram zoom 0.04 middle"), n, [&] { w.m_klav->grab(); });
            w.m_klav->m_cursorMode = true;
            w.m_klav->m_cursorX = 300;
            measure(QStringLiteral("paint klavogram cursor mode middle"), n, [&] { w.m_klav->grab(); });
            w.m_klav->m_cursorMode = false;
            w.m_klav->setZoom(0.25f);
            measure(QStringLiteral("paint graph"), n, [&] { w.m_graph->grab(); });
            w.m_graph->m_zoomX = 0.5f;
            measure(QStringLiteral("paint graph zoom 0.5"), n, [&] { w.m_graph->grab(); });
            w.m_graph->m_zoomX = 8.0f;
            measure(QStringLiteral("paint legend"), n, [&] { w.m_legend->grab(); });
            measure(QStringLiteral("paint text"), n, [&] { w.m_text->grab(); });
            measure(QStringLiteral("paint whole window"), n, [&] { w.grab(); });

            // Form3 and Form4 open: every selection change recomputes them.
            w.m_extra->show();
            w.m_hist->show();
            QApplication::processEvents();
            for (int k : {int(ExtraStats::Words), int(ExtraStats::Pairs), int(ExtraStats::Sentences)}) {
                w.m_extra->m_kinds[k]->setChecked(true);
                measure(QStringLiteral("updateExtraStats kind %1").arg(k), n, [&] { w.updateExtraStats(); });
            }
            // Form3 by parts, words of the whole text.
            {
                ExtraStatsWindow *x = w.m_extra;
                x->m_kinds[ExtraStats::Words]->setChecked(true);
                measure(QStringLiteral("  extra: setSource (words)"), n, [&] { w.updateExtraStats(); });
                measure(QStringLiteral("  extra: fingerSeries"), n,
                        [&] { fingerSeries(w.m_model, w.m_schemes.zones(w.m_fingers->currentText())); });
                measure(QStringLiteral("  extra: computeNow"), n, [&] { x->computeNow(); });
                measure(QStringLiteral("  extra: showRows"), n, [&] { x->showRows(); });
                measure(QStringLiteral("  extra: rows()"), n, [&] { ExtraStats::rows(x->m_occ, false, 0); });
                QVector<QStringList> table;
                measure(QStringLiteral("  extra: format the table"), n, [&] {
                    table.clear();
                    for (const ExtraStats::Row &r : x->m_rows)
                        table << QStringList{ExtraStats::formatSpeed(r.speed, QLocale()), r.text};
                });
                measure(QStringLiteral("  extra: model setTable"), n,
                        [&] { x->m_listModel->setTable({QStringLiteral("a"), QStringLiteral("b")}, table); });
                measure(QStringLiteral("  extra: selectRow(0)"), n, [&] { x->selectRow(0); });
                measure(QStringLiteral("  extra: paint"), n, [&] { x->grab(); });
                x->m_averages->setChecked(true);
                measure(QStringLiteral("  extra: setSource (words, averages)"), n, [&] { w.updateExtraStats(); });
                x->m_averages->setChecked(false);
            }
            measure(QStringLiteral("updateHistograms"), n, [&] { w.updateHistograms(); });
            measure(QStringLiteral("paint histograms"), n, [&] { w.m_hist->grab(); });
            measure(QStringLiteral("select 100 chars, Form3+Form4 open"), n, [&] { select(w, len / 2 + 7, 100); });
            measure(QStringLiteral("recalculate, Form3+Form4 open"), n, [&] { w.recalculate(); });
            w.m_extra->hide();
            w.m_hist->hide();

            TableExport::Table keys;
            measure(QStringLiteral("export keyTable"), n, [&] { keys = w.keyTable(); });
            measure(QStringLiteral("export csv"), n, [&] { TableExport::write(m_tmp.filePath(QStringLiteral("k.csv")), keys, false, QLocale()); });
            measure(QStringLiteral("export xlsx + chart"), n, [&] { TableExport::write(m_tmp.filePath(QStringLiteral("k.xlsx")), keys, true, QLocale()); }, 3);
        }
    }

    // One key at a time into a large recording: the latency of every event (the hook waits for it).
    void keystrokes()
    {
        QSettings().setValue(QStringLiteral("JournalOn"), true);
        for (int n : m_sizes) {
            MainWindow w; // not shown: the keys count as typed into another window
            QVERIFY(w.openFile(tsf(n)));
            QVector<double> t;
            const int events = 2000;
            qint64 now = 1'000'000'000;
            for (int i = 0; i < events; ++i) {
                const bool down = i % 2 == 0;
                const quint8 vk = 'A' + (i / 2) % 26;
                HookEvent e;
                now += 90'000;
                e.timeUs = now;
                e.flags = quint32(vk) << 16 | (vk & 0x7f) | (down ? KeyRecord::HasChar : KeyRecord::KeyUp | KeyRecord::NoChar);
                e.ch = down ? char16_t(u'a' + (vk - 'A')) : 0;
                e.chars = down ? 1 : 0;
                QElapsedTimer timer;
                timer.start();
                w.keyEvent(e);
                t << double(timer.nsecsElapsed()) / 1e3;
            }
            QCOMPARE(w.m_doc.records.size(), n + events);
            std::sort(t.begin(), t.end());
            log(QStringLiteral("%1\t%2\tmedian %3 us\tp99 %4 us\tmax %5 us")
                    .arg(QStringLiteral("keyEvent (journal on)"), -40)
                    .arg(n, 7)
                    .arg(t[t.size() / 2], 0, 'f', 1)
                    .arg(t[t.size() * 99 / 100], 0, 'f', 1)
                    .arg(t.last(), 0, 'f', 1));
            // What the timer then does when the window becomes active.
            measure(QStringLiteral("recalculate after typing"), n, [&] { w.recalculate(); });
        }
        QSettings().remove(QStringLiteral("JournalOn"));
        QFile::remove(JournalWriter(QCoreApplication::applicationDirPath()).path());
    }

    void settings()
    {
        // QSettings as the program makes it on every key and statistics update. The registry is
        // only read here, from a key that does not exist.
        measure(QStringLiteral("QSettings ini + 4 values"), 0, [&] {
            for (int i = 0; i < 1000; ++i) {
                const QSettings s;
                s.value(QStringLiteral("GlobalOnOff"), true).toBool();
                s.value(QStringLiteral("GlobalClear"), true).toBool();
                s.value(QStringLiteral("AutoComments"), false).toBool();
                s.value(QStringLiteral("JournalOn"), false).toBool();
            }
        });
#ifdef Q_OS_WIN
        measure(QStringLiteral("QSettings registry + 4 values"), 0, [&] {
            for (int i = 0; i < 1000; ++i) {
                const QSettings s(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("TypingStatisticsPerfNone"),
                                  QStringLiteral("None"));
                s.value(QStringLiteral("GlobalOnOff"), true).toBool();
                s.value(QStringLiteral("GlobalClear"), true).toBool();
                s.value(QStringLiteral("AutoComments"), false).toBool();
                s.value(QStringLiteral("JournalOn"), false).toBool();
            }
        });
#endif
    }

    void startupAndMemory()
    {
        measure(QStringLiteral("MainWindow() + show"), 0, [&] {
            MainWindow w;
            w.show();
            QApplication::processEvents();
        });
        for (int n : m_sizes) {
            const qint64 before = privateBytes();
            {
                MainWindow w;
                w.show();
                QVERIFY(w.openFile(tsf(n)));
                QApplication::processEvents();
                const qint64 after = privateBytes();
                log(QStringLiteral("%1\t%2\t%3 MB")
                        .arg(QStringLiteral("memory: window with the recording"), -40)
                        .arg(n, 7)
                        .arg(double(after - before) / 1048576.0, 0, 'f', 1));
            }
        }
    }
};

QTEST_MAIN(TstPerf)
#include "tst_perf.moc"
