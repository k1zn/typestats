// A system without a sound backend yet: playback is silent.

#include "AudioOut.h"

struct AudioOut::Impl
{
};

AudioOut::AudioOut() : d(std::make_unique<Impl>()) {}
AudioOut::~AudioOut() = default;

bool AudioOut::open(int)
{
    return false;
}

void AudioOut::write(const QVector<qint16> &) {}
void AudioOut::reset() {}
void AudioOut::close() {}

bool AudioOut::isOpen() const
{
    return false;
}
