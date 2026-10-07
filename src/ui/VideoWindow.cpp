#include "VideoWindow.h"

#include "media/Av1Codec.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint), m_decoder(std::make_unique<Av1Decoder>())
{
    setWindowTitle(tr("Видео"));
    resize(400, 270);
    setMinimumSize(200, 150);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    auto *bar = new QWidget;
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(4, 2, 4, 2);
    m_play = new QToolButton;
    m_play->setText(QStringLiteral("▶"));
    m_play->setCheckable(true);
    m_play->setToolTip(tr("Воспроизвести: клавограмма прокручивается вместе с видео"));
    connect(m_play, &QToolButton::toggled, this, [this](bool on) {
        m_play->setText(on ? QStringLiteral("❚❚") : QStringLiteral("▶"));
        emit playToggled(on);
    });
    m_time = new QLabel;
    m_rec = new QLabel(QStringLiteral("● ") + tr("Запись"));
    m_rec->setStyleSheet(QStringLiteral("color: #d00000; font-weight: bold"));
    m_rec->hide();
    row->addWidget(m_play);
    row->addWidget(m_time, 1);
    row->addWidget(m_rec);
    layout->addWidget(bar);
}

VideoWindow::~VideoWindow() = default;

void VideoWindow::setLive(bool live)
{
    if (m_live == live)
        return;
    m_live = live;
    m_play->setVisible(!live);
    m_time->setVisible(!live);
    if (live)
        m_play->setChecked(false);
    m_image = QImage();
    m_message.clear();
    update();
}

void VideoWindow::setRecording(bool recording)
{
    m_recording = recording;
    m_rec->setVisible(recording);
}

void VideoWindow::setPicture(const QImage &image)
{
    if (!m_live)
        return;
    m_image = image;
    m_message.clear();
    update();
}

void VideoWindow::setClip(const MediaClip *clip)
{
    m_clip = clip;
    m_decoder->reset();
    m_key = m_decoded = -1;
    if (!m_live) {
        m_image = QImage();
        update();
    }
}

void VideoWindow::setMessage(const QString &text)
{
    m_message = text;
    update();
}

void VideoWindow::setPlaying(bool playing)
{
    const QSignalBlocker b(m_play);
    m_play->setChecked(playing);
    m_play->setText(playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
}

void VideoWindow::showTime(qint64 docUs)
{
    if (m_live)
        return;
    updateTimeLabel(docUs);
    const auto [key, frame] = m_clip ? m_clip->videoFramesAt(docUs) : std::pair{-1, -1};
    if (frame < 0) {
        m_image = QImage();
        m_message = m_clip && !m_clip->isEmpty() ? QString() : tr("В этой записи нет видео");
        update();
        return;
    }
    if (frame == m_decoded && key == m_key)
        return;
    // Forward within the same run of frames: on from where the decoder is; else from the key frame.
    int from = key;
    if (key == m_key && m_decoded >= key && frame > m_decoded)
        from = m_decoded + 1;
    else
        m_decoder->reset();
    I420Frame picture;
    for (int i = from; i <= frame; ++i) {
        const MediaPacket &p = m_clip->packets[i];
        if (p.stream >= m_clip->streams.size() || m_clip->streams[p.stream].kind != MediaStream::Video)
            continue;
        const I420Frame f = m_decoder->decode(p.data);
        if (!f.isNull())
            picture = f;
    }
    m_key = key;
    m_decoded = frame;
    if (!picture.isNull())
        m_image = Yuv::toImage(picture);
    m_message.clear();
    update();
}

void VideoWindow::updateTimeLabel(qint64 docUs)
{
    const qint64 s = std::max<qint64>(0, docUs / 1000000);
    m_time->setText(QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')));
}

QRect VideoWindow::pictureRect() const
{
    const QRect area(0, 0, width(), height() - m_play->parentWidget()->height());
    if (m_image.isNull())
        return area;
    QSize s = m_image.size();
    s.scale(area.size(), Qt::KeepAspectRatio);
    return QRect(area.x() + (area.width() - s.width()) / 2, area.y() + (area.height() - s.height()) / 2, s.width(),
                 s.height());
}

void VideoWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect area(0, 0, width(), height() - m_play->parentWidget()->height());
    p.fillRect(area, Qt::black);
    if (!m_image.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(pictureRect(), m_image);
    }
    if (!m_message.isEmpty()) {
        p.setPen(QColor(200, 200, 200));
        p.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
}

void VideoWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    emit visibilityChanged(true);
}

void VideoWindow::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    m_play->setChecked(false);
    emit visibilityChanged(false);
}
