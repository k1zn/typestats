#pragma once

#include "Yuv.h"

#include <QObject>
#include <QSize>
#include <QVector>
#include <memory>

// The webcam and the microphone (re/webcam.md), each system its own way: Media Foundation (Windows), AVFoundation
// (macOS), V4L2 and PulseAudio (Linux). Frames and samples are given in the capture thread, timed by steady_clock
// (hookNowUs, the clock of the keys); receivers connect queued.
struct CaptureDevice
{
    QString id;
    QString name;
};

class Camera : public QObject
{
    Q_OBJECT
public:
    explicit Camera(QObject *parent = nullptr);
    ~Camera() override;

    static QList<CaptureDevice> devices();
    // Asks for frames near this size and rate (the camera picks what it has); an empty id - the first camera.
    // The result comes as started() or failed().
    void start(const QString &deviceId, QSize size, int fps);
    void stop();
    bool isActive() const;

signals:
    void frame(const I420Frame &picture, qint64 steadyUs);
    void started(QSize size);
    void failed(const QString &reason);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

class Microphone : public QObject
{
    Q_OBJECT
public:
    explicit Microphone(QObject *parent = nullptr);
    ~Microphone() override;

    static QList<CaptureDevice> devices();
    void start(const QString &deviceId);
    void stop();
    bool isActive() const;

signals:
    // Interleaved samples in [-1, 1]; steadyUs - the time of the first of them.
    void samples(const QVector<float> &pcm, int channels, int rate, qint64 steadyUs);
    void failed(const QString &reason);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

Q_DECLARE_METATYPE(I420Frame)
