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
};

QTEST_APPLESS_MAIN(TstEditing)
#include "tst_editing.moc"
