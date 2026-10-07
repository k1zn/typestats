#pragma once

#include "core/MediaClip.h"

#include <QIcon>
#include <QImage>
#include <QWidget>
#include <memory>

class AudioOut;
class Av1Decoder;
class OpusAudioDecoder;
class QLabel;
class QSlider;
class QToolButton;

// The picture of the webcam (Form5 "Видео", re/webcam.md): while recording - what the camera sees; while looking at a
// recording - its frame at the time of the klavogram's left edge, and playback in real time.
class VideoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWindow(QWidget *parent = nullptr);
    ~VideoWindow() override;

    // Live: the camera's pictures; otherwise the frames of the clip.
    void setLive(bool live);
    bool isLive() const { return m_live; }
    void setRecording(bool recording);
    void setPicture(const QImage &image); // live
    // The clip whose frames are shown (null: none). Its packets may grow (recording).
    void setClip(const MediaClip *clip);
    // The frame at the document's time.
    void showTime(qint64 docUs);
    void setPlaying(bool playing);
    // The span of the slider (the document's time of the first and the last key or frame).
    void setSpan(qint64 fromUs, qint64 toUs);
    // The sound of the clip while playing: from a time, then kept a little ahead of the time played.
    void startSound(qint64 docUs);
    void feedSound(qint64 docUs);
    void stopSound();
    const QImage &image() const { return m_image; }
    // The message instead of a picture ("no video here", the camera's error).
    void setMessage(const QString &text);

signals:
    void playToggled(bool play);
    void saveRequested(); // "Сохранить видео…": the clip as a WebM file
    void seekRequested(qint64 docUs); // the slider moved by hand
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    friend class TstUi;
    QRect pictureRect() const;
    void updateTimeLabel(qint64 docUs);
    void updateIcons(); // drawn in the colour of the text (the themes)

    bool m_live = false;
    bool m_recording = false;
    const MediaClip *m_clip = nullptr;
    std::unique_ptr<Av1Decoder> m_decoder;
    int m_key = -1, m_decoded = -1;  // the key frame and the last frame the decoder has had
    QImage m_image;
    QString m_message;
    std::unique_ptr<AudioOut> m_soundOut;
    std::unique_ptr<OpusAudioDecoder> m_soundDecoder;
    bool m_sound = false;
    int m_soundNext = -1;      // the next audio packet to decode
    qint64 m_soundEndUs = 0;   // the document's time up to which the sound is queued
    QToolButton *m_play = nullptr;
    QToolButton *m_save = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_rec = nullptr;
    QSlider *m_slider = nullptr;   // milliseconds from the span's start
    qint64 m_spanFromUs = 0, m_spanToUs = 0;
    bool m_resume = false;         // playing when the slider was grabbed: on again when let go
    QIcon m_playIcon, m_pauseIcon;
};
