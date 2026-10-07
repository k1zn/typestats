// macOS: sound out through an AudioQueue; each write is a buffer of its own, freed when played.

#include "media/AudioOut.h"

#include <AudioToolbox/AudioToolbox.h>

#include <cstring>

struct AudioOut::Impl
{
    AudioQueueRef queue = nullptr;
    bool started = false;

    static void done(void *, AudioQueueRef q, AudioQueueBufferRef b) { AudioQueueFreeBuffer(q, b); }
};

AudioOut::AudioOut() : d(std::make_unique<Impl>()) {}

AudioOut::~AudioOut()
{
    close();
}

bool AudioOut::open(int rate)
{
    close();
    AudioStreamBasicDescription f{};
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    f.mBitsPerChannel = 16;
    f.mChannelsPerFrame = 1;
    f.mBytesPerFrame = 2;
    f.mFramesPerPacket = 1;
    f.mBytesPerPacket = 2;
    return AudioQueueNewOutput(&f, &Impl::done, nullptr, nullptr, nullptr, 0, &d->queue) == noErr;
}

void AudioOut::write(const QVector<qint16> &samples)
{
    if (!d->queue || samples.isEmpty())
        return;
    AudioQueueBufferRef b = nullptr;
    const UInt32 bytes = UInt32(samples.size() * 2);
    if (AudioQueueAllocateBuffer(d->queue, bytes, &b) != noErr)
        return;
    std::memcpy(b->mAudioData, samples.constData(), bytes);
    b->mAudioDataByteSize = bytes;
    if (AudioQueueEnqueueBuffer(d->queue, b, 0, nullptr) != noErr) {
        AudioQueueFreeBuffer(d->queue, b);
        return;
    }
    if (!d->started)
        d->started = AudioQueueStart(d->queue, nullptr) == noErr;
}

void AudioOut::reset()
{
    if (d->queue) {
        AudioQueueReset(d->queue);
        AudioQueueStop(d->queue, true);
        d->started = false;
    }
}

void AudioOut::close()
{
    if (d->queue) {
        AudioQueueStop(d->queue, true);
        AudioQueueDispose(d->queue, true);
    }
    d->queue = nullptr;
    d->started = false;
}

bool AudioOut::isOpen() const
{
    return d->queue;
}
