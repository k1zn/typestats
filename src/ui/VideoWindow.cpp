#include "VideoWindow.h"

#include "media/AudioOut.h"
#include "media/Av1Codec.h"
#include "media/OpusCodec.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint), m_decoder(std::make_unique<Av1Decoder>()),
      m_soundOut(std::make_unique<AudioOut>()), m_soundDecoder(std::make_unique<OpusAudioDecoder>())
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
    m_save = new QToolButton;
    m_save->setText(QStringLiteral("💾"));
    m_save->setToolTip(tr("Сохранить видео в файл WebM (открывается в браузере и проигрывателях)"));
    connect(m_save, &QToolButton::clicked, this, &VideoWindow::saveRequested);
    row->addWidget(m_play);
    row->addWidget(m_time, 1);
    row->addWidget(m_rec);
    row->addWidget(m_save);
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
    m_save->setVisible(!live);
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

void VideoWindow::startSound(qint64 docUs)
{
    stopSound();
    if (!m_clip || m_clip->streamOf(MediaStream::Audio) < 0 || !m_soundOut->open(OpusAudioEncoder::kRate))
        return;
    m_soundDecoder->reset();
    // The sound goes with the pictures: the same correction by hand.
    const qint64 from = docUs - qint64(m_clip->shiftMs) * 1000;
    m_soundNext = int(m_clip->packets.size());
    for (int i = 0; i < m_clip->packets.size(); ++i) {
        const MediaPacket &p = m_clip->packets[i];
        if (m_clip->streams[p.stream].kind == MediaStream::Audio
            && m_clip->docTime(p.ptsUs) + MediaClip::kOpusPacket > from - MediaClip::kOpusPreroll) {
            m_soundNext = i;
            break;
        }
    }
    m_soundEndUs = from;
    m_sound = true;
}

void VideoWindow::feedSound(qint64 docUs)
{
    if (!m_sound)
        return;
    const qint64 until = docUs - qint64(m_clip->shiftMs) * 1000 + 300000; // a little ahead
    constexpr int perMs = OpusAudioEncoder::kRate / 1000;
    while (m_soundNext < m_clip->packets.size()) {
        const MediaPacket &p = m_clip->packets[m_soundNext];
        if (m_clip->streams[p.stream].kind != MediaStream::Audio) {
            ++m_soundNext;
            continue;
        }
        const qint64 t = m_clip->docTime(p.ptsUs);
        if (t > until)
            break;
        ++m_soundNext;
        QVector<qint16> pcm = m_soundDecoder->decode(p.data); // the preroll too: the decoder settles
        const qint64 end = t + qint64(pcm.size()) * 1000 / perMs;
        if (end <= m_soundEndUs)
            continue;
        if (t < m_soundEndUs) {
            pcm.remove(0, std::min<qsizetype>(pcm.size(), (m_soundEndUs - t) * perMs / 1000));
        } else if (t > m_soundEndUs + 5000) { // a pause in recording: silence in its place
            m_soundOut->write(QVector<qint16>(qsizetype((t - m_soundEndUs) * perMs / 1000), 0));
        }
        m_soundOut->write(pcm);
        m_soundEndUs = end;
    }
}

void VideoWindow::stopSound()
{
    m_sound = false;
    m_soundOut->close();
}
