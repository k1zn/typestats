#include "WebmWriter.h"

#include <QRandomGenerator>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace {

// EBML: an element is its id, the size of its payload as a variable-length integer, the payload.
QByteArray vint(quint64 v)
{
    int len = 1;
    while (len < 8 && v >= (quint64(1) << (7 * len)) - 1)
        ++len;
    QByteArray out(len, 0);
    for (int i = len - 1; i >= 0; --i) {
        out[i] = char(v & 0xFF);
        v >>= 8;
    }
    out[0] = char(quint8(out[0]) | (0x80 >> (len - 1)));
    return out;
}

QByteArray idBytes(quint32 id)
{
    QByteArray out;
    for (int shift = 24; shift >= 0; shift -= 8)
        if ((id >> shift) || !out.isEmpty())
            out.append(char((id >> shift) & 0xFF));
    return out;
}

QByteArray element(quint32 id, const QByteArray &payload)
{
    return idBytes(id) + vint(quint64(payload.size())) + payload;
}

QByteArray uintElement(quint32 id, quint64 v)
{
    QByteArray p;
    do {
        p.prepend(char(v & 0xFF));
        v >>= 8;
    } while (v);
    return element(id, p);
}

QByteArray floatElement(quint32 id, double v)
{
    char b[8];
    qToBigEndian(v, b);
    return element(id, QByteArray(b, 8));
}

QByteArray stringElement(quint32 id, const char *s)
{
    return element(id, QByteArray(s));
}

class BitReader
{
public:
    explicit BitReader(QByteArrayView b) : m_b(b) {}
    quint32 bits(int n)
    {
        quint32 v = 0;
        for (int i = 0; i < n; ++i) {
            const qsizetype byte = m_pos / 8;
            const int bit = byte < m_b.size() ? (quint8(m_b[byte]) >> (7 - m_pos % 8)) & 1 : 0;
            v = v << 1 | quint32(bit);
            ++m_pos;
        }
        return v;
    }

private:
    QByteArrayView m_b;
    qsizetype m_pos = 0;
};

// leb128 of an OBU size.
bool leb128(QByteArrayView b, qsizetype &pos, quint64 &v)
{
    v = 0;
    for (int i = 0; i < 8; ++i) {
        if (pos >= b.size())
            return false;
        const quint8 byte = quint8(b[pos++]);
        v |= quint64(byte & 0x7F) << (7 * i);
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

enum : quint32 {
    EBML = 0x1A45DFA3, EBMLVersion = 0x4286, EBMLReadVersion = 0x42F7, EBMLMaxIDLength = 0x42F2,
    EBMLMaxSizeLength = 0x42F3, DocType = 0x4282, DocTypeVersion = 0x4287, DocTypeReadVersion = 0x4285,
    Segment = 0x18538067, Info = 0x1549A966, TimecodeScale = 0x2AD7B1, Duration = 0x4489, MuxingApp = 0x4D80,
    WritingApp = 0x5741, Tracks = 0x1654AE6B, TrackEntry = 0xAE, TrackNumber = 0xD7, TrackUID = 0x73C5,
    TrackType = 0x83, CodecID = 0x86, CodecPrivate = 0x63A2, CodecDelay = 0x56AA, SeekPreRoll = 0x56BB,
    Video = 0xE0, PixelWidth = 0xB0, PixelHeight = 0xBA, Audio = 0xE1, SamplingFrequency = 0xB5, Channels = 0x9F,
    Cluster = 0x1F43B675, Timecode = 0xE7, SimpleBlock = 0xA3,
};

constexpr int kOpusPreSkip = 312; // the encoder's lookahead at 48 kHz

} // namespace

namespace Webm {

QByteArray av1Config(QByteArrayView frame)
{
    qsizetype pos = 0;
    while (pos < frame.size()) {
        const quint8 header = quint8(frame[pos]);
        const int type = (header >> 3) & 0xF;
        const bool extension = header & 4, hasSize = header & 2;
        const qsizetype obuStart = pos;
        pos += extension ? 2 : 1;
        quint64 size = 0;
        if (!hasSize || !leb128(frame, pos, size) || pos + qsizetype(size) > frame.size())
            return {};
        if (type == 1) { // OBU_SEQUENCE_HEADER
            BitReader r(frame.sliced(pos, qsizetype(size)));
            const quint32 profile = r.bits(3);
            r.bits(1); // still_picture
            const bool reduced = r.bits(1);
            quint32 level = 0, tier = 0;
            if (reduced) {
                level = r.bits(5);
            } else {
                const bool timing = r.bits(1);
                if (timing)
                    level = 31; // not parsed further: "any level"
                else {
                    r.bits(1);                     // initial_display_delay_present_flag
                    r.bits(5);                     // operating_points_cnt_minus_1
                    r.bits(12);                    // operating_point_idc[0]
                    level = r.bits(5);
                    if (level > 7)
                        tier = r.bits(1);
                }
            }
            QByteArray out;
            out.append(char(0x81));                                  // marker, version 1
            out.append(char((profile << 5) | level));
            out.append(char((tier << 7) | 0x0C));                    // 8 bits, 4:2:0 (subsampling x and y)
            out.append(char(0));
            out.append(frame.sliced(obuStart, pos + qsizetype(size) - obuStart).toByteArray());
            return out;
        }
        pos += qsizetype(size);
    }
    return {};
}

QByteArray write(const MediaClip &clip)
{
    const int videoStream = clip.streamOf(MediaStream::Video), audioStream = clip.streamOf(MediaStream::Audio);
    const MediaPacket *firstKey = nullptr;
    qint64 origin = MediaClip::kAll;
    for (const MediaPacket &p : clip.packets) {
        if (origin == MediaClip::kAll || p.ptsUs < origin)
            origin = p.ptsUs;
        if (!firstKey && p.key && clip.streams.value(p.stream).kind == MediaStream::Video)
            firstKey = &p;
    }
    if (origin == MediaClip::kAll)
        origin = 0;

    QByteArray header = element(EBML, uintElement(EBMLVersion, 1) + uintElement(EBMLReadVersion, 1)
                                          + uintElement(EBMLMaxIDLength, 4) + uintElement(EBMLMaxSizeLength, 8)
                                          + stringElement(DocType, "webm") + uintElement(DocTypeVersion, 4)
                                          + uintElement(DocTypeReadVersion, 2));
    QByteArray tracks;
    if (videoStream >= 0) {
        const MediaStream &s = clip.streams[videoStream];
        QByteArray t = uintElement(TrackNumber, 1) + uintElement(TrackUID, QRandomGenerator::global()->generate() | 1)
                       + uintElement(TrackType, 1) + stringElement(CodecID, "V_AV1");
        if (firstKey) {
            const QByteArray config = av1Config(firstKey->data);
            if (!config.isEmpty())
                t += element(CodecPrivate, config);
        }
        t += element(Video, uintElement(PixelWidth, s.a) + uintElement(PixelHeight, s.b));
        tracks += element(TrackEntry, t);
    }
    if (audioStream >= 0) {
        QByteArray head("OpusHead", 8);
        head.append(char(1));                 // version
        head.append(char(1));                 // channels
        char b[4];
        qToLittleEndian<quint16>(kOpusPreSkip, b);
        head.append(b, 2);
        qToLittleEndian<quint32>(48000, b);
        head.append(b, 4);
        head.append(2, char(0));              // output gain
        head.append(char(0));                 // channel mapping family
        QByteArray t = uintElement(TrackNumber, 2) + uintElement(TrackUID, QRandomGenerator::global()->generate() | 1)
                       + uintElement(TrackType, 2) + stringElement(CodecID, "A_OPUS") + element(CodecPrivate, head)
                       + uintElement(CodecDelay, quint64(kOpusPreSkip) * 1000000000 / 48000)
                       + uintElement(SeekPreRoll, 80000000)
                       + element(Audio, floatElement(SamplingFrequency, 48000.0) + uintElement(Channels, 1));
        tracks += element(TrackEntry, t);
    }

    // Clusters: a new one at each key frame of the video, or before the relative time of a block overflows.
    QList<const MediaPacket *> order;
    for (const MediaPacket &p : clip.packets)
        if (p.stream < clip.streams.size())
            order.append(&p);
    std::stable_sort(order.begin(), order.end(), [](const MediaPacket *a, const MediaPacket *b) { return a->ptsUs < b->ptsUs; });
    QByteArray clusters, cluster;
    qint64 clusterMs = -1, lastMs = 0;
    auto flush = [&] {
        if (clusterMs >= 0)
            clusters += element(Cluster, uintElement(Timecode, quint64(clusterMs)) + cluster);
        cluster.clear();
    };
    for (const MediaPacket *p : order) {
        const bool video = clip.streams[p->stream].kind == MediaStream::Video;
        const qint64 ms = (p->ptsUs - origin) / 1000;
        if (clusterMs < 0 || (video && p->key) || ms - clusterMs > 30000) {
            flush();
            clusterMs = ms;
        }
        lastMs = std::max(lastMs, ms);
        QByteArray block = vint(video ? 1 : 2);
        const qint16 rel = qint16(ms - clusterMs);
        char b[2];
        qToBigEndian(rel, b);
        block.append(b, 2);
        block.append(char(p->key ? 0x80 : 0));
        block.append(p->data);
        cluster += element(SimpleBlock, block);
    }
    flush();

    const QByteArray info = element(Info, uintElement(TimecodeScale, 1000000) + floatElement(Duration, double(lastMs))
                                              + stringElement(MuxingApp, "Typing statistics")
                                              + stringElement(WritingApp, "Typing statistics"));
    return header + element(Segment, info + element(Tracks, tracks) + clusters);
}

} // namespace Webm
