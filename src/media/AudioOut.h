#pragma once

#include <QVector>
#include <memory>

// Sound out for the playback of a recording (re/webcam.md): mono 16-bit samples, queued as they are decoded.
// Windows: waveOut; elsewhere a backend of the system.
class AudioOut
{
public:
    AudioOut();
    ~AudioOut();
    AudioOut(const AudioOut &) = delete;
    AudioOut &operator=(const AudioOut &) = delete;

    bool open(int rate);
    // Queues the samples after those given before.
    void write(const QVector<qint16> &samples);
    // Drops what is queued (pause, a jump).
    void reset();
    void close();
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
