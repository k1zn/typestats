// Linux: sound out through PulseAudio's simple API (loaded at run time; without it playback is silent).

#include "media/AudioOut.h"
#include "PulseSimple.h"

struct AudioOut::Impl
{
    Pulse::Simple *s = nullptr;
    int rate = 48000;
};

AudioOut::AudioOut() : d(std::make_unique<Impl>()) {}

AudioOut::~AudioOut()
{
    close();
}

bool AudioOut::open(int rate)
{
    close();
    const Pulse::Api &pa = Pulse::Api::get();
    if (!pa.ok)
        return false;
    d->rate = rate;
    const Pulse::SampleSpec spec{Pulse::S16LE, quint32(rate), 1};
    // Room for 2 s: the playback queues ~0.3 s ahead, writes do not wait. Run dry (a pause in the recorded sound,
    // VideoWindow::feedSound), the stream waits for the next write and plays it at once, as waveOut does: 20 ms
    // to start again (the default, the whole 2 s, would make the sound after every pause late by ~1.7 s; 0 would
    // let the server read on into the silence and drop the start of what comes next).
    const Pulse::BufferAttr attr{quint32(rate * 4), quint32(rate * 2 * 2), quint32(rate * 2 / 50), quint32(-1),
                                 quint32(-1)};
    int error = 0;
    d->s = pa.open(nullptr, "Typing statistics", Pulse::Playback, nullptr, "playback", &spec, nullptr, &attr, &error);
    return d->s;
}

void AudioOut::write(const QVector<qint16> &samples)
{
    if (d->s && !samples.isEmpty()) {
        int error = 0;
        Pulse::Api::get().write(d->s, samples.constData(), size_t(samples.size()) * 2, &error);
    }
}

void AudioOut::reset()
{
    if (d->s) {
        int error = 0;
        Pulse::Api::get().flush(d->s, &error);
    }
}

void AudioOut::close()
{
    if (d->s)
        Pulse::Api::get().free(d->s);
    d->s = nullptr;
}

bool AudioOut::isOpen() const
{
    return d->s;
}
