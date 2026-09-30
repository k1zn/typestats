#pragma once

#include <QImage>
#include <QWidget>

class QMediaPlayer;
class QVideoFrame;
class QVideoSink;

// "Видео" (Form5): the frame of the attached video at a given moment (re/video.md). The window
// takes the size of the frame and stays on top; nothing is played, the klavogram moves the video.
class VideoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWindow(QWidget *parent = nullptr);
    // False in a build without QtMultimedia (TS_VIDEO=OFF).
    static bool available();

    // Opens a video file; opened() tells whether it can be shown.
    void open(const QString &path);
    void unload();
    bool isLoaded() const { return m_loaded; }
    // Shows the frame at this moment, ms (the last frame past the end).
    void setPositionMs(qint64 ms);
    qint64 positionMs() const { return m_position; }
    const QImage &frame() const { return m_frame; }

signals:
    void opened(bool ok);

protected:
    void paintEvent(QPaintEvent *) override;

private:
#ifndef TS_NO_VIDEO
    void frameArrived(const QVideoFrame &frame);
    void seek();

    QMediaPlayer *m_player;
    QVideoSink *m_sink;
#endif
    QImage m_frame;
    bool m_loading = false;
    bool m_loaded = false;
    qint64 m_position = 0;
};
