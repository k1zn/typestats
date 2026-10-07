#pragma once

#include "Yuv.h"

#include <QList>
#include <QString>
#include <memory>

// AV1 through libaom in its realtime mode (re/webcam.md): a frame in, its packet out at once (no lag, no frames
// reordered), so a recording can be cut after any frame.
struct EncodedFrame
{
    QByteArray data;
    qint64 ptsUs = 0;
    bool key = false;
};

struct Av1Settings
{
    int width = 640;
    int height = 360;
    int fps = 15;
    int kbps = 90;
    int keyIntervalMs = 4000;
    int threads = 2;
};

class Av1Encoder
{
public:
    Av1Encoder();
    ~Av1Encoder();
    Av1Encoder(const Av1Encoder &) = delete;
    Av1Encoder &operator=(const Av1Encoder &) = delete;

    bool open(const Av1Settings &settings, QString *error = nullptr);
    bool isOpen() const;
    const Av1Settings &settings() const { return m_settings; }
    // The frame must have the size of the settings. `ptsUs` grow; `key` forces a key frame.
    QList<EncodedFrame> encode(const I420Frame &frame, qint64 ptsUs, bool key = false);
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    Av1Settings m_settings;
};

class Av1Decoder
{
public:
    Av1Decoder();
    ~Av1Decoder();
    Av1Decoder(const Av1Decoder &) = delete;
    Av1Decoder &operator=(const Av1Decoder &) = delete;

    // The picture the packet gives (null when it gives none or is broken). Packets go in their order, from a key one.
    I420Frame decode(QByteArrayView packet);
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
