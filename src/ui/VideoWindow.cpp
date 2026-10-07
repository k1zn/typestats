#include "VideoWindow.h"

#include "media/AudioOut.h"
#include "media/Av1Codec.h"
#include "media/OpusCodec.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {

// A click on the groove goes to that place at once (not a page step), and the handle can be dragged on from there.
class SeekSlider : public QSlider
{
public:
    SeekSlider() : QSlider(Qt::Horizontal) {}

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            QStyleOptionSlider opt;
            initStyleOption(&opt);
            const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            if (!handle.contains(e->position().toPoint())) {
                const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
                const int x = e->position().toPoint().x() - groove.x() - handle.width() / 2;
                setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), x, groove.width() - handle.width(),
                                                         opt.upsideDown));
            }
        }
        QSlider::mousePressEvent(e);
    }
};

} // namespace

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
    m_play->setCheckable(true);
    m_play->setAutoRaise(true);
    m_play->setToolTip(tr("Воспроизвести: клавограмма прокручивается вместе с видео"));
    connect(m_play, &QToolButton::toggled, this, [this](bool on) {
        m_play->setIcon(on ? m_pauseIcon : m_playIcon);
        emit playToggled(on);
    });
    m_slider = new SeekSlider;
    m_slider->setSingleStep(1000);
    m_slider->setPageStep(5000);
    m_slider->setToolTip(tr("Перемотка (стрелки — на секунду)"));
    connect(m_slider, &QSlider::valueChanged, this, [this](int ms) {
        updateTimeLabel(m_spanFromUs + qint64(ms) * 1000);
        emit seekRequested(m_spanFromUs + qint64(ms) * 1000);
    });
    // Dragged while playing: the playback waits and goes on from where the handle is let go.
    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        m_resume = m_play->isChecked();
        if (m_resume)
            m_play->setChecked(false);
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        if (m_resume)
            m_play->setChecked(true);
        m_resume = false;
    });
    m_time = new QLabel;
    m_rec = new QLabel(QStringLiteral("● ") + tr("Запись"));
    m_rec->setStyleSheet(QStringLiteral("color: #d00000; font-weight: bold"));
    m_rec->hide();
    m_save = new QToolButton;
    m_save->setAutoRaise(true);
    m_save->setToolTip(tr("Сохранить видео в файл WebM (открывается в браузере и проигрывателях)"));
    connect(m_save, &QToolButton::clicked, this, &VideoWindow::saveRequested);
    row->addWidget(m_play);
    row->addWidget(m_slider, 1);
    row->addWidget(m_time);
    row->addStretch(0);
    row->addWidget(m_rec);
    row->addWidget(m_save);
    layout->addWidget(bar);
    updateIcons();
    updateTimeLabel(0);
}

void VideoWindow::updateIcons()
{
    const qreal dpr = devicePixelRatioF();
    const QColor ink = palette().color(QPalette::ButtonText);
    auto draw = [&](auto paint) {
        constexpr int side = 16;
        QPixmap pm(QSize(side, side) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        paint(p);
        return QIcon(pm);
    };
    m_playIcon = draw([](QPainter &p) {
        QPainterPath path;
        path.moveTo(4, 2.5);
        path.lineTo(13.5, 8);
        path.lineTo(4, 13.5);
        path.closeSubpath();
        p.drawPath(path);
    });
    m_pauseIcon = draw([](QPainter &p) {
        p.drawRoundedRect(QRectF(3.5, 2.5, 3.5, 11), 0.8, 0.8);
        p.drawRoundedRect(QRectF(9, 2.5, 3.5, 11), 0.8, 0.8);
    });
    m_play->setIcon(m_play->isChecked() ? m_pauseIcon : m_playIcon);
    m_save->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton, nullptr, this));
}

void VideoWindow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        updateIcons();
}

void VideoWindow::setSpan(qint64 fromUs, qint64 toUs)
{
    m_spanFromUs = fromUs;
    m_spanToUs = std::max(fromUs, toUs);
    const QSignalBlocker b(m_slider);
    m_slider->setRange(0, int(std::min<qint64>((m_spanToUs - m_spanFromUs) / 1000, std::numeric_limits<int>::max())));
}

VideoWindow::~VideoWindow() = default;

void VideoWindow::setLive(bool live)
{
    if (m_live == live)
        return;
    m_live = live;
    m_play->setVisible(!live);
    m_slider->setVisible(!live);
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
    m_play->setIcon(playing ? m_pauseIcon : m_playIcon);
}

void VideoWindow::showTime(qint64 docUs)
{
    if (m_live)
        return;
    if (!m_slider->isSliderDown()) { // not under the hand
        const QSignalBlocker b(m_slider);
        m_slider->setValue(int(std::clamp<qint64>((docUs - m_spanFromUs) / 1000, 0, m_slider->maximum())));
        updateTimeLabel(docUs);
    }
    // Nothing recorded: nothing to play, seek or save.
    const bool any = m_clip && !m_clip->isEmpty();
    m_play->setEnabled(any);
    m_slider->setEnabled(any);
    m_time->setEnabled(any);
    m_save->setEnabled(any);
    if (!any)
        m_play->setChecked(false);
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
    // From the span's start: the time of the recording, as players show it.
    auto text = [](qint64 us) {
        const qint64 s = std::max<qint64>(0, us / 1000000);
        return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    };
    m_time->setText(text(docUs - m_spanFromUs) + QStringLiteral(" / ") + text(m_spanToUs - m_spanFromUs));
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
