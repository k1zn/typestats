// The main window driven without a screen (QT_QPA_PLATFORM=offscreen, set by ctest): opening a
// recording, editing, copying, the graph and its link with the klavogram.

#include "core/Editing.h"
#include "ui/StampRecorder.h"
#include "core/Stamps.h"
#include "core/Der.h"
#include "core/TsfFile.h"
#include "core/Journal.h"
#include "core/KeyList.h"
#include "ui/AppPaths.h"
#include "ui/ExtraStatsWindow.h"
#include "ui/FingerZonesDialog.h"
#include "ui/GraphPanels.h"
#include "ui/Hotkeys.h"
#include "ui/GraphWidget.h"
#include "ui/HistogramWindow.h"
#include "ui/KlavogramWidget.h"
#include "ui/LiveStatsWindow.h"
#include "ui/Look.h"
#include "ui/MainWindow.h"
#include "ui/Presets.h"
#include "ui/TextInputWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TextView.h"
#ifdef TS_HAVE_WEBCAM
#include "media/Yuv.h"
#include "ui/VideoPropertiesDialog.h"
#include "ui/VideoWindow.h"
#include "ui/WebcamRecorder.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QListWidget>
#include <QRadioButton>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSplitter>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QDataStream>
#include <QFile>

#include "xlsxdocument.h"
#include <QTest>
#include <QTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QComboBox>
#include <QToolButton>
#include <QMessageBox>
#include <QPushButton>
#include <QKeyEvent>

// A time stamp token that only carries the imprint (not signed): enough for StampRecorder, which keeps what
// comes; the report would not accept it.
QByteArray fakeToken(const QByteArray &imprint)
{
    using namespace Der;
    const QByteArray algId = tlv(Sequence, tlv(Oid, oid("2.16.840.1.101.3.4.2.1")) + tlv(Null, {}));
    const QByteArray tst = tlv(Sequence, integer(1) + tlv(Oid, oid("1.2.3.4")) + tlv(Sequence, algId + tlv(OctetString, imprint))
                                             + integer(7) + tlv(GeneralizedTime, "20261006120000Z"));
    const QByteArray encap = tlv(Sequence, tlv(Oid, oid("1.2.840.113549.1.9.16.1.4")) + tlv(context(0), tlv(OctetString, tst)));
    const QByteArray signedData = tlv(Sequence, integer(3) + tlv(Set, {}) + encap + tlv(Set, tlv(Sequence, integer(1))));
    return tlv(Sequence, tlv(Oid, oid("1.2.840.113549.1.7.2")) + tlv(context(0), signedData));
}

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
        AppPaths::setDataDir(m_settings.path()); // FingerZones.ini, ExStats.ini, journals
    }

    void dataFiles()
    {
        // The files of the program are in its data folder; an older copy next to the program is taken over.
        QTemporaryDir data;
        AppPaths::setDataDir(data.path());
        const QString name = QStringLiteral("tst_ui-%1.ini").arg(QCoreApplication::applicationPid());
        QFile old(QDir(QCoreApplication::applicationDirPath()).filePath(name));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("[a]\r\n");
        old.close();
        const QString path = AppPaths::file(name);
        QCOMPARE(path, QDir(data.path()).filePath(name));
        QVERIFY(QFile::exists(path));
        QVERIFY(old.remove());
        AppPaths::setDataDir(m_settings.path());
    }

    void journalFailure()
    {
        // A journal that cannot be written is said once, without stopping the recording.
        QTemporaryDir data;
        const QString notDir = QDir(data.path()).filePath(QStringLiteral("file"));
        QFile f(notDir);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        AppPaths::setDataDir(notDir);
        QSettings().setValue(QStringLiteral("JournalOn"), true);
        {
            MainWindow w;
            w.applySettings();
            if (QApplication::activeWindow())
                QSKIP("the test window got the focus");
            HookEvent e;
            e.flags = quint32('A') << 16 | 0x1E | KeyRecord::HasChar;
            e.ch = u'a';
            e.chars = 1;
            w.keyEvent(e);
            QVERIFY(w.m_journalFailed);
            QCOMPARE(w.findChildren<QMessageBox *>().size(), 1);
            e.timeUs = 100000;
            w.keyEvent(e);
            QCOMPARE(w.findChildren<QMessageBox *>().size(), 1);
            QCOMPARE(w.m_doc.records.size(), 2);
        }
        QSettings().remove(QStringLiteral("JournalOn"));
        AppPaths::setDataDir(m_settings.path());
    }

    void opensAndPaints()
    {
        MainWindow w;
        QCOMPARE(w.m_doc.platform, currentKeyPlatform()); // a new recording is this system's
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        QCOMPARE(w.m_doc.platform, KeyPlatform::Windows); // the original's file
        QCOMPARE(w.m_model.keyNames, KeyPlatform::Windows);
        QCOMPARE(w.m_model.size(), 269);
        QCOMPARE(w.m_stats->rowCount(), 17);
        QCOMPARE(w.m_stats->item(0, 1)->text(), QStringLiteral("269 (269)"));
        QVERIFY(w.m_keys->rowCount() > 5);
        QVERIFY(!w.grab().isNull()); // every widget paints itself
        QVERIFY(w.m_legend->isVisible());
        // The capture state goes first in the title: it is what the task bar shows. An opened file switches
        // recording off (LoadTsf).
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: OFF - Typing statistics (re-")));
        QVERIFY(w.windowTitle().endsWith(QStringLiteral(" - обыка.tsf")));
        w.m_capture->setChecked(true);
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: ON - ")));
        QVERIFY(w.windowTitle().endsWith(QStringLiteral(" - обыка.tsf")));
        w.m_capture->setChecked(false);
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: OFF - ")));
    }

    void hotkeyNames()
    {
        // The keys are named as the system names them.
#if defined(Q_OS_WIN)
        const QString clear = QStringLiteral("LCtrl+LWin"), copy = QStringLiteral("Ctrl+C"), mark = QStringLiteral("Ins");
#elif defined(Q_OS_MACOS)
        const QString clear = QStringLiteral("L⌃+L⌘"), copy = QStringLiteral("⌘C"), mark = QStringLiteral("⌘I");
#else
        const QString clear = QStringLiteral("LCtrl+LSuper"), copy = QStringLiteral("Ctrl+C"), mark = QStringLiteral("Ins");
#endif
        QCOMPARE(Hotkeys::clear(), clear);
        QCOMPARE(Hotkeys::copy(), copy);
        QCOMPARE(Hotkeys::mark(), mark);
        MainWindow w;
        QStringList hints;
        for (const QToolButton *b : w.findChildren<QToolButton *>())
            hints << b->toolTip();
        QVERIFY(hints.contains(QStringLiteral("Очистить (") + clear + u')'));
        QVERIFY(hints.contains(QStringLiteral("Копировать (") + copy + u')'));

        auto action = [](int key, Qt::KeyboardModifiers mods) {
            const QKeyEvent e(QEvent::KeyPress, key, mods);
            return Hotkeys::textAction(&e);
        };
        QCOMPARE(action(Qt::Key_Delete, Qt::NoModifier), Hotkeys::TextAction::Delete);
        QCOMPARE(action(Qt::Key_Insert, Qt::NoModifier), Hotkeys::TextAction::Mark);
        QCOMPARE(action(Qt::Key_C, Qt::ControlModifier), Hotkeys::TextAction::Copy);
        QCOMPARE(action(Qt::Key_Insert, Qt::ControlModifier), Hotkeys::TextAction::Copy);
        QCOMPARE(action(Qt::Key_C, Qt::NoModifier), Hotkeys::TextAction::None);
#ifdef Q_OS_MACOS
        QCOMPARE(action(Qt::Key_Backspace, Qt::NoModifier), Hotkeys::TextAction::Delete);
        QCOMPARE(action(Qt::Key_I, Qt::ControlModifier), Hotkeys::TextAction::Mark);
#else
        QCOMPARE(action(Qt::Key_Backspace, Qt::NoModifier), Hotkeys::TextAction::None);
#endif
    }

    void hookFailure()
    {
        // The hook could not start: "Ts: OFF", the reason once; it stays off until the hook starts by itself.
        MainWindow w;
        QVERIFY(w.m_capture->isChecked());
        emit w.m_hook.failed(QStringLiteral("no access"));
        QVERIFY(!w.m_capture->isChecked());
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: OFF - ")));
        QCOMPARE(w.findChildren<QMessageBox *>().size(), 1);
        QCOMPARE(w.findChild<QMessageBox *>()->text(), QStringLiteral("no access"));
        w.m_capture->setChecked(true); // the user tries: the reason again, the same box
        QVERIFY(!w.m_capture->isChecked());
        QCOMPARE(w.findChildren<QMessageBox *>().size(), 1);
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: OFF - ")));

        emit w.m_hook.started(); // the access was granted
        QVERIFY(w.m_capture->isChecked());
        QVERIFY(w.windowTitle().startsWith(QStringLiteral("Ts: ON - ")));
        QTRY_COMPARE(w.findChildren<QMessageBox *>().size(), 0);
        // Switched off by the user: a start of the hook does not switch it on.
        w.m_capture->setChecked(false);
        emit w.m_hook.started();
        QVERIFY(!w.m_capture->isChecked());

        // With a command (Linux): it is in one line, the button copies it and the window stays.
        w.m_capture->setChecked(true);
        emit w.m_hook.failed(QStringLiteral("no access"), QStringLiteral("sudo fix"));
        auto *dialog = w.m_hookErrorBox.data();
        QVERIFY(dialog && !qobject_cast<QMessageBox *>(dialog));
        QCOMPARE(dialog->findChild<QLineEdit *>()->text(), QStringLiteral("sudo fix"));
        QPushButton *copy = nullptr;
        for (QPushButton *b : dialog->findChildren<QPushButton *>())
            if (b->isDefault())
                copy = b;
        QVERIFY(copy);
        QGuiApplication::clipboard()->clear();
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sudo fix"));
        QVERIFY(dialog->isVisible());
        emit w.m_hook.started();
        QTRY_VERIFY(!w.m_hookErrorBox);
    }

    void klavogramCursor()
    {
        // The cursor mode sticks to the nearest key event: the same one as a search from the first record.
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("824.tsf")));
        KlavogramWidget *k = w.m_klav;
        k->m_cursorMode = true;
        const QVector<KlavRecord> &klav = w.m_model.klav;
        int checked = 0;
        for (float zoom : {0.04f, 0.25f, 3.0f, 300.0f})
            for (float scroll : {-500.0f, 0.0f, 0.001f * klav[klav.size() / 2].tDraw, 0.001f * klav.last().tDraw}) {
                k->setZoom(zoom);
                k->setScrollMs(scroll);
                for (int x : {-1000, 1, 5, 100, 333, k->width() - 1, k->width() + 50}) {
                    k->m_cursorX = x;
                    k->grab();
                    float best = 1e30f;
                    qint64 bestDraw = 0;
                    for (const KlavRecord &r : klav) {
                        const float px = (float(kExtMilli * r.tDraw) - k->m_scrollMs) * k->m_zoom;
                        const float distance = std::fabs(px - float(x));
                        if (distance < best) {
                            best = distance;
                            bestDraw = r.tDraw;
                        } else if (distance > best) {
                            break;
                        }
                        if (px > float(k->width()))
                            break;
                    }
                    const qint64 drawUs =
                        best > 8.0f ? qint64((float(x) / k->m_zoom + k->m_scrollMs) * 1000.0f) : bestDraw;
                    QCOMPARE(k->m_cursorT, k->absoluteTime(drawUs));
                    ++checked;
                }
            }
        QCOMPARE(checked, 4 * 4 * 7);
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

    void recordingKeepsRoom()
    {
        // The next key must not reallocate the whole recording inside the hook, also after editing.
        MainWindow w;
        QVERIFY(w.openFile(golden("обыка.tsf")));
        auto room = [&w] { return w.m_doc.records.capacity() - w.m_doc.records.size(); };
        QVERIFY(room() >= 4096);
        select(w, 9, 5);
        w.deleteSelection();
        QVERIFY(room() >= 4096);
        w.undo();
        QVERIFY(room() >= 4096);
        select(w, 0, 40);
        w.removeNonText();
        QVERIFY(room() >= 4096);
        w.normalizeRecords();
        QVERIFY(room() >= 4096);
    }

    void textFormatsAfterRecalculate()
    {
        // The text cursor inside a coloured run does not colour the text built next (QTextEdit::setPlainText
        // takes the format at the cursor).
        MainWindow w;
        w.show();
        QVERIFY(w.openFile(golden("824.tsf")));
        const QString html = w.m_text->document()->toHtml();
        int coloured = -1;
        for (QTextBlock b = w.m_text->document()->begin(); b.isValid() && coloured < 0; b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().hasProperty(QTextFormat::ForegroundBrush) && it.fragment().length() > 1) {
                    coloured = it.fragment().position() + 1;
                    break;
                }
        QVERIFY(coloured > 0);
        select(w, coloured, 0);
        w.recalculate();
        QCOMPARE(w.m_text->document()->toHtml(), html);
        select(w, coloured, 1);
        w.recalculate();
        QCOMPARE(w.m_text->document()->toHtml(), html);
    }

    void convertLayout()
    {
        MainWindow w;
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        const QString text = w.m_model.text;
        // A layout that types every key as "x": the selected word becomes "xxxxx", the rest stays.
        w.m_toUnicode = [](quint8 scan, bool, bool, char16_t out[2]) {
            out[0] = u'x';
            return scan == 0x39 ? 0 : 1;
        };
        w.convertLayout(); // nothing selected: nothing to do
        QCOMPARE(w.m_model.text, text);
        select(w, 9, 5); // "этому"
        w.convertLayout();
        QCOMPARE(w.m_model.text, text.left(9) + QStringLiteral("xxxxx") + text.mid(14));
        w.undo();
        QCOMPARE(w.m_model.text, text);
    }

    void darkTheme()
    {
        // The button in the right corner of the toolbar switches the theme now, keeps it, and the text keeps its
        // selection while its style colours follow the theme.
        auto foregrounds = [](const QTextDocument *doc) {
            QSet<QRgb> colors;
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                for (auto it = b.begin(); !it.atEnd(); ++it)
                    if (it.fragment().charFormat().hasProperty(QTextFormat::ForegroundBrush))
                        colors.insert(it.fragment().charFormat().foreground().color().rgb());
            return colors;
        };
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("824.tsf")));
        QWidget *bar = w.m_themeButton->parentWidget();
        QCOMPARE(w.m_themeButton->geometry().right(), bar->width() - MainWindow::kCornerMargin - 1);
        QVERIFY(!w.m_themeButton->isChecked() && !Look::isDark());
        const QSet<QRgb> light = foregrounds(w.m_text->document());
        QVERIFY(light.contains(QColor(255, 0, 0).rgb())); // erased characters
        QTextCursor c = w.m_text->textCursor();
        c.setPosition(5);
        c.setPosition(20, QTextCursor::KeepAnchor);
        w.m_text->setTextCursor(c);

        w.m_themeButton->click();
        QVERIFY(Look::isDark());
        QVERIFY(QSettings().value(QStringLiteral("DarkTheme")).toBool());
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x36, 0x36, 0x36));
        QCOMPARE(w.m_legend->palette().color(QPalette::Window), Look::colors().legend);
        const QSet<QRgb> dark = foregrounds(w.m_text->document());
        QVERIFY(dark.contains(Look::colors().erased.rgb()));
        QVERIFY(!dark.contains(QColor(255, 0, 0).rgb()));
        QCOMPARE(w.m_text->textCursor().selectionStart(), 5);
        QCOMPARE(w.m_text->textCursor().selectionEnd(), 20);
        // A window opened later is dark too.
        MainWindow w2;
        QVERIFY(w2.m_themeButton->isChecked());

        w.m_themeButton->click();
        QVERIFY(!Look::isDark());
        QVERIFY(!QSettings().value(QStringLiteral("DarkTheme")).toBool());
        QCOMPARE(w.m_legend->palette().color(QPalette::Window), QColor(255, 255, 200));
        QCOMPARE(w.m_legend->palette().color(QPalette::WindowText), QColor(Qt::black)); // the frame
        QCOMPARE(foregrounds(w.m_text->document()), light);

        // The first start takes the system's theme (offscreen has none: light) and keeps it; later the saved one.
        QSettings().remove(QStringLiteral("DarkTheme"));
        Look::applySavedTheme();
        QVERIFY(!Look::isDark());
        QCOMPARE(QSettings().value(QStringLiteral("DarkTheme")), QVariant(false));
        QSettings().setValue(QStringLiteral("DarkTheme"), true);
        Look::applySavedTheme();
        QVERIFY(Look::isDark());
        Look::setDark(false);
    }

    void minimumSize()
    {
        // The window is no smaller than what holds everything: the toolbar with the widest text of the time stamps,
        // the panes at their least with the graph shown, the statistics and three keys.
        MainWindow w;
        w.show();
        w.m_proofButton->setText(QStringLiteral("✓ 100 % !"));
        w.m_proofButton->adjustSize();
        w.m_proofButton->show();
        w.placeCornerButtons();
        w.resize(100, 100);
        QCoreApplication::processEvents();
        QCOMPARE(w.size(), w.minimumSizeHint());
        QVERIFY(w.m_proofButton->x() > w.m_newZonesButton->geometry().right() + 23); // the button right of it too
        QVERIFY(w.m_proofButton->geometry().right() < w.m_themeButton->x());
        QVERIFY(w.m_themeButton->geometry().right() < w.m_themeButton->parentWidget()->width());
        QVERIFY(w.m_graph->isVisible());
        QVERIFY(w.m_graph->height() > 50);
        QVERIFY(w.m_keys->height() >= w.m_keys->minimumHeight());
    }

    void panelButtons()
    {
        // The buttons of the floating panels only open them and are off while they are open.
        MainWindow w;
        w.show();
        QVERIFY(w.m_legend->isVisible());
        QVERIFY(!w.m_legendButton->isEnabled());
        QVERIFY(!w.m_axisPanel->isVisible());
        QVERIFY(w.m_axisButton->isEnabled());
        // The original's video buttons stay disabled (its AVI is not ported); the webcam has its button in the corner.
        int video = 0;
        for (const QToolButton *b : w.findChildren<QToolButton *>())
            if (b->toolTip() == QStringLiteral("Видео") || b->toolTip() == QStringLiteral("Свойства видео")) {
                QVERIFY(!b->isEnabled());
                ++video;
            }
        QCOMPARE(video, 2);
#ifdef TS_HAVE_WEBCAM
        QVERIFY(w.m_cameraButton->isVisible());
        QVERIFY(w.m_cameraButton->geometry().bottom() < w.m_themeButton->parentWidget()->height());
        QCOMPARE(w.m_cameraButton->geometry().right(), w.m_themeButton->geometry().right());
        QVERIFY(!w.m_cameraButton->geometry().intersects(w.m_proofButton->geometry()));
#endif

        emit w.m_legend->closed(); // the red button
        w.m_legend->hide();
        QVERIFY(w.m_legendButton->isEnabled());
        w.showLegend();
        QVERIFY(w.m_legend->isVisible());
        QVERIFY(!w.m_legendButton->isEnabled());

        w.showAxisPanel();
        QVERIFY(w.m_axisPanel->isVisible());
        QVERIFY(!w.m_axisButton->isEnabled());
        w.showAxisPanel(); // not a toggle
        QVERIFY(w.m_axisPanel->isVisible());
        w.m_axisPanel->hide();
        emit w.m_axisPanel->closed();
        QVERIFY(w.m_axisButton->isEnabled());

        w.showLiveStats();
        w.showLiveStats(); // shows, does not toggle
        QVERIFY(w.m_live->isVisible());
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

    // The Windows hook tells the window of the moment of the key: it decides, not the window active now.
    void windowOfTheKey()
    {
        MainWindow w;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        qint64 t = 0;
        auto key = [&](quint64 window, bool own, quint8 vk) {
            for (bool down : {true, false}) {
                HookEvent e;
                e.timeUs = (t += 100) * 1000;
                e.flags = quint32(vk) << 16 | (vk & 0x7f) | KeyRecord::NoChar | (down ? 0u : quint32(KeyRecord::KeyUp));
                e.window = window;
                e.ownWindow = own;
                w.keyEvent(e);
            }
        };
        key(0x1234, false, 'A'); // another program's window
        QCOMPARE(w.m_doc.records.size(), 2);
        key(0x1234, true, 'B'); // one of the program's own
        QCOMPARE(w.m_doc.records.size(), 2);
        const auto input = quint64(w.m_input->winId());
        key(input, true, 'C'); // the text input window is typed into
        QCOMPARE(w.m_doc.records.size(), 4);
        key(input, true, 0x1B); // its Esc is a command
        QCOMPARE(w.m_doc.records.size(), 4);
    }

    // The Windows hook in its thread: started, stopped, started again. It only listens: no input is sent.
    void hookThread()
    {
#ifdef Q_OS_WIN
        KeyboardHook hook;
        QSignalSpy started(&hook, &KeyboardHook::started);
        if (!hook.start())
            QSKIP("the system did not allow the hook here");
        QVERIFY(hook.isRunning());
        QCOMPARE(started.size(), 1);
        hook.stop();
        QVERIFY(!hook.isRunning());
        QVERIFY(hook.start());
        QCOMPARE(started.size(), 2);
        hook.stop();
        QVERIFY(hook.start()); // the destructor stops it
#else
        QSKIP("the Windows hook");
#endif
    }

    void webcam()
    {
#ifdef TS_HAVE_WEBCAM
        // A recording with the webcam: frames and sound go in as the devices would give them (re/webcam.md).
        MainWindow w;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        WebcamRecorder &cam = *w.m_webcam;
        cam.setDevicesEnabled(false);
        WebcamRecorder::Settings s;
        s.video = true;
        s.quality = 0; // 320×240, 10 fps
        s.audio = true;
        cam.setSettings(s);
        cam.setCapture(true);
        QVERIFY(cam.isRecording());
        auto key = [&w](qint64 ms, quint8 vk, bool down, char16_t ch) {
            HookEvent e;
            e.timeUs = ms * 1000;
            e.flags = quint32(vk) << 16 | (vk & 0x7f) | (down ? (ch ? KeyRecord::HasChar : KeyRecord::NoChar)
                                                               : KeyRecord::KeyUp | KeyRecord::NoChar);
            e.ch = ch;
            e.chars = ch ? 1 : 0;
            w.keyEvent(e);
        };
        const I420Frame picture = Yuv::fromImage(QImage(320, 240, QImage::Format_RGB32));
        const QVector<float> sound(4800, 0.1f); // 100 ms, mono 48 kHz
        // a at 0.5 s, b at 1.5 s, c at 2.5 s; a frame and 100 ms of sound every 100 ms from 0 to 3 s.
        for (int ms = 0; ms <= 3000; ms += 100) {
            for (auto [at, vk, ch] : {std::tuple{500, 'A', u'a'}, {1500, 'B', u'b'}, {2500, 'C', u'c'}}) {
                if (ms == at)
                    key(at, quint8(vk), true, ch);
                if (ms == at + 100)
                    key(at + 80, quint8(vk), false, 0);
            }
            cam.addVideo(picture, qint64(ms) * 1000);
            cam.addAudio(sound, 1, 48000, qint64(ms) * 1000);
            cam.drain();
        }
        QCOMPARE(w.m_doc.records.size(), 6);
        // The first record came at 0.5 s with dt 0 (the recorder's first event): the document's time 0 is 0.5 s.
        QCOMPARE(DocTime::of(w.m_doc.records, 0), 0);
        const MediaClip &clip = w.m_clip;
        QList<qint64> video;
        int audio = 0;
        for (const MediaPacket &p : clip.packets) {
            if (clip.streams[p.stream].kind == MediaStream::Video)
                video.append(clip.docTime(p.ptsUs));
            else
                ++audio;
        }
        QCOMPARE(video.size(), 31);
        for (int i = 0; i < video.size(); ++i)
            QCOMPARE(video[i], qint64(i) * 100000 - 500000); // frames before the first key wait for it
        QVERIFY(clip.packets[0].key);
        QVERIFY(audio >= 150);
        QVERIFY(w.m_unsaved);

        // The frame of the klavogram's left edge: b's press is at 1.0 s of the document.
        w.recalculate();
        QCOMPARE(w.m_model.text, QStringLiteral("abc"));
        w.scrollKlavogramToDocTime(1000000);
        QVERIFY(std::abs(w.klavogramDocTimeUs() - 1000000) <= 1000);
        QCOMPARE(clip.docTime(clip.packets[clip.videoFramesAt(w.klavogramDocTimeUs()).second].ptsUs), 1000000);

        // "Сохранить блок" of "b": b's press and release (a's release before it is dropped: the block starts at a
        // press), the video of their time only.
        select(w, 1, 1);
        const TsfDocument block = w.documentForSave(true);
        QCOMPARE(block.records.size(), 2);
        MediaClip cut;
        QVERIFY(MediaClip::parse(block.webcam, cut));
        qint64 lastVideo = MediaClip::kAll;
        for (const MediaPacket &p : cut.packets)
            if (cut.streams[p.stream].kind == MediaStream::Video)
                lastVideo = std::max(lastVideo, cut.docTime(p.ptsUs));
        // The block's records: b's press at 920 ms of its time (its dt), its release at 1000 ms.
        QCOMPARE(DocTime::of(block.records, 0), 920000);
        QCOMPARE(DocTime::end(block.records), 1000000);
        QCOMPARE(lastVideo, 920000);               // the frame of b's press is the last within the block
        QCOMPARE(cut.docTime(cut.startUs), 920000); // shown from the block's first record
        QCOMPARE(cut.videoFramesAt(919000).first, -1);
        QCOMPARE(cut.docTime(cut.packets[cut.videoFramesAt(920000).second].ptsUs), 920000);
        // The whole recording as a block: the normalized records (the first at 60 s), the same frames at their keys.
        select(w, 0, 0);
        MediaClip all;
        QVERIFY(MediaClip::parse(w.documentForSave(true).webcam, all));
        QCOMPARE(all.docTime(all.packets[all.videoFramesAt(61000000).second].ptsUs), 61000000); // b's press

        // Deleting "a": the clip follows, b's press keeps its frame; "Отменить" brings the time back.
        cam.setCapture(false);
        const int bPress = 2;
        const qint64 before = clip.packets[clip.videoFramesAt(DocTime::of(w.m_doc.records, bPress)).second].ptsUs;
        select(w, 0, 1);
        w.deleteSelection();
        int b = -1;
        for (int i = 0; i < w.m_doc.records.size(); ++i)
            if (w.m_doc.records[i].ch == u'b' && w.m_doc.records[i].isDown())
                b = i;
        QVERIFY(b >= 0);
        QCOMPARE(clip.packets[clip.videoFramesAt(DocTime::of(w.m_doc.records, b)).second].ptsUs, before);
        w.undo();
        QCOMPARE(clip.packets[clip.videoFramesAt(DocTime::of(w.m_doc.records, bPress)).second].ptsUs, before);

        // Saved and opened again: the same clip.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("cam.tsf"));
        QVERIFY(Tsf::write(path, w.documentForSave(false), true));
        const QByteArray bytes = clip.serialize();
        MainWindow again;
        QVERIFY(again.openFile(path));
        QCOMPARE(again.m_clip.serialize(), bytes);
        QVERIFY(!again.m_damaged->isVisibleTo(&again)); // the video is outside the original's signature
        // Looked at: the frames of the recording, not the camera.
        again.showVideo();
        QVERIFY(!again.m_video->isLive());
        again.scrollKlavogramToDocTime(DocTime::of(again.m_doc.records, bPress));
        QVERIFY(!again.m_video->image().isNull());
        // The slider: from the first frame to the last; moved by hand, the klavogram and the frame follow.
        const auto [spanFrom, spanTo] = again.videoSpan();
        QCOMPARE(spanFrom, again.m_clip.firstUs());
        QCOMPARE(again.m_video->m_slider->maximum(), int((spanTo - spanFrom) / 1000));
        const qint64 cPress = DocTime::of(again.m_doc.records, 4);
        again.m_video->m_slider->setValue(int((cPress - spanFrom) / 1000));
        QVERIFY(std::abs(again.klavogramDocTimeUs() - cPress) <= 1000);
        QCOMPARE(again.m_videoShownUs, cPress);
        QCOMPARE(again.m_clip.docTime(again.m_clip.packets[again.m_video->m_decoded].ptsUs), cPress);
        // Playing goes on from there; let go after a drag while playing, it plays from the new place.
        again.playVideo(true);
        QCOMPARE(again.m_playFromUs, cPress);
        again.m_video->m_play->setChecked(true);
        emit again.m_video->m_slider->sliderPressed();
        QVERIFY(!again.m_playTimer->isActive());
        again.m_video->m_slider->setValue(int((DocTime::of(again.m_doc.records, bPress) - spanFrom) / 1000));
        emit again.m_video->m_slider->sliderReleased();
        QVERIFY(again.m_playTimer->isActive());
        QCOMPARE(again.m_playFromUs, DocTime::of(again.m_doc.records, bPress));
        again.m_video->m_play->setChecked(false);
        QVERIFY(!again.m_playTimer->isActive());
        // Played to the end: it stops there; ▶ again starts from the beginning.
        again.m_video->m_play->setChecked(true);
        again.m_playFromUs = spanTo + 1;
        again.playTick();
        QVERIFY(!again.m_playTimer->isActive());
        QVERIFY(!again.m_video->m_play->isChecked());
        QCOMPARE(again.m_videoShownUs, spanTo);
        QCOMPARE(again.m_video->m_slider->value(), again.m_video->m_slider->maximum());
        again.m_video->m_play->setChecked(true);
        QCOMPARE(again.m_playFromUs, spanFrom);
        again.m_video->m_play->setChecked(false);
        // Video going on after a save is not unsaved records (no question when closing).
        w.m_unsaved = false;
        cam.addVideo(picture, 3100000);
        cam.drain();
        QVERIFY(!w.m_unsaved);
#else
        QSKIP("built without the webcam");
#endif
    }

    void webcamStartsWithTyping()
    {
#ifdef TS_HAVE_WEBCAM
        // The camera records what is typed into the document: not when a file is only opened, from its first key; the
        // corner button tells it (re/webcam.md).
        MainWindow w;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        WebcamRecorder &cam = *w.m_webcam;
        cam.setDevicesEnabled(false);
        WebcamRecorder::Settings s;
        s.video = true;
        cam.setSettings(s);
        w.startCapture();
        QVERIFY(w.m_capture->isChecked());
        QVERIFY(!cam.isRecording());
        QVERIFY(w.m_cameraButton->text().isEmpty()); // the camera alone
        w.showVideo();
        QVERIFY(!w.m_video->isLive()); // nothing recorded: no camera
        QVERIFY(!w.m_video->m_message.isEmpty());
        QVERIFY(!w.m_video->m_play->isEnabled()); // and nothing to play, seek or save
        QVERIFY(!w.m_video->m_slider->isEnabled());
        QVERIFY(!w.m_video->m_save->isEnabled());
        HookEvent e;
        e.timeUs = 1000;
        e.flags = quint32('A') << 16 | 'A' | KeyRecord::HasChar;
        e.ch = u'a';
        e.chars = 1;
        w.keyEvent(e);
        QVERIFY(cam.isRecording());
        QVERIFY(w.m_video->isLive());
        QCOMPARE(w.m_cameraButton->text(), QStringLiteral("0:00"));
        QVERIFY(w.m_cameraKey.contains(Look::colors().proofBad.name())); // red
        // "Остановить запись камеры": the keys after it have no video; "Продолжить": from the next key again.
        emit w.m_cameraButton->menu()->aboutToShow();
        QVERIFY(w.m_cameraStop->isVisible());
        w.m_cameraStop->trigger();
        QVERIFY(!cam.isRecording());
        e.timeUs += 100000;
        w.keyEvent(e);
        QVERIFY(!cam.isRecording());
        emit w.m_cameraButton->menu()->aboutToShow();
        QCOMPARE(w.m_cameraStop->text(), QStringLiteral("Продолжить запись камеры"));
        w.m_cameraStop->trigger();
        QVERIFY(cam.isRecording()); // at once: the document is being typed into
        emit w.m_cameraButton->menu()->aboutToShow();
        QVERIFY(w.m_cameraStop->isVisible());
        QCOMPARE(w.m_cameraStop->text(), QStringLiteral("Остановить запись камеры"));
        QVERIFY(w.m_cameraKey.contains(Look::colors().proofBad.name())); // red again
        w.m_cameraStop->trigger();
        // Another document: the camera waits again (not stopped any more); one with video shows it, green.
        w.clear();
        QVERIFY(!cam.isRecording());
        QVERIFY(!w.m_video->isLive());
        TsfDocument doc;
        KeyRecord a;
        a.flags = quint32('A') << 16 | 'A' | KeyRecord::HasChar;
        a.ch = u'a';
        doc.records = {a};
        MediaClip clip;
        clip.addStream(MediaStream::video(320, 240));
        clip.packets = {{0, true, 0, QByteArray("x")}, {0, false, 61000000, QByteArray("y")}};
        doc.webcam = clip.serialize();
        w.setDocument(doc, QStringLiteral("v"), false);
        QVERIFY(!cam.isRecording());
        QCOMPARE(w.m_cameraButton->text(), QStringLiteral("1:01"));
        QVERIFY(w.m_video->m_play->isEnabled());
        QVERIFY(w.m_video->m_slider->isEnabled());
        QVERIFY(w.m_cameraKey.contains(Look::colors().proofOk.name())); // green
        // Ts: OFF stops a recording.
        w.keyEvent(e);
        QVERIFY(cam.isRecording());
        w.m_capture->setChecked(false);
        QVERIFY(!cam.isRecording());
#else
        QSKIP("built without the webcam");
#endif
    }

    void webcamCustomQuality()
    {
#ifdef TS_HAVE_WEBCAM
        // "Своё": the values kept, held within what the encoder takes; it starts from the quality chosen before.
        WebcamRecorder::Settings s;
        s.quality = WebcamRecorder::Custom;
        s.custom = {1281, 50, 60, 1};
        s.save();
        const WebcamRecorder::Settings back = WebcamRecorder::Settings::load();
        QCOMPARE(back.quality, int(WebcamRecorder::Custom));
        QVERIFY((back.preset() == WebcamRecorder::Preset{1280, 120, 30, 10}));
        WebcamRecorder::Settings good;
        good.quality = WebcamRecorder::Good;
        QVERIFY((good.preset() == WebcamRecorder::preset(WebcamRecorder::Good)));

        VideoPropertiesDialog d;
        d.setSettings(good);
        QVERIFY(!d.m_custom->isVisibleTo(&d));
        d.m_quality->setCurrentIndex(WebcamRecorder::Custom);
        QVERIFY(d.m_custom->isVisibleTo(&d));
        QVERIFY((d.settings().preset() == WebcamRecorder::preset(WebcamRecorder::Good)));
        // The bitrate follows the size and the rate of frames, until one of the user's own is typed.
        using P = WebcamRecorder::Preset;
        QCOMPARE(P::suggestedKbps(320, 240, 10), 40);
        QCOMPARE(P::suggestedKbps(640, 360, 15), 100);
        QCOMPARE(P::suggestedKbps(640, 480, 24), 200);
        QCOMPARE(P::suggestedKbps(1920, 1080, 30), 900);
        QVERIFY(d.m_autoKbps->isChecked());
        d.m_width->setValue(1920);
        d.m_height->setValue(1080);
        d.m_fps->setValue(30);
        QCOMPARE(d.m_kbps->value(), 900);
        d.m_kbps->setValue(300);
        QVERIFY(!d.m_autoKbps->isChecked());
        d.m_fps->setValue(25);
        QCOMPARE(d.m_kbps->value(), 300);
        d.m_autoKbps->setChecked(true);
        QCOMPARE(d.m_kbps->value(), P::suggestedKbps(1920, 1080, 25));
        d.m_fps->setValue(30);
        d.m_kbps->setValue(300);
        QVERIFY(d.m_estimate->text().contains(QStringLiteral("3")));
        QVERIFY(d.m_estimate->text().contains(QStringLiteral("·"))); // too few bits for such a frame
        QVERIFY((d.settings().preset() == WebcamRecorder::Preset{1920, 1080, 30, 300}));
        // A recording with it: the stream of the clip has the size chosen.
        MainWindow w;
        w.m_webcam->setDevicesEnabled(false);
        w.m_webcam->setSettings([&] {
            WebcamRecorder::Settings v = d.settings();
            v.video = true;
            return v;
        }());
        w.m_webcam->setCapture(true);
        QVERIFY(w.m_webcam->isRecording());
        const int stream = w.m_clip.streamOf(MediaStream::Video);
        QVERIFY(stream >= 0);
        QCOMPARE(w.m_clip.streams[stream].a, 1920);
        QCOMPARE(w.m_clip.streams[stream].b, 1080);
        w.m_webcam->setCapture(false);
#else
        QSKIP("built without the webcam");
#endif
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
        // The whole text comes back from where the selection put it, the same as made anew (and sorted).
        for (int round = 0; round < 3; ++round) {
            const auto fresh = ExtraStats::rows(
                ExtraStats::collect(w.m_model, x->m_fingers, 0, int(w.m_model.size()), ExtraStats::Words,
                                    x->m_template->currentText(), x->filter()),
                false, x->m_sort.mode, x->m_sort.descending);
            QCOMPARE(x->rows().size(), fresh.size());
            for (int i = 0; i < fresh.size(); ++i) {
                QCOMPARE(x->rows().at(i).text, fresh.at(i).text);
                QCOMPARE(x->rows().at(i).speed, fresh.at(i).speed);
                QCOMPARE(x->rows().at(i).value, fresh.at(i).value);
            }
            select(w, 0, 40 + round);
            QVERIFY(x->rows().size() < words);
            select(w, 0, 0);
        }

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
            QVERIFY(!r.text.contains(QChar(u'о')));

        x->m_list->selectAll();
        x->copy();
        QCOMPARE(QApplication::clipboard()->text().count(QLatin1Char(' ')), int(x->rows().size()) - 1);

        // A recalculated text of the same size is analysed again; the strings of the list are its rows'.
        x->m_filterOn[2]->setChecked(false);
        x->m_kinds[ExtraStats::Words]->setChecked(true);
        const QVector<ExtraStats::Row> before = x->rows();
        for (KeyRecord &r : w.m_doc.records)
            r.dtUs = r.dtUs / 2 + 1;
        w.recalculate();
        QCOMPARE(x->rows().size(), before.size());
        QVERIFY(x->rows().first().speed > before.first().speed);
        QCOMPARE(x->m_list->model()->index(2, 1).data().toString(), x->rows().at(2).text);
        QCOMPARE(x->m_list->model()->index(2, 0).data().toString(), ExtraStats::formatSpeed(x->rows().at(2).speed, QLocale()));

        // The fingers follow the scheme, also when a scheme of the same name is edited.
        w.m_schemes = FingerZoneSchemes(); // not stored next to the test
        FingerZones zones = FingerZones::standard();
        zones.setReadOnly(false);
        const int scan = w.m_model.flags.first() & 0x7f;
        zones.assign(scan, 7, false);
        w.m_schemes.store(QStringLiteral("Моя"), zones);
        w.m_fingers->addItem(QStringLiteral("Моя"));
        w.m_fingers->setCurrentText(QStringLiteral("Моя"));
        QCOMPARE(x->m_fingers, fingerSeries(w.m_model, zones));
        zones.assign(scan, 2, false);
        w.m_schemes.store(QStringLiteral("Моя"), zones);
        w.zonesChanged();
        QCOMPARE(x->m_fingers, fingerSeries(w.m_model, zones));
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

    void openingSwitchesCaptureOff()
    {
        MainWindow w;
        HookEvent e;
        e.timeUs = 1000;
        e.flags = quint32(0x41) << 16 | 0x1E | KeyRecord::HasChar;
        e.ch = u'a';
        e.chars = 1;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        w.keyEvent(e); // 'a' is held
        QVERIFY(w.m_capture->isChecked());
        // A .tsf: recording goes off, and the held key of the recording left behind adds nothing to the file.
        QVERIFY(w.openFile(golden("обыка.tsf")));
        QVERIFY(!w.m_capture->isChecked());
        TsfDocument file;
        QCOMPARE(Tsf::read(golden("обыка.tsf"), file), Tsf::ReadError::None);
        QCOMPARE(w.m_doc.records.size(), file.records.size());
        QVERIFY(!w.m_unsaved);
        // A journal leaves recording as it is (0x42abcc).
        QTemporaryDir dir;
        JournalWriter journal(dir.path());
        KeyRecord r;
        r.flags = quint32(0x41) << 16 | 0x1E | KeyRecord::HasChar;
        r.ch = u'a';
        QVERIFY(journal.append(r));
        journal.close();
        w.m_capture->setChecked(true);
        QVERIFY(w.openFile(journal.path()));
        QVERIFY(w.m_capture->isChecked());
    }

    void askToSaveOnExit()
    {
        QSettings().remove(QStringLiteral("AskSaveOnExit"));
        MainWindow w;
        HookEvent e;
        e.timeUs = 1000;
        e.flags = quint32(0x41) << 16 | 0x1E | KeyRecord::HasChar;
        e.ch = u'a';
        e.chars = 1;
        if (QApplication::activeWindow())
            QSKIP("the test window got the focus");
        QVERIFY(w.askToSave()); // nothing recorded: no question
        w.keyEvent(e);
        QVERIFY(w.m_unsaved);
        // The answer to the question, given once it is shown.
        const auto answer = [](QMessageBox::StandardButton button, bool never) {
            QTimer::singleShot(0, [=] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(box);
                QVERIFY(box->checkBox());
                box->checkBox()->setChecked(never);
                box->button(button)->click();
            });
        };
        answer(QMessageBox::Cancel, true);
        QVERIFY(!w.askToSave()); // stays; "do not ask" with Cancel is not remembered
        QVERIFY(QSettings().value(QStringLiteral("AskSaveOnExit"), true).toBool());
        answer(QMessageBox::Discard, true);
        QVERIFY(w.askToSave());
        QVERIFY(!QSettings().value(QStringLiteral("AskSaveOnExit"), true).toBool());
        QVERIFY(w.askToSave()); // not asked any more
        // The same switch in the settings.
        {
            SettingsDialog d;
            auto *ask = d.findChildren<QCheckBox *>().at(0);
            for (QCheckBox *c : d.findChildren<QCheckBox *>())
                if (c->text() == QStringLiteral("Спрашивать о сохранении при выходе"))
                    ask = c;
            QVERIFY(!ask->isChecked());
            ask->setChecked(true);
            d.save();
        }
        QVERIFY(QSettings().value(QStringLiteral("AskSaveOnExit")).toBool());
        // Saved, opened or cleared: nothing to ask about.
        w.clear();
        QVERIFY(w.askToSave());
        QSettings().remove(QStringLiteral("AskSaveOnExit"));
    }

    void stampRecorder()
    {
        TsfDocument doc;
        StampRecorder s;
        struct Request { QByteArray imprint; int service; StampRecorder::Done done; };
        QList<Request> sent;
        s.setSend([&](const QByteArray &imprint, int service, StampRecorder::Done done) {
            sent.append({imprint, service, std::move(done)});
        });
        QSignalSpy changed(&s, &StampRecorder::changed);
        s.attach(&doc);
        const auto key = [&](char16_t c, quint32 dtUs, bool up = false) {
            KeyRecord r;
            r.dtUs = dtUs;
            r.flags = quint32(c - u'a' + 0x41) << 16 | 0x1E | KeyRecord::HasChar | (up ? quint32(KeyRecord::KeyUp) : 0u);
            r.ch = c;
            doc.records.append(r);
            s.recordsAdded();
        };
        key(u'a', 1000); // off: nothing goes out
        QVERIFY(sent.isEmpty());
        s.setEnabled(true);
        s.attach(&doc);
        // The first key of a session is stamped at once.
        key(u'b', 1000);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent[0].service, 0);
        Stamps::Chain chain;
        for (const KeyRecord &r : doc.records)
            chain.push(r);
        key(u'b', 1000, true); // while it is on its way: no second request
        QCOMPARE(sent.size(), 1);
        sent[0].done(fakeToken(sent[0].imprint), {});
        QCOMPARE(doc.stamps.size(), 1);
        QCOMPARE(doc.stamps[0].end, 2);
        // The chain v3: the records salted with the stamp's key, in leaves; no video here.
        QCOMPARE(doc.stamps[0].version, Stamps::kVersion);
        QCOMPARE(doc.stamps[0].key.size(), Stamps::kKeySize);
        QCOMPARE(chain.hashV3({}, 0, 2, doc.stamps[0].delayMs, doc.stamps[0].key, {}, 0), sent[0].imprint);
        QCOMPARE(changed.size(), 1);
        // Within the interval nothing; the first key after it - a stamp of everything since.
        key(u'c', 4000000);
        QCOMPARE(sent.size(), 1);
        key(u'c', 6000000, true);
        QCOMPARE(sent.size(), 2);
        // An authority fails: the next one at once, with a request of its own; then every other one.
        const int services = int(StampRecorder::services().size());
        QVERIFY(services >= 3);
        sent[1].done({}, QStringLiteral("down"));
        QCOMPARE(sent.size(), 3);
        QCOMPARE(sent[2].service, 1);
        sent[2].done(QByteArray("garbage"), {});
        for (int i = 2; i < services; ++i) {
            QCOMPARE(sent.size(), i + 2);
            QCOMPARE(sent.last().service, i);
            sent.last().done({}, QStringLiteral("down"));
        }
        QCOMPARE(sent.size(), services + 1); // all failed: it waits
        QVERIFY(s.lastError().contains(QStringLiteral("down")));
        QCOMPARE(doc.stamps.size(), 1);
        // Recording stops: what is left is stamped now (the authority that answered last goes first).
        s.captureChanged(false);
        QCOMPARE(sent.size(), services + 2);
        QCOMPARE(sent.last().service, 0);
        // The records replaced while it was on its way (an edit, another document): the answer is dropped.
        s.attach(&doc);
        sent.last().done(fakeToken(sent.last().imprint), {});
        QCOMPARE(doc.stamps.size(), 1);
        // After the last key, the idle stamp.
        s.captureChanged(true);
        key(u'd', 1000);
        QCOMPARE(sent.size(), services + 3); // a new session starts with a stamp
        sent.last().done(fakeToken(sent.last().imprint), {});
        QCOMPARE(doc.stamps.size(), 2);
        key(u'd', 1000, true);
        QCOMPARE(sent.size(), services + 3);
        QTRY_COMPARE_WITH_TIMEOUT(sent.size(), services + 4, Stamps::kIdleMs + 2000);
    }

    void stampFlush()
    {
        // Before saving: a stamp of everything there is, video too (re/stamps.md).
        TsfDocument doc;
        MediaClip clip;
        clip.addStream(MediaStream::video(320, 240));
        StampRecorder s;
        struct Request { QByteArray imprint; StampRecorder::Done done; };
        QList<Request> sent;
        s.setSend([&](const QByteArray &imprint, int, StampRecorder::Done done) { sent.append({imprint, std::move(done)}); });
        QSignalSpy flushed(&s, &StampRecorder::flushed);
        s.setClip(&clip);
        s.setEnabled(true);
        s.attach(&doc, true);
        const auto key = [&](char16_t c, bool up = false) {
            KeyRecord r;
            r.dtUs = 100000;
            r.flags = quint32(c - u'a' + 0x41) << 16 | 0x1E | KeyRecord::HasChar | (up ? quint32(KeyRecord::KeyUp) : 0u);
            r.ch = c;
            doc.records.append(r);
            s.recordsAdded();
        };
        const auto frame = [&](qint64 us) {
            clip.packets.append({0, clip.packets.isEmpty(), us, QByteArray(10, 'x')});
            s.packetsAdded();
        };
        // Nothing at all: held as it is. Video, but nothing typed since the document came: nothing to stamp it with.
        s.flush();
        QCOMPARE(flushed.size(), 1);
        QCOMPARE(flushed.last()[0].toBool(), true);
        frame(0);
        s.flush();
        QCOMPARE(flushed.size(), 2);
        QCOMPARE(flushed.last()[0].toBool(), false);
        key(u'a');
        QCOMPARE(sent.size(), 1);
        // A flush while a stamp is on its way: after it, a stamp of all there is.
        s.flush();
        QCOMPARE(sent.size(), 1);
        sent[0].done(fakeToken(sent[0].imprint), {});
        QCOMPARE(sent.size(), 2);
        QCOMPARE(flushed.size(), 2);
        frame(100000); // came after the request: the save leaves it out
        sent[1].done(fakeToken(sent[1].imprint), {});
        QCOMPARE(flushed.size(), 3);
        QCOMPARE(flushed.last()[0].toBool(), true);
        QCOMPARE(s.cover().records, 1);
        QCOMPARE(s.cover().packets, 1);
        QCOMPARE(s.cover().stamps, 2);
        QCOMPARE(doc.stamps.last().mediaEnd, 1);
        // Another one: the frame left out, then nothing more - at once, with no request.
        s.flush();
        QCOMPARE(sent.size(), 3);
        sent[2].done(fakeToken(sent[2].imprint), {});
        QCOMPARE(s.cover().packets, 2);
        s.flush();
        QCOMPARE(sent.size(), 3);
        QCOMPARE(flushed.size(), 5);
        QCOMPARE(flushed.last()[0].toBool(), true);
        // All the authorities down: false.
        key(u'a', true); // within the interval: no stamp of its own
        QCOMPARE(sent.size(), 3);
        s.flush();
        QCOMPARE(sent.size(), 4);
        for (int i = 0; i < int(StampRecorder::services().size()); ++i)
            sent.last().done({}, QStringLiteral("down"));
        QCOMPARE(flushed.size(), 6);
        QCOMPARE(flushed.last()[0].toBool(), false);
        // Recording stops; the frames the encoder still gives are stamped a moment later, not in 10 s.
        s.captureChanged(true);
        const qsizetype stopped = sent.size();
        s.captureChanged(false); // what is left is stamped at once
        QCOMPARE(sent.size(), stopped + 1);
        sent.last().done(fakeToken(sent.last().imprint), {});
        const qsizetype before = sent.size();
        frame(500000);
        QTRY_VERIFY_WITH_TIMEOUT(sent.size() > before, 2500);
    }

    void stampLive()
    {
        // The real authorities over the network (TS_STAMP_LIVE=1, ~15 s): records made up on the clock - no input
        // is sent anywhere - stamped as they come, then checked as a file would be.
        if (qEnvironmentVariableIntValue("TS_STAMP_LIVE") != 1)
            QSKIP("TS_STAMP_LIVE=1 to ask the real time stamping authorities");
        TsfDocument doc;
        StampRecorder s;
        s.setEnabled(true);
        s.attach(&doc);
        QElapsedTimer clock;
        clock.start();
        qint64 last = 0;
        const QString text = QStringLiteral("stamps over the network ");
        for (int round = 0; round < 2; ++round)
            for (const QChar c : text) {
                for (const bool up : {false, true}) {
                    QTest::qWait(up ? 70 : 180);
                    const qint64 now = clock.nsecsElapsed() / 1000;
                    KeyRecord r;
                    r.dtUs = quint32(now - last);
                    last = now;
                    const quint8 vk = c == u' ' ? 0x20 : quint8(c.toUpper().unicode());
                    r.flags = quint32(vk) << 16 | 0x1E | KeyRecord::HasChar | (up ? quint32(KeyRecord::KeyUp) : 0u);
                    r.ch = c.unicode();
                    doc.records.append(r);
                    s.recordsAdded();
                }
            }
        QTRY_VERIFY_WITH_TIMEOUT(!doc.stamps.isEmpty() && doc.stamps.last().end == doc.records.size() && !s.busy(),
                                 Stamps::kIdleMs + 20000);
        QVERIFY2(s.lastError().isEmpty(), qPrintable(s.lastError()));
        const Stamps::Report r = Stamps::verify(Recalc::normalized(doc.records), doc.stamps, doc.stampCertificates);
        qInfo("stamps %d, confirmed %d of %d, drift %lld ms, %s", r.stamps, r.confirmed, r.records,
              qint64(r.driftMs), qPrintable(r.authorities.join(u',')));
        QCOMPARE(r.bad, 0);
        QVERIFY(r.stamps >= 3);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
    }

    void blockKeepsStamps()
    {
        // "Сохранить блок" of a stamped recording with video: the block carries the stamps of what it holds, the
        // rest of the recording hidden (re/stamps.md, chain v2); the video is under them.
        MainWindow w;
        QVERIFY(w.openFile(golden("stamps/video.tsf")));
        QCOMPARE(w.stampReport().status, Stamps::Report::Status::Confirmed);
        select(w, 20, 40);
        int revealed = -1;
        const TsfDocument block = w.documentForSave(true, &revealed);
        QVERIFY(!block.stamps.isEmpty());
        QVERIFY(revealed <= 14);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("block.tsf"));
        QVERIFY(Tsf::write(path, block, true));
        MainWindow again;
        QVERIFY(again.openFile(path));
        QVERIFY(!again.m_model.text.isEmpty() && w.m_model.text.contains(again.m_model.text.left(10)));
        const Stamps::Report r = again.stampReport();
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
        QVERIFY(r.hiddenBeforeUs > 0 && r.hiddenAfterUs > 0);
        QVERIFY(r.packets > 0);
        QCOMPARE(r.packetsStamped, r.packets);
    }

    void proofButton()
    {
        MainWindow w;
        QVERIFY(w.m_proofButton->isHidden()); // no stamps, stamping off
        QVERIFY(w.openFile(QStringLiteral(TS_GOLDEN_DIR "/stamps/stamped.tsf")));
        QTRY_VERIFY(!w.m_proofButton->isHidden());
        QCOMPARE(w.m_proofButton->text(), QStringLiteral("✓ 100 %"));
        // A deletion in the last part voids its stamp: the time of the rest stays confirmed.
        const int pos = w.m_model.positionOfElement(70);
        QTextCursor c = w.m_text->textCursor();
        c.setPosition(pos);
        c.setPosition(pos + 2, QTextCursor::KeepAnchor);
        w.m_text->setTextCursor(c);
        w.deleteSelection();
        QCOMPARE(w.m_doc.stamps.size(), 3);
        QVERIFY(w.m_doc.stamps[2].voided);
        QTRY_VERIFY(w.m_proofButton->text() != QStringLiteral("✓ 100 %") && w.m_proofButton->text().startsWith(QStringLiteral("✓ ")));
        w.undo();
        QVERIFY(!w.m_doc.stamps[2].voided);
        QTRY_COMPARE(w.m_proofButton->text(), QStringLiteral("✓ 100 %"));
        // Saved and read back, the stamps are the same.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("s.tsf"));
        QVERIFY(Tsf::write(path, w.m_doc, true));
        TsfDocument back;
        QCOMPARE(Tsf::read(path, back), Tsf::ReadError::None);
        QCOMPARE(Stamps::verify(Recalc::normalized(back.records), back.stamps, back.stampCertificates).status,
                 Stamps::Report::Status::Confirmed);
    }

    void exportTables()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        const TableExport::Table keys = w.keyTable();
        QCOMPARE(keys.header.size(), 3);
        QCOMPARE(keys.header.at(1), QStringLiteral("Пауза, мс"));
        QVERIFY(keys.rows.size() > 250);
        QVERIFY(!keys.rows.first().at(1).isValid()); // the first press has no pause
        QVERIFY(keys.rows.at(1).at(1).toDouble() > 1.0);
        QVERIFY(keys.rows.at(1).at(2).toDouble() > 1.0);
        // Exactly the numbers the list shows (in the locale of the list).
        for (const QLocale &loc : {QLocale(), QLocale(QLocale::Russian), QLocale(QLocale::English)}) {
            const auto shown = KeyList::rows(w.m_model.klav, loc, -std::numeric_limits<double>::infinity(),
                                             std::numeric_limits<double>::infinity(), -1, 3, w.m_model.keyNames);
            QCOMPARE(shown.size(), keys.rows.size());
            for (int i = 0; i < shown.size(); ++i) {
                QCOMPARE(keys.rows[i].at(0).toString(), shown[i].key);
                for (const auto &[cell, text] : {std::pair{keys.rows[i].at(1), shown[i].pause},
                                                 std::pair{keys.rows[i].at(2), shown[i].duration}}) {
                    bool ok = false;
                    const double v = loc.toDouble(text, &ok);
                    QCOMPARE(cell.isValid(), ok);
                    if (ok)
                        QCOMPARE(cell.toDouble(), v);
                }
            }
        }

        const QString xlsx = m_settings.filePath(QStringLiteral("keys.xlsx"));
        QVERIFY(TableExport::write(xlsx, keys, true, QLocale(QLocale::Russian)));
        QXlsx::Document doc(xlsx);
        QVERIFY(doc.load());
        QCOMPARE(doc.read(1, 1).toString(), keys.header.at(0));
        QCOMPARE(doc.read(3, 1).toString(), keys.rows.at(1).at(0).toString());
        QCOMPARE(doc.read(3, 2).toDouble(), keys.rows.at(1).at(1).toDouble());

        const QString csv = QString::fromUtf8(TableExport::toCsv(keys, QLocale(QLocale::Russian)).mid(3));
        const QStringList lines = csv.split(QStringLiteral("\r\n"));
        QCOMPARE(lines.at(0), QStringLiteral("Клавиша;Пауза, мс;Длительность, мс"));
        QCOMPARE(lines.at(1).count(QLatin1Char(';')), 2);
        QVERIFY(lines.at(2).contains(QLatin1Char(',')));
        QVERIFY(!lines.at(2).contains(QLatin1Char('.')));
        // A cell with the separator or quotes is quoted.
        TableExport::Table t;
        t.header = {QStringLiteral("a;b"), QStringLiteral("say \"hi\"")};
        t.rows.append({1, 2.5});
        QCOMPARE(QString::fromUtf8(TableExport::toCsv(t, QLocale(QLocale::Russian)).mid(3)),
                 QStringLiteral("\"a;b\";\"say \"\"hi\"\"\"\r\n1;2,5\r\n"));
        QCOMPARE(QString::fromUtf8(TableExport::toCsv(t, QLocale::c()).mid(3)),
                 QStringLiteral("a;b,\"say \"\"hi\"\"\"\r\n1,2.5\r\n"));

        w.showExtraStats();
        w.m_extra->m_averages->setChecked(true);
        const TableExport::Table extra = w.extraTable();
        QCOMPARE(extra.header.size(), 3);
        QCOMPARE(extra.rows.size(), w.m_extra->rows().size());
        QCOMPARE(extra.rows.first().at(0).toString(), w.m_extra->rows().first().text);
        QCOMPARE(extra.rows.first().at(2).toInt(), w.m_extra->rows().first().value);
    }

    void graphShownAtStart_data()
    {
        QTest::addColumn<QSize>("window");
        QTest::addColumn<int>("text");
        QTest::addColumn<int>("klav");
        QTest::newRow("defaults") << QSize() << -1 << -1;
        QTest::newRow("saved") << QSize(876, 579) << 120 << 200;
        QTest::newRow("large") << QSize(1600, 1000) << 120 << 200;
        QTest::newRow("tall panes") << QSize(1600, 1000) << 300 << 400;
    }

    void graphShownAtStart()
    {
        // The graph is there from the start, whatever the window was (the panes are laid out once the window is).
        QFETCH(QSize, window);
        QFETCH(int, text);
        QFETCH(int, klav);
        {
            QSettings s;
            s.clear();
            if (window.isValid()) {
                QWidget probe;
                probe.resize(window);
                s.setValue(QStringLiteral("WindowGeometry"), probe.saveGeometry());
                s.setValue(QStringLiteral("TextWinHeight"), text);
                s.setValue(QStringLiteral("KlavWinHeight"), klav);
            }
        }
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCoreApplication::processEvents();
        const QList<int> sizes = w.m_leftSplit->sizes();
        QVERIFY2(!w.m_graphFolded, qPrintable(QStringLiteral("%1 %2 %3").arg(sizes[0]).arg(sizes[1]).arg(sizes[2])));
        QVERIFY(w.m_graph->isVisible());
        QVERIFY(sizes[1] >= 100);
        // The panes as saved when they fit; else the text and the klavogram give way in proportion.
        if (text > 0) {
            if (text + klav + 100 <= sizes[0] + sizes[1] + sizes[2]) {
                QCOMPARE(sizes[0], text);
                QCOMPARE(sizes[2], klav);
            } else {
                QVERIFY(sizes[0] < text && sizes[2] < klav);
                QVERIFY(qAbs(sizes[0] * klav - sizes[2] * text) <= text + klav);
            }
        }
        QSettings().clear();
    }

    void graphFoldedByHandStays()
    {
        // Folded by hand and closed: folded again (the port's key GraphFolded).
        QSettings().clear();
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            const QList<int> sizes = w.m_leftSplit->sizes();
            w.m_leftSplit->setSizes({sizes[0], 80, sizes[2] + sizes[1] - 80});
            w.graphPaneResized();
            QVERIFY(w.m_graphFolded);
            w.saveSettings();
        }
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.m_graphFolded);
        QVERIFY(!w.m_graph->isVisible());
        QSettings().clear();
    }

    void foldedGraph()
    {
        MainWindow w;
        w.resize(876, 579);
        w.show();
        QVERIFY(w.openFile(golden("обыка.tsf")));
        QList<int> sizes = w.m_leftSplit->sizes();
        QVERIFY(sizes[1] >= 100);
        // The graph pane is dragged below 100 px: only its scroll bar is left, for the klavogram.
        w.m_leftSplit->setSizes({sizes[0], 80, sizes[2] + sizes[1] - 80});
        w.graphPaneResized();
        QVERIFY(w.m_graphFolded);
        QVERIFY(!w.m_graph->isVisible());
        QVERIFY(!w.m_legend->isVisible());
        QCOMPARE(w.m_leftSplit->sizes()[1], w.m_graphScroll->sizeHint().height());
        QVERIFY(w.m_graphScroll->maximum() > 1000); // milliseconds of the recording
        w.m_graphScroll->setValue(3000);
        QCOMPARE(int(w.m_klav->scrollMs()), 3000);
        QVERIFY(w.m_keys->rowCount() > 0);
        // Dragged back up: the graph returns.
        sizes = w.m_leftSplit->sizes();
        w.m_leftSplit->setSizes({sizes[0], 150, sizes[2] - 150 + sizes[1]});
        w.graphPaneResized();
        QVERIFY(!w.m_graphFolded);
        QVERIFY(w.m_graph->isVisible());
    }

private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
