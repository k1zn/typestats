#include "MediaClip.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace {

constexpr char kMagic[4] = {'T', 'S', 'M', 'V'};
constexpr quint8 kVersion = 1;
constexpr qint64 kStaleUs = 1000000; // a frame older than this is not shown for a later time (a pause in recording)

template <typename T>
void put(QByteArray &out, T v)
{
    char b[sizeof(T)];
    qToLittleEndian(v, b);
    out.append(b, sizeof(T));
}

class Reader
{
public:
    explicit Reader(QByteArrayView b) : m_b(b) {}
    template <typename T>
    bool get(T &v)
    {
        if (m_pos + qsizetype(sizeof(T)) > m_b.size())
            return false;
        v = qFromLittleEndian<T>(m_b.data() + m_pos);
        m_pos += sizeof(T);
        return true;
    }
    bool bytes(QByteArray &out, qsizetype n)
    {
        if (n < 0 || m_pos + n > m_b.size())
            return false;
        out = m_b.mid(m_pos, n).toByteArray();
        m_pos += n;
        return true;
    }
    bool atEnd() const { return m_pos == m_b.size(); }

private:
    QByteArrayView m_b;
    qsizetype m_pos = 0;
};

} // namespace

int MediaClip::streamOf(MediaStream::Kind kind) const
{
    for (int i = int(streams.size()) - 1; i >= 0; --i)
        if (streams[i].kind == kind)
            return i;
    return -1;
}

int MediaClip::addStream(const MediaStream &s)
{
    const int i = streamOf(s.kind);
    if (i >= 0 && streams[i] == s)
        return i;
    streams.append(s);
    return int(streams.size()) - 1;
}

qint64 MediaClip::firstUs() const
{
    for (const MediaPacket &p : packets)
        if (startUs == kAll || p.ptsUs >= startUs)
            return docTime(p.ptsUs);
    return 0;
}

qint64 MediaClip::lastUs() const
{
    qint64 last = kAll;
    for (const MediaPacket &p : packets)
        last = std::max(last, p.ptsUs);
    return last == kAll ? 0 : docTime(last);
}

MediaClip MediaClip::cut(qint64 fromUs, qint64 toUs) const
{
    MediaClip out;
    out.streams = streams;
    out.shiftMs = shiftMs;
    out.originUs = originUs + fromUs;
    const qint64 from = ptsOf(fromUs), to = ptsOf(toUs);
    out.startUs = std::max(from, startUs);

    // Per video stream: the key frame at or before the start (or the first key frame after it).
    QList<int> firstVideo(streams.size(), -1);
    for (int i = 0; i < packets.size(); ++i) {
        const MediaPacket &p = packets[i];
        if (p.stream >= streams.size() || streams[p.stream].kind != MediaStream::Video || !p.key)
            continue;
        int &f = firstVideo[p.stream];
        if (p.ptsUs <= from || f < 0)
            f = i;
    }
    for (int i = 0; i < packets.size(); ++i) {
        const MediaPacket &p = packets[i];
        if (p.stream >= streams.size() || p.ptsUs > to)
            continue;
        if (streams[p.stream].kind == MediaStream::Video) {
            if (firstVideo[p.stream] < 0 || i < firstVideo[p.stream])
                continue;
        } else if (p.ptsUs + kOpusPacket <= from - kOpusPreroll) {
            continue;
        }
        out.packets.append(p);
    }
    return out;
}

std::pair<int, int> MediaClip::videoFramesAt(qint64 docUs) const
{
    const qint64 target = ptsOf(docUs) - qint64(shiftMs) * 1000;
    int frame = -1;
    for (int i = 0; i < packets.size(); ++i) {
        const MediaPacket &p = packets[i];
        if (p.stream >= streams.size() || streams[p.stream].kind != MediaStream::Video)
            continue;
        if (p.ptsUs > target)
            break;
        frame = i;
    }
    if (frame < 0 || target - packets[frame].ptsUs > kStaleUs || (startUs != kAll && target < startUs))
        return {-1, -1};
    for (int i = frame; i >= 0; --i) {
        const MediaPacket &p = packets[i];
        if (p.stream < streams.size() && streams[p.stream].kind == MediaStream::Video && p.key)
            return {i, frame};
    }
    return {-1, -1};
}

QByteArray MediaClip::packetBytes(const MediaPacket &p)
{
    QByteArray out;
    out.reserve(14 + p.data.size());
    put<quint8>(out, p.stream);
    put<quint8>(out, p.key ? 1 : 0);
    put<qint64>(out, p.ptsUs);
    put<quint32>(out, quint32(p.data.size()));
    out.append(p.data);
    return out;
}

QByteArray MediaClip::serialize() const
{
    QByteArray out;
    qsizetype size = 32;
    for (const MediaPacket &p : packets)
        size += 14 + p.data.size();
    out.reserve(size + streams.size() * 7);
    out.append(kMagic, 4);
    put<quint8>(out, kVersion);
    put<quint8>(out, quint8(streams.size()));
    for (const MediaStream &s : streams) {
        put<quint8>(out, s.kind);
        put<quint8>(out, s.codec);
        put<quint16>(out, s.a);
        put<quint16>(out, s.b);
        put<quint8>(out, s.c);
    }
    put<qint64>(out, originUs);
    put<qint64>(out, startUs);
    put<qint32>(out, shiftMs);
    for (const MediaPacket &p : packets)
        out.append(packetBytes(p));
    return out;
}

bool MediaClip::parse(QByteArrayView bytes, MediaClip &clip)
{
    clip = MediaClip();
    if (bytes.size() < 6 || std::memcmp(bytes.data(), kMagic, 4) != 0 || quint8(bytes[4]) != kVersion)
        return false;
    Reader r(bytes.sliced(5));
    quint8 n = 0;
    if (!r.get(n))
        return false;
    for (int i = 0; i < n; ++i) {
        quint8 kind, codec, c;
        quint16 a, b;
        if (!r.get(kind) || !r.get(codec) || !r.get(a) || !r.get(b) || !r.get(c))
            return false;
        clip.streams.append({MediaStream::Kind(kind), MediaStream::Codec(codec), a, b, c});
    }
    if (!r.get(clip.originUs) || !r.get(clip.startUs) || !r.get(clip.shiftMs))
        return false;
    while (!r.atEnd()) {
        MediaPacket p;
        quint8 flags;
        quint32 size;
        if (!r.get(p.stream) || !r.get(flags) || !r.get(p.ptsUs) || !r.get(size) || !r.bytes(p.data, qsizetype(size))
            || p.stream >= clip.streams.size())
            return false;
        p.key = flags & 1;
        clip.packets.append(std::move(p));
    }
    return true;
}

namespace DocTime {

qint64 end(const KeyRecords &records)
{
    qint64 t = 0;
    for (const KeyRecord &r : records)
        t += r.dtUs;
    return t;
}

qint64 of(const KeyRecords &records, int i)
{
    qint64 t = 0;
    for (int j = 0; j <= i && j < records.size(); ++j)
        t += records[j].dtUs;
    return t;
}

qint64 modelOffset(const KeyRecords &records)
{
    // The normalization drops the leading releases and gives the first press 60 s; the times after it stay.
    for (int i = 0; i < records.size(); ++i)
        if (records[i].isDown())
            return of(records, i) - 60000000;
    return 0;
}

} // namespace DocTime
