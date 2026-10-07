// The webcam's codecs (re/webcam.md): YUV conversions, AV1 (libaom realtime), Opus.

#include "core/MediaClip.h"
#include "core/TsfFile.h"
#include "core/WebmWriter.h"
#include "media/Av1Codec.h"
#include "media/Capture.h"
#include "media/OpusCodec.h"
#include "media/Yuv.h"

#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QTest>

#include <cmath>

namespace {

// A test picture: a gradient with a square moving across it.
QImage scene(int w, int h, int t)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(x * 255 / w, y * 255 / h, 128));
    QPainter p(&img);
    p.fillRect((t * 7) % (w - 40), h / 3, 40, 40, Qt::white);
    p.fillRect(w / 2, (t * 5) % (h - 30), 30, 30, Qt::black);
    return img;
}

double psnr(const I420Frame &a, const I420Frame &b)
{
    if (a.width != b.width || a.height != b.height)
        return 0;
    double se = 0;
    const int n = a.width * a.height;
    for (int i = 0; i < n; ++i) {
        const int d = a.y()[i] - b.y()[i];
        se += d * d;
    }
    if (se == 0)
        return 99;
    return 10 * std::log10(255.0 * 255.0 * n / se);
}

} // namespace

class TstMedia : public QObject
{
    Q_OBJECT

private slots:
    void yuvRoundTrip()
    {
        const QImage img = scene(64, 48, 0);
        const I420Frame f = Yuv::fromImage(img);
        QCOMPARE(f.width, 64);
        QCOMPARE(f.height, 48);
        QCOMPARE(f.data.size(), 64 * 48 * 3 / 2);
        const QImage back = Yuv::toImage(f);
        // Chroma is halved: colours of a smooth picture come back close.
        int worst = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x) {
                const QRgb a = img.pixel(x, y), b = back.pixel(x, y);
                worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                                  std::abs(qBlue(a) - qBlue(b))});
            }
        QVERIFY2(worst <= 12, qPrintable(QString::number(worst)));
    }

    void cameraFormats()
    {
        // A 4×2 picture in NV12 and YUYV gives the same I420.
        const uchar y[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        const uchar uv[4] = {100, 200, 110, 210};
        const I420Frame a = Yuv::fromNv12(y, 4, uv, 4, 4, 2);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(a.u()), 2), QByteArray("\x64\x6e"));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(a.v()), 2), QByteArray("\xc8\xd2"));
        const uchar yuyv[16] = {10, 100, 20, 200, 30, 110, 40, 210, 50, 100, 60, 200, 70, 110, 80, 210};
        const I420Frame b = Yuv::fromYuyv(yuyv, 8, 4, 2);
        QCOMPARE(b.data, a.data);
        // Odd sizes lose the last column and row.
        QCOMPARE(Yuv::fromYuyv(yuyv, 8, 3, 2).width, 2);
    }

    void scale()
    {
        const I420Frame src = Yuv::fromImage(scene(640, 480, 3));
        const I420Frame small = Yuv::scaled(src, 320, 180); // 4:3 → 16:9: cropped top and bottom
        QCOMPARE(small.width, 320);
        QCOMPARE(small.height, 180);
        const I420Frame big = Yuv::scaled(small, 640, 360);
        QCOMPARE(big.width, 640);
        // Scaling down and up keeps the picture (a smooth one).
        QVERIFY(psnr(Yuv::scaled(src, 640, 360), big) > 25);
    }

    void av1RoundTrip()
    {
        Av1Settings s;
        s.width = 320;
        s.height = 240;
        s.fps = 15;
        s.kbps = 60;
        Av1Encoder enc;
        QString error;
        QVERIFY2(enc.open(s, &error), qPrintable(error));
        Av1Decoder dec;
        QList<EncodedFrame> packets;
        QList<I420Frame> sources;
        qint64 bytes = 0;
        for (int i = 0; i < 90; ++i) { // 6 s at 15 fps
            const I420Frame f = Yuv::fromImage(scene(320, 240, i));
            sources.append(f);
            const auto out = enc.encode(f, qint64(i) * 66667);
            QCOMPARE(out.size(), 1); // no lag: a packet per frame, at once
            QCOMPARE(out[0].ptsUs, qint64(i) * 66667);
            bytes += out[0].data.size();
            packets.append(out[0]);
        }
        QVERIFY(packets[0].key);
        // A key frame every 4 s: at frames 0 and 60.
        for (int i = 1; i < packets.size(); ++i)
            QCOMPARE(packets[i].key, i == 60);
        // Around the bitrate asked for (the first key frame is large).
        const double kbps = bytes * 8.0 / 6 / 1000;
        QVERIFY2(kbps < 200, qPrintable(QString::number(kbps)));
        double worst = 99;
        QList<I420Frame> decoded;
        for (int i = 0; i < packets.size(); ++i) {
            const I420Frame d = dec.decode(packets[i].data);
            QCOMPARE(d.width, 320);
            QCOMPARE(d.height, 240);
            worst = std::min(worst, psnr(d, sources[i]));
            decoded.append(d);
        }
        QVERIFY2(worst > 22, qPrintable(QString::number(worst)));

        // Decoding from the second key frame gives the same pictures as from the start: a cut copies packets.
        Av1Decoder late;
        for (int i = 60; i < packets.size(); ++i)
            QCOMPARE(late.decode(packets[i].data).data, decoded[i].data);
    }

    void av1Speed()
    {
        // The realtime encoder keeps up with the camera with room to spare (Debug builds are slower).
        Av1Settings s;
        Av1Encoder enc;
        QVERIFY(enc.open(s));
        QList<I420Frame> frames;
        for (int i = 0; i < 8; ++i)
            frames.append(Yuv::fromImage(scene(s.width, s.height, i)));
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 45; ++i)
            enc.encode(frames[i % frames.size()], qint64(i) * 66667);
        const double msPerFrame = t.elapsed() / 45.0;
        qInfo("AV1 640x360: %.1f ms per frame", msPerFrame);
        QVERIFY(msPerFrame < 66);
    }

    void webm()
    {
        // 3 s of video and audio through the encoders into a clip, out as WebM.
        MediaClip clip;
        Av1Settings s;
        s.width = 320;
        s.height = 240;
        Av1Encoder enc;
        QVERIFY(enc.open(s));
        OpusAudioEncoder audio;
        const int v = clip.addStream(MediaStream::video(s.width, s.height));
        const int a = clip.addStream(MediaStream::audio(48000, 1));
        QVector<float> tone(4800);
        for (int i = 0; i < 45; ++i) {
            const qint64 t = qint64(i) * 66667;
            for (const EncodedFrame &f : enc.encode(Yuv::fromImage(scene(s.width, s.height, i)), t))
                clip.packets.append({quint8(v), f.key, f.ptsUs, f.data});
            for (int k = 0; k < tone.size(); ++k)
                tone[k] = float(0.3 * std::sin(2 * M_PI * 330 * (i * 3200 + k) / 48000.0));
            for (const EncodedFrame &f : audio.push(tone.constData(), 3200, 1, 48000, t))
                clip.packets.append({quint8(a), true, f.ptsUs, f.data});
        }
        const QByteArray config = Webm::av1Config(clip.packets[0].data);
        QVERIFY(config.size() > 4);
        QCOMPARE(quint8(config[0]), quint8(0x81));
        QCOMPARE(quint8(config[1]) >> 5, 0); // profile 0 (main): 8 bits, 4:2:0
        const QByteArray w = Webm::write(clip);
        QVERIFY(w.contains("V_AV1"));
        // For a look by hand: ffprobe / a browser.
        if (const QByteArray out = qgetenv("TS_MEDIA_WEBM"); !out.isEmpty()) {
            QFile f(QString::fromLocal8Bit(out));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(w);
        }
        // A .tsf with this video (tsstat --extract-video by hand).
        if (const QByteArray out = qgetenv("TS_MEDIA_TSF"); !out.isEmpty()) {
            TsfDocument doc;
            doc.webcam = clip.serialize();
            QVERIFY(Tsf::write(QString::fromLocal8Bit(out), doc, true));
        }
    }

    void captureDevices()
    {
        // Listing does not switch a camera on; on CI machines there are none.
        for (const CaptureDevice &d : Camera::devices())
            qInfo("camera: %s", qPrintable(d.name));
        for (const CaptureDevice &d : Microphone::devices())
            qInfo("microphone: %s", qPrintable(d.name));
    }

    void opusRoundTrip()
    {
        // 1 s of a 440 Hz tone, stereo 44.1 kHz, pushed in odd pieces.
        OpusAudioEncoder enc;
        QVERIFY(enc.isOpen());
        const int rate = 44100;
        QVector<float> pcm(rate * 2);
        for (int i = 0; i < rate; ++i)
            pcm[2 * i] = pcm[2 * i + 1] = float(0.5 * std::sin(2 * M_PI * 440 * i / rate));
        QList<EncodedFrame> packets;
        int pos = 0;
        for (int piece : {441, 1000, 3333, 777}) {
            while (pos + piece <= rate) {
                packets += enc.push(pcm.constData() + 2 * pos, piece, 2, rate, qint64(pos) * 1000000 / rate);
                pos += piece;
            }
        }
        QVERIFY(packets.size() >= 48);
        for (int i = 1; i < packets.size(); ++i)
            QCOMPARE(packets[i].ptsUs - packets[i - 1].ptsUs, OpusAudioEncoder::kFrameUs);
        qint64 bytes = 0;
        for (const auto &p : packets)
            bytes += p.data.size();
        QVERIFY2(bytes < 4000, qPrintable(QString::number(bytes))); // 16 kbit/s
        OpusAudioDecoder dec;
        double energy = 0;
        int n = 0;
        for (int i = 5; i < packets.size(); ++i) { // after the warm-up
            const QVector<qint16> out = dec.decode(packets[i].data);
            QCOMPARE(out.size(), OpusAudioEncoder::kFrame);
            for (qint16 s : out) {
                energy += double(s) * s;
                ++n;
            }
        }
        const double rms = std::sqrt(energy / n) / 32768;
        QVERIFY2(rms > 0.25 && rms < 0.45, qPrintable(QString::number(rms))); // 0.5 / √2
        // A gap in time starts a new run.
        const auto next = enc.push(pcm.constData(), 2000, 2, rate, 5000000);
        QVERIFY(next.isEmpty() || next[0].ptsUs == 5000000);
    }
};

QTEST_GUILESS_MAIN(TstMedia)
#include "tst_media.moc"
