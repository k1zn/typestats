// Windows: waveOut, the oldest and simplest way (it is in every Windows, Media Feature Pack or not).

#include "AudioOut.h"

#include <windows.h>
#include <mmsystem.h>

#include <list>

struct AudioOut::Impl
{
    HWAVEOUT out = nullptr;
    struct Buffer
    {
        WAVEHDR header{};
        QVector<qint16> samples;
    };
    std::list<Buffer> queued;

    void collect()
    {
        for (auto it = queued.begin(); it != queued.end();) {
            if (it->header.dwFlags & WHDR_DONE) {
                waveOutUnprepareHeader(out, &it->header, sizeof(WAVEHDR));
                it = queued.erase(it);
            } else {
                ++it;
            }
        }
    }
};

AudioOut::AudioOut() : d(std::make_unique<Impl>()) {}

AudioOut::~AudioOut()
{
    close();
}

bool AudioOut::open(int rate)
{
    close();
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = DWORD(rate);
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = DWORD(rate) * 2;
    return waveOutOpen(&d->out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

bool AudioOut::isOpen() const
{
    return d->out;
}

void AudioOut::write(const QVector<qint16> &samples)
{
    if (!d->out || samples.isEmpty())
        return;
    d->collect();
    Impl::Buffer &b = d->queued.emplace_back();
    b.samples = samples;
    b.header.lpData = reinterpret_cast<LPSTR>(b.samples.data());
    b.header.dwBufferLength = DWORD(b.samples.size() * 2);
    if (waveOutPrepareHeader(d->out, &b.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR
        || waveOutWrite(d->out, &b.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        d->queued.pop_back();
}

void AudioOut::reset()
{
    if (!d->out)
        return;
    waveOutReset(d->out); // marks every buffer done
    d->collect();
}

void AudioOut::close()
{
    if (!d->out)
        return;
    reset();
    waveOutClose(d->out);
    d->out = nullptr;
}
