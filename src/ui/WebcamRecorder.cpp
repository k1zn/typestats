#include "WebcamRecorder.h"

#include "media/Av1Codec.h"
#include "media/OpusCodec.h"

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>
#include <memory>

// The encoders, in their thread. A frame of another generation (a new session or a new clip) starts with a key frame.
class WebcamRecorder::Worker : public QObject
{
public:
    Av1Encoder video;
    std::unique_ptr<OpusAudioEncoder> audio;
    Preset preset{};
    int lastGeneration = -1;

    void open(const Preset &p, bool withAudio)
    {
        preset = p;
        Av1Settings s;
        s.width = p.width;
        s.height = p.height;
        s.fps = p.fps;
        s.kbps = p.kbps;
        video.open(s);
        audio = withAudio ? std::make_unique<OpusAudioEncoder>() : nullptr;
        lastGeneration = -1;
    }
    void close()
    {
        video.close();
        audio.reset();
    }
};

WebcamRecorder::Settings WebcamRecorder::Settings::load()
{
    QSettings q;
    Settings s;
    s.video = q.value(QStringLiteral("WebcamOn"), false).toBool();
    s.camera = q.value(QStringLiteral("WebcamDevice")).toString();
    s.quality = std::clamp(q.value(QStringLiteral("WebcamQuality"), 1).toInt(), 0, 2);
    s.audio = q.value(QStringLiteral("WebcamAudio"), false).toBool();
    s.microphone = q.value(QStringLiteral("WebcamMic")).toString();
    return s;
}

void WebcamRecorder::Settings::save() const
{
    QSettings q;
    q.setValue(QStringLiteral("WebcamOn"), video);
    q.setValue(QStringLiteral("WebcamDevice"), camera);
    q.setValue(QStringLiteral("WebcamQuality"), quality);
    q.setValue(QStringLiteral("WebcamAudio"), audio);
    q.setValue(QStringLiteral("WebcamMic"), microphone);
}

WebcamRecorder::Preset WebcamRecorder::preset(int quality)
{
    switch (quality) {
    case 0:
        return {320, 240, 10, 40};
    case 2:
        return {640, 480, 24, 200};
    default:
        return {640, 360, 15, 90};
    }
}

WebcamRecorder::WebcamRecorder(QObject *parent) : QObject(parent)
{
    m_worker = new Worker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("webcam encoder"));
    m_thread.start();

    m_camera = new Camera(this);
    m_mic = new Microphone(this);
    connect(m_camera, &Camera::frame, this, [this](const I420Frame &p, qint64 t) { addVideo(p, t); }, Qt::DirectConnection);
    connect(m_mic, &Microphone::samples, this,
            [this](const QVector<float> &pcm, int ch, int rate, qint64 t) { addAudio(pcm, ch, rate, t); },
            Qt::DirectConnection);
    connect(m_camera, &Camera::failed, this, &WebcamRecorder::failed, Qt::QueuedConnection);
    connect(m_mic, &Microphone::failed, this, &WebcamRecorder::failed, Qt::QueuedConnection);
}

WebcamRecorder::~WebcamRecorder()
{
    m_accepting = false;
    m_camera->stop();
    m_mic->stop();
    m_thread.quit();
    m_thread.wait();
}

void WebcamRecorder::setSettings(const Settings &s)
{
    const bool restart = m_recording && (s.quality != m_settings.quality || s.audio != m_settings.audio
                                          || s.camera != m_settings.camera || s.microphone != m_settings.microphone
                                          || s.video != m_settings.video);
    if (restart)
        stopSession();
    m_camera->stop();
    m_mic->stop();
    m_settings = s;
    update();
}

void WebcamRecorder::setClip(MediaClip *clip)
{
    // Packets on their way belong to the old clip; the new one starts with a key frame.
    ++m_generation;
    m_pending.clear();
    m_clip = clip;
    m_lastPts[0] = m_lastPts[1] = MediaClip::kAll;
    if (m_recording && m_clip) {
        m_videoStream = m_settings.video ? m_clip->addStream(MediaStream::video(m_preset.width, m_preset.height)) : -1;
        m_audioStream = m_settings.audio ? m_clip->addStream(MediaStream::audio(OpusAudioEncoder::kRate, 1)) : -1;
    }
    update();
}

void WebcamRecorder::setCapture(bool on)
{
    m_capture = on;
    update();
}

void WebcamRecorder::setPreview(bool on)
{
    m_preview = on;
    update();
}

void WebcamRecorder::setClock(qint64 docEndUs, std::optional<qint64> timerUs)
{
    m_docEndUs = docEndUs;
    m_timerUs = timerUs;
    if (m_timerUs && !m_pending.isEmpty()) {
        const QList<Pending> pending = std::exchange(m_pending, {});
        for (const Pending &p : pending)
            place(p.kind, p.frame);
        emit packetsAdded();
    }
}

void WebcamRecorder::update()
{
    const bool record = m_capture && m_clip && (m_settings.video || m_settings.audio);
    if (record != m_recording) {
        if (record)
            startSession();
        else
            stopSession();
        emit recordingChanged(m_recording);
    }
    const bool camera = m_devices && m_settings.video && (m_recording || m_preview);
    const bool mic = m_devices && m_settings.audio && m_recording;
    const Preset p = preset(m_settings.quality);
    if (camera && !m_camera->isActive())
        m_camera->start(m_settings.camera, QSize(p.width, p.height), p.fps);
    else if (!camera && m_camera->isActive())
        m_camera->stop();
    if (mic && !m_mic->isActive())
        m_mic->start(m_settings.microphone);
    else if (!mic && m_mic->isActive())
        m_mic->stop();
}

void WebcamRecorder::startSession()
{
    m_recording = true;
    m_preset = preset(m_settings.quality);
    m_fps = m_preset.fps;
    ++m_generation;
    m_pending.clear();
    m_lastPts[0] = m_lastPts[1] = MediaClip::kAll;
    m_videoStream = m_settings.video ? m_clip->addStream(MediaStream::video(m_preset.width, m_preset.height)) : -1;
    m_audioStream = m_settings.audio ? m_clip->addStream(MediaStream::audio(OpusAudioEncoder::kRate, 1)) : -1;
    const Preset p = m_preset;
    const bool audio = m_settings.audio;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, p, audio] { w->open(p, audio); }, Qt::QueuedConnection);
    m_lastFrameUs = 0;
    m_accepting = true;
}

void WebcamRecorder::stopSession()
{
    m_recording = false;
    m_accepting = false;
    ++m_generation;
    m_pending.clear();
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); }, Qt::QueuedConnection);
}

void WebcamRecorder::addVideo(const I420Frame &picture, qint64 steadyUs)
{
    // The preview: a picture now and then.
    if (m_preview && steadyUs - m_lastPictureUs >= 1000000 / 15) {
        m_lastPictureUs = steadyUs;
        QMetaObject::invokeMethod(this, [this, image = Yuv::toImage(picture)] { emit this->picture(image); },
                                  Qt::QueuedConnection);
    }
    if (!m_accepting)
        return;
    // The rate of the preset (a camera may give more), and no queue when the encoder falls behind.
    const int fps = m_fps;
    if (m_lastFrameUs != 0 && steadyUs - m_lastFrameUs < 1000000 / fps - 8000)
        return;
    if (m_queued >= 2)
        return;
    m_lastFrameUs = steadyUs;
    ++m_queued;
    const int generation = m_generation;
    QMetaObject::invokeMethod(
        m_worker,
        [this, w = m_worker, generation, picture, steadyUs] {
            --m_queued;
            const I420Frame frame = Yuv::scaled(picture, w->preset.width, w->preset.height);
            const bool key = generation != w->lastGeneration;
            w->lastGeneration = generation;
            for (const EncodedFrame &f : w->video.encode(frame, steadyUs, key))
                QMetaObject::invokeMethod(this, [this, generation, f] { encoded(generation, MediaStream::Video, f); },
                                          Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void WebcamRecorder::addAudio(const QVector<float> &pcm, int channels, int rate, qint64 steadyUs)
{
    if (!m_accepting)
        return;
    const int generation = m_generation;
    QMetaObject::invokeMethod(
        m_worker,
        [this, w = m_worker, generation, pcm, channels, rate, steadyUs] {
            if (!w->audio)
                return;
            for (const EncodedFrame &f : w->audio->push(pcm.constData(), int(pcm.size() / channels), channels, rate, steadyUs))
                QMetaObject::invokeMethod(this, [this, generation, f] { encoded(generation, MediaStream::Audio, f); },
                                          Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void WebcamRecorder::drain()
{
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

void WebcamRecorder::encoded(int generation, int kind, const EncodedFrame &f)
{
    if (generation != m_generation || !m_clip || !m_recording)
        return;
    if (!m_timerUs) {
        m_pending.append({kind, f});
        return;
    }
    place(kind, f);
    emit packetsAdded();
}

void WebcamRecorder::place(int kind, const EncodedFrame &f)
{
    const int stream = kind == MediaStream::Video ? m_videoStream : m_audioStream;
    if (stream < 0)
        return;
    // In the document's time; it only grows within a stream (a hotkey moves the timer without a record).
    qint64 pts = m_clip->ptsOf(m_docEndUs + (f.ptsUs - *m_timerUs));
    if (m_lastPts[kind] != MediaClip::kAll)
        pts = std::max(pts, m_lastPts[kind] + 1);
    m_lastPts[kind] = pts;
    m_clip->packets.append({quint8(stream), f.key, pts, f.data});
}
