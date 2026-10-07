// The webcam recording's container and its lines in a .tsf (re/webcam.md): Z85, MediaClip, cut, Tsf round trip.

#include "core/MediaClip.h"
#include "core/TsfFile.h"
#include "core/WebmWriter.h"
#include "core/Z85.h"

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

namespace {

// A clip of 10 s: video at 10 fps with a key frame every 2 s, audio packets of 20 ms. pts from 0, origin 0.
MediaClip sample()
{
    MediaClip c;
    const int v = c.addStream(MediaStream::video(320, 240));
    const int a = c.addStream(MediaStream::audio(48000, 1));
    qint64 nextAudio = 0;
    for (int i = 0; i < 100; ++i) {
        const qint64 t = qint64(i) * 100000;
        while (nextAudio <= t) {
            c.packets.append({quint8(a), true, nextAudio, QByteArray("a") + QByteArray::number(nextAudio)});
            nextAudio += MediaClip::kOpusPacket;
        }
        c.packets.append({quint8(v), i % 20 == 0, t, QByteArray("v") + QByteArray::number(i)});
    }
    return c;
}

QList<qint64> ptsOf(const MediaClip &c, MediaStream::Kind kind)
{
    QList<qint64> out;
    for (const MediaPacket &p : c.packets)
        if (c.streams[p.stream].kind == kind)
            out.append(p.ptsUs);
    return out;
}

} // namespace

class TstClip : public QObject
{
    Q_OBJECT

private slots:
    void z85()
    {
        // The test vector of RFC 32.
        const QByteArray bytes = QByteArray::fromHex("864FD26FB559F75B");
        QCOMPARE(Z85::encode(bytes), QStringLiteral("HelloWorld"));
        bool ok = false;
        QCOMPARE(Z85::decode(u"HelloWorld", &ok), bytes);
        QVERIFY(ok);
        // Padding to whole words.
        QCOMPARE(Z85::encode(QByteArray("\x86\x4f", 2)).size(), 5);
        QCOMPARE(Z85::decode(Z85::encode(QByteArray("\x86\x4f", 2))).left(2), QByteArray("\x86\x4f", 2));
        // Every byte value survives.
        QByteArray all;
        for (int i = 0; i < 256; ++i)
            all.append(char(i));
        QCOMPARE(Z85::decode(Z85::encode(all)), all);
        // Not Z85.
        Z85::decode(u"Hello Worl", &ok);
        QVERIFY(!ok);
        Z85::decode(u"Hell", &ok);
        QVERIFY(!ok);
        Z85::decode(u"#####", &ok); // above 2^32
        QVERIFY(!ok);
        // No characters the lines of a .tsf could not carry.
        const QString text = Z85::encode(all);
        for (QChar c : text)
            QVERIFY(c.unicode() > 32 && c.unicode() < 127 && c != u'"' && c != u'\\' && c != u';');
    }

    void serialize()
    {
        MediaClip c = sample();
        c.originUs = -1234;
        c.startUs = 500000;
        c.shiftMs = -40;
        MediaClip back;
        QVERIFY(MediaClip::parse(c.serialize(), back));
        QCOMPARE(back.streams, c.streams);
        QCOMPARE(back.originUs, c.originUs);
        QCOMPARE(back.startUs, c.startUs);
        QCOMPARE(back.shiftMs, c.shiftMs);
        QCOMPARE(back.packets.size(), c.packets.size());
        for (int i = 0; i < c.packets.size(); ++i) {
            QCOMPARE(back.packets[i].stream, c.packets[i].stream);
            QCOMPARE(back.packets[i].key, c.packets[i].key);
            QCOMPARE(back.packets[i].ptsUs, c.packets[i].ptsUs);
            QCOMPARE(back.packets[i].data, c.packets[i].data);
        }
        // Broken or cut short: not a clip.
        QVERIFY(!MediaClip::parse(c.serialize().chopped(1), back));
        QVERIFY(!MediaClip::parse(QByteArray("TSMX") + c.serialize().mid(4), back));
        // The bytes a stamp hashes: the header of the packet and its data.
        QCOMPARE(MediaClip::packetBytes({1, true, 0x0102, "xy"}),
                 QByteArray::fromHex("0101" "0201000000000000" "02000000") + "xy");
    }

    void addStream()
    {
        MediaClip c;
        QCOMPARE(c.addStream(MediaStream::video(640, 360)), 0);
        QCOMPARE(c.addStream(MediaStream::video(640, 360)), 0);
        QCOMPARE(c.addStream(MediaStream::audio(48000, 1)), 1);
        QCOMPARE(c.addStream(MediaStream::video(320, 240)), 2); // the camera changed: a new stream
        QCOMPARE(c.streamOf(MediaStream::Video), 2);
        QCOMPARE(c.streamOf(MediaStream::Audio), 1);
    }

    void cut()
    {
        const MediaClip c = sample();
        // [3.05 s, 6.5 s]: video from the key frame at 2 s up to the frame at 6.5 s, audio from 3.05 − 0.08 − 0.02.
        const MediaClip b = c.cut(3050000, 6500000);
        QCOMPARE(b.originUs, 3050000);
        QCOMPARE(b.startUs, 3050000);
        const QList<qint64> video = ptsOf(b, MediaStream::Video);
        QCOMPARE(video.first(), 2000000);
        QCOMPARE(video.last(), 6500000);
        QCOMPARE(video.size(), 46);
        const QList<qint64> audio = ptsOf(b, MediaStream::Audio);
        QCOMPARE(audio.first(), 2960000); // the packet [2.96, 2.98) ends after 3.05 − 0.08
        QCOMPARE(audio.last(), 6500000);
        // The packets are those of the clip, byte for byte (the stamps hash them).
        for (const MediaPacket &p : b.packets) {
            bool found = false;
            for (const MediaPacket &q : c.packets)
                if (q.ptsUs == p.ptsUs && q.stream == p.stream) {
                    QCOMPARE(MediaClip::packetBytes(q), MediaClip::packetBytes(p));
                    found = true;
                }
            QVERIFY(found);
        }
        // In the cut clip's time: visible from 0, the frame at 0 is the one at 3.0 s, decoded from the key at 2 s.
        QCOMPARE(b.firstUs(), 10000); // the first visible packet: audio at 3.06 s
        const auto [key, frame] = b.videoFramesAt(0);
        QCOMPARE(b.packets[key].ptsUs, 2000000);
        QCOMPARE(b.packets[frame].ptsUs, 3000000);
        // Nothing before the start is shown.
        QCOMPARE(b.videoFramesAt(-100000).first, -1);
        // A cut of a cut: the times add up.
        const MediaClip bb = b.cut(1000000, 2000000);
        QCOMPARE(bb.originUs, 4050000);
        QCOMPARE(ptsOf(bb, MediaStream::Video).first(), 4000000);
        QCOMPARE(ptsOf(bb, MediaStream::Video).last(), 5000000);
        // Before the first key frame: from the first key frame there is.
        MediaClip late = sample();
        late.packets.removeIf([](const MediaPacket &p) { return p.ptsUs < 1000000; });
        QCOMPARE(ptsOf(late.cut(0, 3000000), MediaStream::Video).first(), 2000000);
    }

    void framesAt()
    {
        MediaClip c = sample();
        auto [key, frame] = c.videoFramesAt(4550000);
        QCOMPARE(c.packets[key].ptsUs, 4000000);
        QCOMPARE(c.packets[frame].ptsUs, 4500000);
        // The correction by hand: the pictures 300 ms later.
        c.shiftMs = 300;
        frame = c.videoFramesAt(4550000).second;
        QCOMPARE(c.packets[frame].ptsUs, 4200000);
        c.shiftMs = 0;
        // Long after the last frame (recording paused): nothing.
        QCOMPARE(c.videoFramesAt(9900000 + 900000).second >= 0, true);
        QCOMPARE(c.videoFramesAt(9900000 + 1100000).first, -1);
        // The origin moved (the start of the document was deleted).
        c.moveOrigin(2000000);
        QCOMPARE(c.packets[c.videoFramesAt(2550000).second].ptsUs, 4500000);
    }

    void tsf()
    {
        TsfDocument doc;
        KeyRecord r;
        r.dtUs = 1000;
        r.flags = 0x01410000 | 0x1E;
        r.ch = u'a';
        doc.records = {r};
        const MediaClip c = sample();
        doc.webcam = c.serialize();
        // Large enough for several lines.
        MediaClip big = c;
        big.packets[1].data = QByteArray(200000, 'x');
        doc.webcam = big.serialize();
        const QStringList lines = Tsf::serialize(doc, true);
        int dataLines = 0;
        for (const QString &l : lines) {
            if (l.startsWith(u"WebcamData")) {
                ++dataLines;
                QVERIFY(l.size() <= 65535 + 16);
            }
            // The original reads each line with sscanf("%x %s") into a buffer on its stack: a long line must not
            // start with a hexadecimal digit (the short keys of the original do, harmlessly).
            if (l.size() > 200 && !QRegularExpression(QStringLiteral("^[0-9A-F]{8} ")).match(l).hasMatch())
                QVERIFY2(!QStringLiteral("0123456789abcdefABCDEF").contains(l[0]), qPrintable(l.left(20)));
        }
        QVERIFY(dataLines >= 4);
        TsfDocument back = Tsf::parse(lines);
        QCOMPARE(back.records.size(), 1);
        QCOMPARE(back.webcam, doc.webcam);
        QVERIFY(!back.webcamDamaged);
        QVERIFY(back.signatureValid); // the video is not under the original's signature

        // A line lost: the video is dropped, the rest is read.
        QStringList broken = lines;
        broken.removeIf([](const QString &l) { return l.startsWith(u"WebcamData2="); });
        back = Tsf::parse(broken);
        QVERIFY(back.webcam.isEmpty());
        QVERIFY(back.webcamDamaged);
        QCOMPARE(back.records.size(), 1);
        // A character changed: the hash does not match.
        broken = lines;
        for (QString &l : broken)
            if (l.startsWith(u"WebcamData1="))
                l[20] = l[20] == u'0' ? u'1' : u'0';
        QVERIFY(Tsf::parse(broken).webcamDamaged);

        // Through a file.
        QTemporaryDir dir;
        const QString path = dir.filePath("v.tsf");
        QVERIFY(Tsf::write(path, doc, true));
        TsfDocument fromFile;
        QCOMPARE(Tsf::read(path, fromFile), Tsf::ReadError::None);
        QCOMPARE(fromFile.webcam, doc.webcam);
    }

    void webm()
    {
        // The structure: an EBML header of DocType webm, then a segment.
        const QByteArray w = Webm::write(sample());
        QVERIFY(w.startsWith(QByteArray::fromHex("1A45DFA3")));
        QVERIFY(w.contains("webm"));
        QVERIFY(w.contains("V_AV1"));
        QVERIFY(w.contains("A_OPUS"));
        QVERIFY(w.contains("OpusHead"));
        QVERIFY(w.contains(QByteArray::fromHex("18538067")));
        // av1C from a sequence header OBU (type 1, with a size): profile 0, level 8 (4.0), tier 0.
        // seq_profile=0, still=0, reduced=0, timing=0, delay=0, op_cnt=0, idc=0 (12 bits), level=8, tier=0.
        const QByteArray seq = QByteArray::fromHex("0a04") + QByteArray::fromHex("00000040");
        const QByteArray config = Webm::av1Config(QByteArray::fromHex("1200") + seq);
        QCOMPARE(config.size(), 4 + seq.size());
        QCOMPARE(quint8(config[0]), quint8(0x81));
        QCOMPARE(quint8(config[1]), quint8(8));
        QVERIFY(Webm::av1Config(QByteArray::fromHex("1200")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstClip)
#include "tst_clip.moc"
