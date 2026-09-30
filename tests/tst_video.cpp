// The attached video: its moment for the klavogram position (re/video.md).

#include "core/Video.h"

#include <QTest>

namespace {

KeyRecord key(quint8 vk, char16_t ch, bool down, quint32 dt)
{
    KeyRecord r;
    r.dtUs = dt;
    r.flags = quint32(vk) << 16 | (vk & 0x7f) | (ch ? KeyRecord::HasChar : KeyRecord::NoChar);
    if (!down)
        r.flags |= KeyRecord::KeyUp;
    r.ch = ch;
    return r;
}

} // namespace

class TstVideo : public QObject
{
    Q_OBJECT
private slots:
    void position()
    {
        // "ab", a pause of 5 s (split at 2 s), "c".
        const KeyRecords recs = {key('A', u'a', true, 1000), key('A', 0, false, 100000),
                                 key('B', u'b', true, 100000), key('B', 0, false, 100000),
                                 key('C', u'c', true, 5000000), key('C', 0, false, 100000)};
        const TextModel m = Recalc::run(recs, {});
        QCOMPARE(m.klav.size(), 6);
        QCOMPARE(m.klav[4].tDraw, qint64(500000)); // the 5 s gap is 200 ms on the klavogram
        QCOMPARE(m.klav[4].t - m.klav[0].t, qint64(5300000));

        // First fragment: the drawing time itself, as the original.
        QCOMPARE(int(Video::positionMs(m, 150.7f, 0)), 150);
        QCOMPARE(int(Video::positionMs(m, 150.7f, 40)), 190);
        QCOMPARE(int(Video::positionMs(m, -30.f, 0)), 0);
        QCOMPARE(int(Video::positionMs(m, -30.f, 100)), 70);
        // After the pause: real time.
        QCOMPARE(int(Video::positionMs(m, 500.f, 0)), 5300);
        QCOMPARE(int(Video::positionMs(m, 550.f, -300)), 5050);
        // Past the last record the time goes on from it.
        QCOMPARE(int(Video::positionMs(m, 1000.f, 0)), 5800);

        QCOMPARE(int(Video::positionMs(TextModel(), 12.f, 5)), 17);
    }

    void frames()
    {
        QCOMPARE(int(Video::frameStartMs(150, 10.0, 400)), 100);
        QCOMPARE(int(Video::frameStartMs(99, 10.0, 400)), 0);
        QCOMPARE(int(Video::frameStartMs(100, 10.0, 400)), 100);
        QCOMPARE(int(Video::frameStartMs(5000, 10.0, 400)), 300); // the last frame
        QCOMPARE(int(Video::frameStartMs(70, 30.0, 0)), 66);     // frame 2 starts at 66.7 ms
        QCOMPARE(int(Video::frameStartMs(67, 30.0, 0)), 66);
        QCOMPARE(int(Video::frameStartMs(66, 30.0, 0)), 33);
        QCOMPARE(int(Video::frameStartMs(1001, 29.97, 0)), 967); // frame 30 starts at 1001.001 ms
        QCOMPARE(int(Video::frameStartMs(1002, 29.97, 0)), 1001);
        QCOMPARE(int(Video::frameStartMs(123, 0.0, 100)), 99);
    }

    void paths()
    {
        QCOMPARE(Video::resolvePath(QStringLiteral("C:/rec"), QStringLiteral("a.avi")), QStringLiteral("C:/rec/a.avi"));
        QCOMPARE(Video::resolvePath(QStringLiteral("C:/rec"), QStringLiteral("../v/a.avi")), QStringLiteral("C:/v/a.avi"));
        QCOMPARE(Video::resolvePath(QStringLiteral("C:/rec"), QStringLiteral("D:/x/a.avi")), QStringLiteral("D:/x/a.avi"));
        QCOMPARE(Video::resolvePath(QString(), QStringLiteral("a.avi")), QStringLiteral("a.avi"));
    }
};

QTEST_APPLESS_MAIN(TstVideo)
#include "tst_video.moc"
