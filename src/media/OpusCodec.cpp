#include "OpusCodec.h"

#include <opus.h>

#include <algorithm>
#include <cmath>

OpusAudioEncoder::OpusAudioEncoder(int kbps)
{
    int err = 0;
    m_enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        m_enc = nullptr;
        return;
    }
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(kbps * 1000));
    opus_encoder_ctl(m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_enc, OPUS_SET_COMPLEXITY(5));
}

OpusAudioEncoder::~OpusAudioEncoder()
{
    if (m_enc)
        opus_encoder_destroy(m_enc);
}

QList<EncodedFrame> OpusAudioEncoder::push(const float *samples, int frames, int channels, int rate, qint64 ptsUs)
{
    QList<EncodedFrame> out;
    if (!m_enc || frames <= 0 || channels <= 0 || rate <= 0)
        return out;
    // More than a packet away from where the last push ended: a new run.
    if (!m_running || std::abs(ptsUs - m_expectedPts) > kFrameUs) {
        m_pending.clear();
        m_pendingPts = ptsUs;
        m_phase = 0;
        m_running = true;
        m_last = 0;
        opus_encoder_ctl(m_enc, OPUS_RESET_STATE);
    }
    m_expectedPts = ptsUs + qint64(frames) * 1000000 / rate;

    // Mono, then linear interpolation to 48 kHz (a voice track: no need for a better filter).
    const double step = double(rate) / kRate;
    for (int i = 0; i < frames; ++i) {
        float s = 0;
        for (int c = 0; c < channels; ++c)
            s += samples[qsizetype(i) * channels + c];
        s /= channels;
        while (m_phase < 1.0) {
            m_pending.append(float(m_last + (s - m_last) * m_phase));
            m_phase += step;
        }
        m_phase -= 1.0;
        m_last = s;
    }

    int used = 0;
    unsigned char buf[1500];
    while (m_pending.size() - used >= kFrame) {
        const int n = opus_encode_float(m_enc, m_pending.constData() + used, kFrame, buf, sizeof buf);
        if (n > 0) {
            EncodedFrame f;
            f.data = QByteArray(reinterpret_cast<const char *>(buf), n);
            f.ptsUs = m_pendingPts + qint64(used) * 1000000 / kRate;
            f.key = true; // every Opus packet can start the playback (after a short warm-up)
            out.append(std::move(f));
        }
        used += kFrame;
    }
    if (used) {
        m_pending.remove(0, used);
        m_pendingPts += qint64(used) * 1000000 / kRate;
    }
    return out;
}

OpusAudioDecoder::OpusAudioDecoder() = default;

OpusAudioDecoder::~OpusAudioDecoder()
{
    reset();
}

void OpusAudioDecoder::reset()
{
    if (m_dec)
        opus_decoder_destroy(m_dec);
    m_dec = nullptr;
}

QVector<qint16> OpusAudioDecoder::decode(QByteArrayView packet)
{
    if (!m_dec) {
        int err = 0;
        m_dec = opus_decoder_create(OpusAudioEncoder::kRate, 1, &err);
        if (err != OPUS_OK) {
            m_dec = nullptr;
            return {};
        }
    }
    QVector<qint16> pcm(OpusAudioEncoder::kRate / 10); // up to 100 ms
    const int n = opus_decode(m_dec, reinterpret_cast<const unsigned char *>(packet.data()), opus_int32(packet.size()),
                              pcm.data(), int(pcm.size()), 0);
    if (n <= 0)
        return {};
    pcm.resize(n);
    return pcm;
}
