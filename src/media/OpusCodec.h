#pragma once

#include "Av1Codec.h"

#include <QList>
#include <QVector>

struct OpusEncoder;
struct OpusDecoder;

// Opus, mono 48 kHz, 20 ms packets (re/webcam.md). The microphone gives any rate and channels: the encoder mixes them
// down and resamples.
class OpusAudioEncoder
{
public:
    static constexpr int kRate = 48000;
    static constexpr int kFrame = 960; // 20 ms
    static constexpr qint64 kFrameUs = 20000;

    explicit OpusAudioEncoder(int kbps = 16);
    ~OpusAudioEncoder();
    OpusAudioEncoder(const OpusAudioEncoder &) = delete;
    OpusAudioEncoder &operator=(const OpusAudioEncoder &) = delete;

    bool isOpen() const { return m_enc; }
    // Interleaved samples in [-1, 1]; `ptsUs` - the time of the first of them. A gap in time starts a new run (the
    // unfinished packet is dropped).
    QList<EncodedFrame> push(const float *samples, int frames, int channels, int rate, qint64 ptsUs);

private:
    OpusEncoder *m_enc = nullptr;
    QVector<float> m_pending;   // mono 48 kHz, less than a packet
    qint64 m_pendingPts = 0;    // time of m_pending[0]
    qint64 m_expectedPts = 0;   // time the next push should start at
    double m_phase = 0;         // resampler position between the last input sample and the next one
    float m_last = 0;
    bool m_running = false;
};

class OpusAudioDecoder
{
public:
    OpusAudioDecoder();
    ~OpusAudioDecoder();
    OpusAudioDecoder(const OpusAudioDecoder &) = delete;
    OpusAudioDecoder &operator=(const OpusAudioDecoder &) = delete;

    // Mono 48 kHz samples of a packet; empty when broken.
    QVector<qint16> decode(QByteArrayView packet);
    void reset();

private:
    OpusDecoder *m_dec = nullptr;
};
