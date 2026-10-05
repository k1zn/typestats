// Editing of records and copying of the text (re/editing.md).

#include "core/Editing.h"
#include "core/TsfFile.h"

#include <QTest>

namespace {

KeyRecord press(quint8 vk, char16_t ch, quint32 dt = 100)
{
    KeyRecord r;
    r.dtUs = dt;
    r.flags = quint32(vk) << 16 | (vk & 0x7f) | (ch ? KeyRecord::HasChar : KeyRecord::NoChar);
    r.ch = ch;
    return r;
}

KeyRecord release(quint8 vk, quint32 dt = 100)
{
    KeyRecord r;
    r.dtUs = dt;
    r.flags = quint32(vk) << 16 | (vk & 0x7f) | KeyRecord::KeyUp | KeyRecord::NoChar;
    return r;
}

// "aD bU" style picture of the records: the character of a press, "^x" for a release.
QString picture(const KeyRecords &recs)
{
    QStringList out;
    for (const KeyRecord &r : recs) {
        const QString key = r.ch ? QString(QChar(r.ch)) : QStringLiteral("#%1").arg(r.vk(), 2, 16, QLatin1Char('0'));
        out.append((r.isDown() ? QString() : QStringLiteral("^")) + (r.isDown() ? key : QString(QChar(r.vk()).toLower())));
    }
    return out.join(QLatin1Char(' '));
}

quint64 totalTime(const KeyRecords &recs)
{
    quint64 t = 0;
    for (const KeyRecord &r : recs)
        t += r.dtUs;
    return t;
}

// a b c typed with overlaps: a↓ b↓ a↑ c↓ b↑ c↑
KeyRecords abc()
{
    return {press('A', u'a'), press('B', u'b'), release('A'), press('C', u'c'), release('B'), release('C')};
}

} // namespace

class TstEditing : public QObject
{
    Q_OBJECT
private slots:
    void deleteKeepsForeignReleases()
    {
        // Delete "b": its press and, later, its release; the release of "a" inside the range stays.
        KeyRecords recs = abc();
        const quint64 time = totalTime(recs);
        Editing::deleteRange(recs, 1, 3);
        QCOMPARE(picture(recs), QStringLiteral("a ^a c ^c"));
        QCOMPARE(totalTime(recs), time);
        QCOMPARE(recs[1].dtUs, 200u); // the time of the dropped press went to the next record
        QCOMPARE(recs[3].dtUs, 200u);
    }

    void deleteToTheEnd()
    {
        KeyRecords recs = abc();
        Editing::deleteRange(recs, 3, recs.size());
        QCOMPARE(picture(recs), QStringLiteral("a b ^a ^b"));
    }

    void nonTextKeys()
    {
        // Shift+a, then a lone Shift, then F5, then b.
        KeyRecords recs = {press(Vk::LShift, 0), press('A', u'A'),     release('A'), release(Vk::LShift),
                           press(Vk::LShift, 0), release(Vk::LShift), press(0x74, 0), release(0x74),
                           press('B', u'b'),     release('B')};
        const quint64 time = totalTime(recs);
        Editing::removeNonText(recs, 0, recs.size());
        QCOMPARE(picture(recs), QStringLiteral("#a0 A ^a ^  b ^b"));
        QCOMPARE(totalTime(recs), time);
    }

    void labels()
    {
        KeyRecords recs = {press('X', u'x'), press('A', u'a'), release('A'), press('B', u'b'), release('B'), press('C', u'c')};
        recs[5].comment = QStringLiteral("label");
        QCOMPARE(Editing::markRange(recs, 1, 5), 1);
        QVERIFY(recs[1].flags & KeyRecord::Marked);
        QVERIFY(!(recs[2].flags & KeyRecord::Marked)); // a release
        QVERIFY(recs[3].flags & KeyRecord::Marked);
        QCOMPARE(recs[1].comment, QStringLiteral("label")); // moved from the press after the run
        QVERIFY(recs[5].comment.isEmpty());
        QCOMPARE(Editing::labelStart(recs, 4), 1);
        QCOMPARE(Editing::labelStart(recs, 5), -1);

        Editing::removeLabel(recs, 3);
        QVERIFY(!(recs[1].flags & KeyRecord::Marked));
        QVERIFY(!(recs[3].flags & KeyRecord::Marked));
        QVERIFY(recs[1].comment.isEmpty());
    }

    void copying()
    {
        // "ab", BackSpace, "c d" with the space erased by a second BackSpace, then "e".
        KeyRecords recs;
        auto type = [&recs](quint8 vk, char16_t ch) {
            recs.append(press(vk, ch));
            recs.append(release(vk));
        };
        type('A', u'a');
        type('B', u'b');
        type(Vk::Back, 0);
        type('C', u'c');
        type(Vk::Space, u' ');
        type(Vk::Back, 0);
        type('E', u'e');
        const TextModel m = Recalc::run(recs, {});
        QCOMPARE(Editing::copyText(m, 0, m.size(), false), QStringLiteral("abc e"));
        QCOMPARE(Editing::copyText(m, 0, m.size(), true), QStringLiteral("ace"));
        QCOMPARE(Editing::copyTagged(m, 0, m.size(), {}),
                 QStringLiteral("a[color=\"#ff4444\"][s]b[/s][/color][color=\"#aa4444\"]c[/color]"
                                "[color=\"#ff4444\"][s] [/s][/color][color=\"#aa4444\"]e[/color]"));
        Editing::TagOptions plain;
        plain.color = plain.colorNext = false;
        QCOMPARE(Editing::copyTagged(m, 0, m.size(), plain), QStringLiteral("a[s]b[/s]c[s] [/s]e"));

        // The record range of the selected "c": from after the record of "b" to the record of the space.
        const auto [from, to] = Editing::recordRange(m, 2, 1);
        QVERIFY(from <= m.recordOfElement(2) && to == m.recordOfElement(3));
        QCOMPARE(Editing::recordRange(m, 2, 0), (std::pair<int, int>(0, 0)));
    }
    void convertLayout()
    {
        // "ghbdtn" typed with the US layout, converted to a Russian one: "привет".
        // A fake layout: scan codes of qwerty letters give Cyrillic, ` is a dead key.
        const QString us = QStringLiteral("qwertyuiopasdfghjkl;zxcvbnm,");
        const QString ru = QStringLiteral("йцукенгшщзфывапролджячсмитьб");
        const quint8 scans[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                                0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33};
        bool deadPending = false;
        auto layout = [&](quint8 scan, bool shift, bool caps, char16_t out[2]) {
            if (scan == 0x29) { // dead accent
                out[0] = u'`';
                deadPending = true;
                return -1;
            }
            for (int k = 0; k < ru.size(); ++k)
                if (scans[k] == scan) {
                    const QChar c = shift != caps ? ru[k].toUpper() : ru[k];
                    if (deadPending) {
                        deadPending = false;
                        out[0] = u'`';
                        out[1] = c.unicode();
                        return 2;
                    }
                    out[0] = c.unicode();
                    return 1;
                }
            return 0;
        };
        auto key = [&](char c) {
            const int k = us.indexOf(QLatin1Char(c));
            KeyRecord r = press(quint8(QChar(c).toUpper().unicode()), char16_t(c));
            r.flags = (r.flags & ~quint32(KeyRecord::ScanMask)) | scans[k];
            return r;
        };
        auto noChar = [](quint8 vk, quint8 scan) {
            KeyRecord r = press(vk, 0);
            r.flags = (r.flags & ~quint32(KeyRecord::ScanMask)) | scan;
            return r;
        };
        KeyRecord accent = press(0xC0, u'`');
        accent.flags = (accent.flags & ~quint32(KeyRecord::ScanMask)) | 0x29;

        // Shift+g h b, CapsLock, d, CapsLock, t, accent, n
        KeyRecords recs = {noChar(Vk::LShift, 0x2A), key('g'), release(Vk::LShift), key('h'), key('b'),
                           noChar(Vk::Capital, 0x3A), key('d'), noChar(Vk::Capital, 0x3A), key('t'), accent, key('n')};
        Editing::convertLayout(recs, 0, recs.size(), layout, false);
        QString text;
        for (const KeyRecord &r : recs)
            if (r.isDown() && r.ch)
                text += QChar(r.ch);
        QCOMPARE(text, QStringLiteral("ПриВе`т"));
        QVERIFY(!(recs[9].flags & KeyRecord::DeadKey)); // the accent was typed on its own

        // Starting with CapsLock on; the range leaves the first record alone.
        KeyRecords two = {key('g'), key('h')};
        Editing::convertLayout(two, 1, 2, layout, true);
        QCOMPARE(QString(QChar(two[0].ch)), QStringLiteral("g"));
        QCOMPARE(QString(QChar(two[1].ch)), QStringLiteral("Р"));

        // A dead key left at the end keeps its flag.
        KeyRecords last = {accent};
        Editing::convertLayout(last, 0, 1, layout, false);
        QVERIFY(last[0].flags & KeyRecord::DeadKey);
        QCOMPARE(last[0].ch, u'`');
        deadPending = false;
    }

    // The answers for a key are asked once, but the result is that of asking every time (a dead key included).
    void convertLayoutRemembersAnswers()
    {
        bool pending = false;
        int calls = 0;
        auto layout = [&](quint8 scan, bool shift, bool caps, char16_t out[2]) {
            ++calls;
            if (scan == 0x29) {
                out[0] = u'`';
                pending = true;
                return -1;
            }
            if (scan == 0x01)
                return 0;
            const char16_t c = char16_t((shift != caps ? u'A' : u'a') + scan % 20);
            if (pending) {
                pending = false;
                out[0] = u'`';
                out[1] = c;
                return 2;
            }
            out[0] = c;
            return 1;
        };
        KeyRecords recs;
        quint32 seed = 7;
        for (int i = 0; i < 2000; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const quint8 pick = quint8(seed >> 24) % 9;
            const quint8 scan = pick == 0 ? 0x29 : pick == 1 ? 0x01 : quint8(0x10 + pick);
            KeyRecord r = press(0x41, u'x');
            r.flags = (r.flags & ~quint32(KeyRecord::ScanMask)) | scan;
            recs << r;
            if (pick == 8) { // Shift / Caps around
                KeyRecord c = press(Vk::Capital, 0);
                c.flags = (c.flags & ~quint32(KeyRecord::ScanMask)) | 0x3A;
                recs << c;
            }
        }
        KeyRecords asked = recs;
        // The reference: every record asks.
        {
            bool leftShift = false;
            bool caps = false;
            int dead = -1;
            pending = false;
            for (int i = 0; i < asked.size(); ++i) {
                KeyRecord &r = asked[i];
                if (r.vk() == Vk::Capital)
                    caps = !caps;
                r.flags &= ~quint32(KeyRecord::DeadKey);
                r.ch = 0;
                char16_t out[2] = {};
                const int n = layout(r.scan(), leftShift, caps, out);
                if (n != 0)
                    r.ch = out[n > 0 ? n - 1 : 0];
                if (n > 1 && dead >= 0) {
                    asked[dead].flags &= ~quint32(KeyRecord::DeadKey);
                    asked[dead].ch = out[0];
                }
                dead = -1;
                if (n < 0) {
                    r.flags |= KeyRecord::DeadKey;
                    dead = i;
                }
            }
        }
        const int askedCalls = calls;
        calls = 0;
        pending = false;
        Editing::convertLayout(recs, 0, recs.size(), layout, false);
        QCOMPARE(recs.size(), asked.size());
        for (int i = 0; i < recs.size(); ++i) {
            QCOMPARE(recs[i].ch, asked[i].ch);
            QCOMPARE(recs[i].flags, asked[i].flags);
        }
        QVERIFY(calls < askedCalls);
    }
};

QTEST_APPLESS_MAIN(TstEditing)
#include "tst_editing.moc"
