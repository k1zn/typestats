#pragma once

#include <QByteArray>
#include <QList>
#include <limits>

// The webcam recording of a document (re/webcam.md): packets of AV1 video and Opus audio as the encoders gave them,
// timed in the document's time. The bytes of a packet (with its header) never change once written - the time stamps
// hash them (re/stamps.md, chain v2): cutting and edits move only the clip's fields.
struct MediaStream
{
    enum Kind : quint8 { Video = 0, Audio = 1 };
    enum Codec : quint8 { Av1 = 0, Opus = 1 };
    Kind kind = Video;
    Codec codec = Av1;
    quint16 a = 0; // video: width; audio: rate / 100
    quint16 b = 0; // video: height; audio: channels
    quint8 c = 0;

    static MediaStream video(int width, int height) { return {Video, Av1, quint16(width), quint16(height), 0}; }
    static MediaStream audio(int rate, int channels) { return {Audio, Opus, quint16(rate / 100), quint16(channels), 0}; }
    bool operator==(const MediaStream &) const = default;
};

struct MediaPacket
{
    quint8 stream = 0;
    bool key = false;
    qint64 ptsUs = 0;
    QByteArray data;
};

class MediaClip
{
public:
    static constexpr qint64 kAll = std::numeric_limits<qint64>::min();
    static constexpr qint64 kOpusPreroll = 80000; // the decoder settles in 80 ms
    static constexpr qint64 kOpusPacket = 20000;

    QList<MediaStream> streams;
    // The time of the document is pts − originUs: the time since the start of the document's first record's dt
    // (re/webcam.md, "Синхронизация").
    qint64 originUs = 0;
    // Packets before it (pts) are only for decoding: a cut keeps the frames from the key frame before the cut.
    qint64 startUs = kAll;
    // Correction of the synchronization by hand, added to the time of the pictures when shown.
    qint32 shiftMs = 0;
    QList<MediaPacket> packets;

    bool isEmpty() const { return packets.isEmpty(); }
    int streamOf(MediaStream::Kind kind) const;
    // The stream index for this kind of stream, added when missing or when its parameters changed.
    int addStream(const MediaStream &s);

    qint64 docTime(qint64 ptsUs) const { return ptsUs - originUs; }
    qint64 ptsOf(qint64 docUs) const { return docUs + originUs; }
    // The span of the visible packets in the document's time ([0, 0] when empty).
    qint64 firstUs() const;
    qint64 lastUs() const;

    // The clip of [fromUs, toUs] (the document's time); its time starts at fromUs. Packets are copied as they are:
    // video from the key frame at or before fromUs to the last frame at or before toUs, audio with its preroll.
    MediaClip cut(qint64 fromUs, qint64 toUs) const;
    // After an edit moved the start of the document's time: the clip's time follows (a clip of an empty
    // document stays as it is).
    void moveOrigin(qint64 deltaUs) { originUs += deltaUs; }

    // The video packets to decode to show the frame at the document's time t: from the key frame up to the last
    // frame at or before t (indexes into `packets`), or {-1, -1} when there is none.
    std::pair<int, int> videoFramesAt(qint64 docUs) const;

    // The bytes of a packet as stored and as hashed by the time stamps: header and data.
    static QByteArray packetBytes(const MediaPacket &p);
    QByteArray serialize() const;
    static bool parse(QByteArrayView bytes, MediaClip &clip);
};
