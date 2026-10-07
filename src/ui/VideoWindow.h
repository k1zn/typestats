#pragma once

#include "core/MediaClip.h"

#include <QImage>
#include <QWidget>
#include <memory>

class Av1Decoder;
class QLabel;
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
    const QImage &image() const { return m_image; }
    // The message instead of a picture ("no video here", the camera's error).
    void setMessage(const QString &text);

signals:
    void playToggled(bool play);
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    QRect pictureRect() const;
    void updateTimeLabel(qint64 docUs);

    bool m_live = false;
    bool m_recording = false;
    const MediaClip *m_clip = nullptr;
    std::unique_ptr<Av1Decoder> m_decoder;
    int m_key = -1, m_decoded = -1;  // the key frame and the last frame the decoder has had
    QImage m_image;
    QString m_message;
    QToolButton *m_play = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_rec = nullptr;
};
