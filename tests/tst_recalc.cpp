#include "core/KeyName.h"
#include "core/MainStats.h"
#include "core/TsfFile.h"

#include <QDir>
#include <QTest>

namespace {

// Builds a press/release pair for a character key.
struct Typist
{
    KeyRecords recs;

    void press(quint8 vk, char16_t ch, quint32 dtUs, quint32 extra = 0)
    {
        KeyRecord r;
        r.dtUs = dtUs;
        r.flags = quint32(vk) << 16 | extra | (ch ? KeyRecord::HasChar | KeyRecord::SingleChar : KeyRecord::NoChar);
        r.ch = ch;
        recs.append(r);
    }
    void release(quint8 vk, quint32 dtUs)
    {
        KeyRecord r;
        r.dtUs = dtUs;
        r.flags = quint32(vk) << 16 | KeyRecord::KeyUp | KeyRecord::NoChar;
        recs.append(r);
    }
    // Types a character: press, 50 ms hold, release; `gapUs` before the press.
    void type(char16_t ch, quint32 gapUs, quint32 extra = 0)
    {
        const quint8 vk = ch == u' ' ? 0x20 : ch == u'\b' ? 0x08 : quint8(QChar(ch).toUpper().unicode() & 0xFF);
        press(vk, ch, gapUs, extra);
        release(vk, 50000);
    }
    void text(const QString &s, quint32 gapUs)
    {
        for (QChar c : s)
            type(c.unicode(), gapUs);
    }
};

// 'x' for every press whose character gets erased.
QString erasedMask(const KeyRecords &document)
{
    const KeyRecords recs = Recalc::normalized(document);
    const QVector<bool> erased = Recalc::erasedRecords(recs);
    QString s;
    for (int i = 0; i < recs.size(); ++i)
        if (recs[i].isDown())
            s += erased[i] ? QLatin1Char('x') : QLatin1Char('.');
    return s;
}

} // namespace

class TstRecalc : public QObject
{
    Q_OBJECT
private slots:
    void keyNames()
    {
        QCOMPARE(keyDisplayName(0x41 << 16 | KeyRecord::HasChar, u'a'), QStringLiteral("a"));
        QCOMPARE(keyDisplayName(0x08 << 16 | KeyRecord::HasChar, 8), QStringLiteral("[BackSpace]"));
        QCOMPARE(keyDisplayName(0x08 << 16 | KeyRecord::HasChar | KeyRecord::Ctrl, 0x7F),
                 QStringLiteral("[Ctrl+BackSpace]"));
        QCOMPARE(keyDisplayName(0x0D << 16 | KeyRecord::HasChar, 13), QStringLiteral("\r"));
        QCOMPARE(keyDisplayName(0xA0 << 16 | KeyRecord::NoChar, 0), QStringLiteral("[LShift]"));
        QCOMPARE(keyDisplayName(0xA2 << 16 | KeyRecord::NoChar | KeyRecord::Ctrl, 0), QStringLiteral("[LCtrl]"));
        QCOMPARE(keyDisplayName(0x7B << 16 | KeyRecord::NoChar, 0), QStringLiteral("[F12]"));
        QCOMPARE(keyDisplayName(0x43 << 16 | KeyRecord::HasChar | KeyRecord::Ctrl | KeyRecord::Alt, u'c'),
                 QStringLiteral("[Ctrl+Alt+c]"));
        QCOMPARE(keyDisplayName(0xFF << 16 | KeyRecord::NoChar, 0), QStringLiteral("[Unrecognized key]"));
    }

    void backspaceMarksPreviousChars()
    {
        Typist t;
        t.text(QStringLiteral("abc"), 100000);
        t.type(u'\b', 100000);
        t.type(u'\b', 100000);
        t.text(QStringLiteral("d"), 100000);
        QCOMPARE(erasedMask(t.recs), QStringLiteral(".xx...")); // a b c BS BS d
    }

    void ctrlBackspaceKeepsSpaceBeforeWord()
    {
        Typist t;
        t.text(QStringLiteral("one two  "), 100000);
        t.press(0x08, 0x7F, 100000, KeyRecord::Ctrl);
        t.release(0x08, 50000);
        // "one two  " + Ctrl+BS: trailing spaces and "two" go, the space before it stays.
        QCOMPARE(erasedMask(t.recs), QStringLiteral("....xxxxx."));
    }

    void ctrlBackspaceStopsAtPunctuation()
    {
        Typist t;
        t.text(QStringLiteral("ab.,"), 100000);
        t.press(0x08, 0x7F, 100000, KeyRecord::Ctrl);
        t.release(0x08, 50000);
        QCOMPARE(erasedMask(t.recs), QStringLiteral("..xx."));
    }

    void repeatedModifierIsDropped()
    {
        Typist t;
        t.press(0xA0, 0, 0);             // LShift
        t.press(0xA0, 0, 30000);         // auto-repeat, dropped; its dt moves on
        t.press(0x41, u'A', 20000);
        t.release(0x41, 10000);
        t.release(0xA0, 10000);
        const KeyRecords n = Recalc::normalized(t.recs);
        QCOMPARE(t.recs.size(), 5); // the document is not touched
        QCOMPARE(n.size(), 4);
        QCOMPARE(n[0].dtUs, 60000000u);
        QCOMPARE(n[1].dtUs, 50000u);
    }

    void textAndFragments()
    {
        Typist t;
        t.text(QStringLiteral("ab"), 100000);
        t.text(QStringLiteral("cd"), 2000000); // 2 s > 500 ms: 'c' starts a fragment, 'd' does too
        t.text(QStringLiteral("e"), 100000);
        RecalcOptions opt;
        opt.splitMs = 500;
        const TextModel m = Recalc::run(t.recs, opt);
        QCOMPARE(m.text, QStringLiteral("ab‡c‡de"));
        QCOMPARE(m.size(), 5);
        QCOMPARE(m.fragmentStarts, (QVector<int>{0, 2, 3}));
        QCOMPARE(m.pauses[1], 150.0f); // 50 ms hold + 100 ms gap
        QCOMPARE(m.pauses[2], 2050.0f);
        QCOMPARE(m.pauses[4], 150.0f);
        QCOMPARE(m.fragmentAt(1), std::make_pair(0, 2));
        QCOMPARE(m.fragmentAt(5), std::make_pair(3, 5));

        opt.byPauses = true;
        const TextModel p = Recalc::run(t.recs, opt);
        QCOMPARE(p.text, QStringLiteral("ab\n————————\nc\n————————\nde"));
        // Fragment around "e".
        const auto [b, e] = Stats::range(p, p.text.size() - 1, 0, true);
        QCOMPARE(b, 3);
        QCOMPARE(e, 5);
    }

    void simpleStats()
    {
        Typist t;
        t.text(QStringLiteral("abcde"), 150000); // 200 ms per char including the 50 ms hold
        const TextModel m = Recalc::run(t.recs, {});
        const MainStats s = Stats::compute(m, 0, m.size(), 500, false);
        QCOMPARE(s.chars, 5);
        QCOMPARE(s.erased, 0);
        QCOMPARE(s.sumAll, 800.0);
        QCOMPARE(s.avgAll, 200.0);
        QCOMPARE(s.gross0, 300.0); // 4 intervals in 800 ms
        QCOMPARE(s.gross, 375.0);
        QCOMPARE(s.arrAll, 0.0);
        QCOMPARE(s.holdAvgUs, 50000);
        QCOMPARE(s.spm0, 300.0f);
        const QStringList f = Stats::format(s, QLocale(QLocale::Russian));
        QCOMPARE(f[MainStats::Chars], QStringLiteral("5 (5)"));
        QCOMPARE(f[MainStats::TotalTime], QStringLiteral("0,800с"));
        QCOMPARE(f[MainStats::AvgHold], QStringLiteral("50,000 мс"));
        QCOMPARE(f[MainStats::SpeedGross], QStringLiteral("375,00 (300,00)"));
        QCOMPARE(f[MainStats::Arrhythmia], QStringLiteral("0,00% (0,00)%"));
    }

    void formatTime()
    {
        const QLocale ru(QLocale::Russian);
        QCOMPARE(Stats::formatTime(3250, 3, ru), QStringLiteral("3,250с"));
        QCOMPARE(Stats::formatTime(65250, 3, ru), QStringLiteral("1м 5,250с"));
        QCOMPARE(Stats::formatTime(3725000, 3, ru), QStringLiteral("1ч 2м 5,000с"));
    }

    void goldenFilesRecalculate_data()
    {
        QTest::addColumn<QString>("file");
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")}))
            QTest::newRow(qPrintable(f)) << QStringLiteral(TS_GOLDEN_DIR "/") + f;
    }

    void goldenFilesRecalculate()
    {
        QFETCH(QString, file);
        TsfDocument d;
        QCOMPARE(Tsf::read(file, d), Tsf::ReadError::None);
        const TextModel m = Recalc::run(d.records, {});
        QVERIFY(m.size() > 10);
        QCOMPARE(m.pauses.size(), m.size());
        QVERIFY(std::is_sorted(m.mapPos.begin(), m.mapPos.end()));
        const MainStats s = Stats::compute(m, 0, m.size(), 500, false);
        QVERIFY(s.chars > 0);
        QVERIFY(s.gross > 0);
    }
};

QTEST_GUILESS_MAIN(TstRecalc)
#include "tst_recalc.moc"
