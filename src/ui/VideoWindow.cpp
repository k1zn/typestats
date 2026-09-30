#include "VideoWindow.h"

#include "core/Video.h"

#include <QPainter>

#ifndef TS_NO_VIDEO
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

bool VideoWindow::available()
{
    return true;
}

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
      m_player(new QMediaPlayer(this)), m_sink(new QVideoSink(this))
{
    setWindowTitle(tr("Видео"));
    setFixedSize(534, 334); // Form5 in the DFM; a video brings its own size
    move(380, 0);
    m_player->setVideoSink(m_sink);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoWindow::frameArrived);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (!m_loading)
            return;
        if (status == QMediaPlayer::LoadedMedia) {
            m_loading = false;
            m_loaded = true;
            const QSize size = m_player->metaData().value(QMediaMetaData::Resolution).toSize();
            if (size.isValid())
                setFixedSize(size);
            m_player->pause(); // decodes the first frame; frames come from seeking only
            seek();
            emit opened(true);
        } else if (status == QMediaPlayer::InvalidMedia) {
            m_loading = false;
            emit opened(false);
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error) {
        if (error == QMediaPlayer::NoError || (!m_loading && !m_loaded))
            return;
        const bool wasLoading = m_loading;
        m_loading = m_loaded = false;
        if (wasLoading)
            emit opened(false);
    });
}

void VideoWindow::open(const QString &path)
{
    unload();
    m_loading = true;
    m_player->setSource(QUrl::fromLocalFile(path));
}

void VideoWindow::unload()
{
    m_loading = m_loaded = false;
    m_position = 0;
    m_frame = QImage();
    m_player->stop();
    m_player->setSource(QUrl());
    update();
}

void VideoWindow::setPositionMs(qint64 ms)
{
    if (ms == m_position)
        return;
    m_position = ms;
    if (m_loaded)
        seek();
}

void VideoWindow::seek()
{
    // The player shows the frame nearest to its position: it is put to the start of the frame the
    // moment falls into, as AVIStreamTimeToSample picks it.
    const double fps = m_player->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    m_player->setPosition(Video::frameStartMs(m_position, fps, m_player->duration()));
}

void VideoWindow::frameArrived(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    m_frame = frame.toImage();
    if (!m_player->metaData().value(QMediaMetaData::Resolution).toSize().isValid() && !m_frame.isNull())
        setFixedSize(m_frame.size());
    update();
}

#else // TS_NO_VIDEO

#include <QTimer>

bool VideoWindow::available()
{
    return false;
}

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Видео"));
}

void VideoWindow::open(const QString &)
{
    QTimer::singleShot(0, this, [this] { emit opened(false); });
}

void VideoWindow::unload() {}

void VideoWindow::setPositionMs(qint64 ms)
{
    m_position = ms;
}

#endif

void VideoWindow::paintEvent(QPaintEvent *)
{
    // The frame stretched over the window, as StretchDIBits of TForm5.PaintBox1Paint.
    QPainter p(this);
    if (m_frame.isNull())
        p.fillRect(rect(), palette().window());
    else
        p.drawImage(rect(), m_frame);
}
