// Recorder: hook events into records (re/recording.md).

#include "core/Recorder.h"

#include <QTest>

namespace {

constexpr quint32 kUp = KeyRecord::KeyUp | KeyRecord::NoChar;

// A key event as the hook reports it: the scan code here is just the low bits of the VK.
HookEvent key(qint64 ms, quint8 vk, bool down, char16_t ch = 0, quint8 scan = 0)
{
    HookEvent e;
    e.timeUs = ms * 1000;
    e.flags = quint32(vk) << 16 | (scan ? scan : quint8(vk & 0x7f));
    if (!down)
        e.flags |= kUp;
    else if (ch)
        e.flags |= KeyRecord::HasChar;
    else
        e.flags |= KeyRecord::NoChar;
    e.ch = ch;
    e.chars = ch ? 1 : 0;
    return e;
}

} // namespace

class TstRecorder : public QObject
{
    Q_OBJECT
private slots:
    void recordsPressesAndReleases()
    {
        Recorder rec;
        KeyRecords recs;
        const RecorderSettings s;
        QVERIFY(rec.handle(key(1000, 'A', true, u'a'), s, {}, recs).recorded);
        QVERIFY(rec.handle(key(1080, 'A', false), s, {}, recs).recorded);
        QVERIFY(rec.handle(key(1200, 'B', true, u'b'), s, {}, recs).recorded);
        QCOMPARE(recs.size(), 3);
        QCOMPARE(recs[0].dtUs, 0u);
        QCOMPARE(recs[1].dtUs, 80000u);
        QCOMPARE(recs[2].dtUs, 120000u);
        QCOMPARE(recs[2].ch, u'b');
        QVERIFY(recs[1].isUp());
    }

    void ignoredEventsDoNotBreakTheTime()
    {
        Recorder rec;
        KeyRecords recs;
        RecorderSettings s;
        rec.handle(key(1000, 'A', true, u'a'), s, {}, recs);
        // A release without a press, a key typed in the program's own window, a key with the capture off.
        QVERIFY(!rec.handle(key(1100, 'Z', false), s, {}, recs).recorded);
        Recorder::Context own;
        own.ownWindow = true;
        QVERIFY(!rec.handle(key(1200, 'C', true, u'c'), s, own, recs).recorded);
        s.capture = false;
        QVERIFY(!rec.handle(key(1300, 'D', true, u'd'), s, {}, recs).recorded);
        // The release of the recorded press gets through even now; the time runs from that press.
        QVERIFY(rec.handle(key(1500, 'A', false), s, {}, recs).recorded);
        QCOMPARE(recs.size(), 2);
        QCOMPARE(recs[1].dtUs, 500000u);
    }

    void hotkeys()
    {
        Recorder rec;
        KeyRecords recs;
        RecorderSettings s;
        QVERIFY(rec.handle(key(0, Vk::F8, true), s, {}, recs).recorded);
        Recorder::Outcome o = rec.handle(key(10, Vk::F9, true), s, {}, recs);
        QVERIFY(!o.recorded);
        QCOMPARE(o.setCapture, std::optional<bool>(true));
        rec.handle(key(20, Vk::F8, false), s, {}, recs);
        o = rec.handle(key(30, Vk::F8, true), s, {}, recs); // F9 is still held
        QCOMPARE(o.setCapture, std::optional<bool>(false));

        rec.handle(key(40, Vk::LControl, true), s, {}, recs);
        o = rec.handle(key(50, Vk::LWin, true), s, {}, recs);
        QVERIFY(o.clear && !o.recorded);
        s.globalClear = false;
        o = rec.handle(key(60, Vk::LWin, true), s, {}, recs);
        QVERIFY(!o.clear && o.recorded);
        o = rec.handle(key(70, Vk::RShift, true), s, {}, recs);
        QVERIFY(o.liveReset && o.recorded);

        // Ctrl+Alt+O by scan codes.
        Recorder rec2;
        rec2.handle(key(0, Vk::LControl, true, 0, 0x1D), s, {}, recs);
        rec2.handle(key(1, Vk::LMenu, true, 0, 0x38), s, {}, recs);
        QVERIFY(rec2.handle(key(2, 'O', true, 0, 0x18), s, {}, recs).toggleLive);
    }

    void autoComments()
    {
        Recorder rec;
        KeyRecords recs;
        RecorderSettings s;
        s.autoComments = true;
        Recorder::Context c;
        c.foregroundWindow = 7;
        int asked = 0;
        c.comment = [&asked] { return QStringLiteral("comment %1").arg(++asked); };
        rec.handle(key(0, 'A', true, u'a'), s, c, recs);      // a new window
        rec.handle(key(100, 'A', false), s, c, recs);
        rec.handle(key(11000, 'B', true, u'b'), s, c, recs);  // after more than 10 s
        c.foregroundWindow = 8;
        rec.handle(key(11100, 'B', false), s, c, recs);       // another window
        QCOMPARE(recs[0].comment, QStringLiteral("comment 1"));
        QVERIFY(recs[1].comment.isEmpty());
        QCOMPARE(recs[2].comment, QStringLiteral("comment 2"));
        QCOMPARE(recs[3].comment, QStringLiteral("comment 3"));
    }

    void deadKeys()
    {
        Recorder rec;
        KeyRecords recs;
        const RecorderSettings s;
        HookEvent dead = key(0, 0xDE, true, u'´');
        dead.flags |= KeyRecord::DeadKey;
        dead.chars = -1;
        rec.handle(dead, s, {}, recs);
        // It combines with the next letter: one character, marked.
        rec.handle(key(100, 'E', true, u'é'), s, {}, recs);
        QVERIFY(recs[0].flags & KeyRecord::DeadKey);
        QVERIFY(recs[1].flags & KeyRecord::SingleChar);

        // It does not combine: the dead key becomes a character of its own.
        rec.handle(dead, s, {}, recs);
        HookEvent two = key(300, 'T', true, u't');
        two.chars = 2;
        two.firstCh = u'´';
        rec.handle(two, s, {}, recs);
        QVERIFY(!(recs[2].flags & KeyRecord::DeadKey));
        QCOMPARE(recs[2].ch, u'´');
        QCOMPARE(recs[3].ch, u't');
        QVERIFY(!(recs[3].flags & KeyRecord::SingleChar));
    }

    void liveStatistics()
    {
        Recorder rec;
        KeyRecords recs;
        RecorderSettings s;
        s.liveVisible = true;
        s.splitMs = 500;
        qint64 t = 0;
        auto type = [&](quint8 vk, char16_t ch, int gapMs) {
            t += gapMs;
            rec.handle(key(t, vk, true, ch), s, {}, recs);
            rec.handle(key(t + 10, vk, false), s, {}, recs);
        };
        type('A', u'a', 0);
        type('B', u'b', 100);
        type('C', u'c', 100);
        QCOMPARE(rec.live().count, 3u);
        QCOMPARE(rec.live().timeUs, quint64(200000));
        QCOMPARE(rec.live().speed, 2.0f * 6e7f / 200000.0f); // 600 per minute
        // Two BackSpaces in a row are one error.
        type(Vk::Back, 0, 100);
        type(Vk::Back, 0, 100);
        QCOMPARE(rec.live().count, 1u);
        QCOMPARE(rec.live().errorPercent, 0.0f); // fewer than two characters left
        type('D', u'd', 100);
        QCOMPARE(rec.live().count, 2u);
        QCOMPARE(rec.live().errorPercent, 50.0f);
        // A pause is left out of the time and of the count of intervals.
        type('E', u'e', 2000);
        QCOMPARE(rec.live().count, 3u);
        QCOMPARE(rec.live().timeUs, quint64(500000));
        QCOMPARE(rec.live().speed, 1.0f * 6e7f / 500000.0f);
        // With "split by pauses" a pause starts over.
        s.byPauses = true;
        type('F', u'f', 2000);
        QCOMPARE(rec.live().count, 1u);
        QCOMPARE(rec.live().timeUs, quint64(0));
    }
};

QTEST_APPLESS_MAIN(TstRecorder)
#include "tst_recorder.moc"
