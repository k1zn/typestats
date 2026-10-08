#pragma once

#include "core/MediaClip.h"
#include "media/Av1Codec.h"
#include "media/Capture.h"

#include <QImage>
#include <QObject>
#include <QThread>

#include <atomic>
#include <optional>

// The webcam (and the microphone) while recording (re/webcam.md): the devices, the encoders in a thread of their own,
// and the packets into the document's clip in its time. Recording follows the capture ("Вкл"): the camera works while
// it is on and the webcam is enabled, or while its picture is looked at.
class WebcamRecorder : public QObject
{
    Q_OBJECT
public:
    struct Preset
    {
        int width, height, fps, kbps;
        bool operator==(const Preset &) const = default;
        // Within what the encoder and the cameras take: even sizes 160×120..1920×1080, 1..30 fps, 10..4000 kbit/s.
        Preset bounded() const;
        // A bitrate that suits the size and the rate of frames (a person at a keyboard): near the presets' ones,
        // growing slower for large frames.
        static int suggestedKbps(int width, int height, int fps);
    };
    enum Quality { Economy, Normal, Good, Custom };
    static Preset preset(int quality); // Economy..Good
    struct Settings
    {
        bool video = false;   // "WebcamOn"
        QString camera;       // "WebcamDevice": an id of Camera::devices(), empty - the first camera
        int quality = Normal; // "WebcamQuality": Quality
        Preset custom{640, 360, 15, 90}; // "WebcamWidth", "WebcamHeight", "WebcamFps", "WebcamKbps": for Custom
        bool audio = false;   // "WebcamAudio"
        QString microphone;   // "WebcamMic"
        Preset preset() const { return quality == Custom ? custom.bounded() : WebcamRecorder::preset(quality); }
        static Settings load();
        void save() const;
    };

    explicit WebcamRecorder(QObject *parent = nullptr);
    ~WebcamRecorder() override;

    void setSettings(const Settings &s);
    const Settings &settings() const { return m_settings; }
    // The clip of the document: the packets go there. A new clip starts with a key frame.
    void setClip(MediaClip *clip);
    // Recording follows the capture.
    void setCapture(bool on);
    bool isCapturing() const { return m_capture; }
    // The picture of the camera is shown (the video window is open while recording or not).
    void setPreview(bool on);
    // Recording starts with the next key typed into the document: the camera and the microphone are open already (the
    // camera takes up to a second to start and to settle its exposure), and the session starts with the camera's last
    // frame and the last 0.3 s of sound before the key.
    void setArmed(bool on);
    bool isArmed() const { return m_armed; }
    bool isRecording() const { return m_recording; }
    // The document's time of a moment `s` of the recording: docEndUs + (s − timerUs) (Recorder::timerUs). Packets
    // wait until there is a timer.
    void setClock(qint64 docEndUs, std::optional<qint64> timerUs);
    // false: no devices are opened (tests feed addVideo / addAudio).
    void setDevicesEnabled(bool on) { m_devices = on; }

    // What the devices give, from any thread.
    void addVideo(const I420Frame &picture, qint64 steadyUs);
    void addAudio(const QVector<float> &pcm, int channels, int rate, qint64 steadyUs);
    // Waits until the encoders have done what they were given (tests).
    void drain();

signals:
    void picture(const QImage &image);  // the camera, for the preview (at most 15 a second)
    void packetsAdded();
    void recordingChanged(bool recording);
    void failed(const QString &reason);

private:
    class Worker;
    struct Pending
    {
        int kind;
        EncodedFrame frame;
    };
    void update();
    void startSession();
    void stopSession();
    void encode(const I420Frame &picture, qint64 steadyUs);
    void encodeAudio(const QVector<float> &pcm, int channels, int rate, qint64 steadyUs);
    void encoded(int generation, int kind, const EncodedFrame &f);
    void place(int kind, const EncodedFrame &f);

    Settings m_settings;
    MediaClip *m_clip = nullptr;
    Camera *m_camera = nullptr;
    Microphone *m_mic = nullptr;
    QThread m_thread;
    Worker *m_worker = nullptr;
    bool m_capture = false, m_preview = false, m_armed = false, m_recording = false, m_devices = true;
    std::atomic<int> m_generation{0};
    std::atomic<bool> m_accepting{false};
    std::atomic<qint64> m_lastFrameUs{0};
    std::atomic<qint64> m_lastPictureUs{0};
    std::atomic<int> m_queued{0};
    std::atomic<int> m_fps{15};          // of the session, read by the camera's thread
    I420Frame m_warm;                    // armed: the last frame before the session (the camera's thread only)
    qint64 m_warmUs = 0;
    struct WarmAudio
    {
        QVector<float> pcm;
        int channels, rate;
        qint64 steadyUs;
    };
    static constexpr qint64 kWarmAudioUs = 300000;
    QList<WarmAudio> m_warmAudio;        // armed: the last 0.3 s of sound before the session (the microphone's thread)
    int m_videoStream = -1, m_audioStream = -1;
    qint64 m_docEndUs = 0;
    std::optional<qint64> m_timerUs;
    qint64 m_lastPts[2] = {MediaClip::kAll, MediaClip::kAll};
    QList<Pending> m_pending;
    Preset m_preset{};
};
