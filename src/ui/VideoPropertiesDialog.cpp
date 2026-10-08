#include "VideoPropertiesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

void fillDevices(QComboBox *box, const QList<CaptureDevice> &devices, const QString &selected)
{
    box->clear();
    box->addItem(QObject::tr("По умолчанию"), QString());
    for (const CaptureDevice &d : devices)
        box->addItem(d.name, d.id);
    const int i = box->findData(selected);
    if (i >= 0) {
        box->setCurrentIndex(i);
    } else if (!selected.isEmpty()) { // not connected now: kept
        box->addItem(QObject::tr("(не подключено)"), selected);
        box->setCurrentIndex(box->count() - 1);
    }
}

} // namespace

VideoPropertiesDialog::VideoPropertiesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Свойства камеры"));
    auto *layout = new QVBoxLayout(this);

    auto *record = new QGroupBox(tr("Во время набора"));
    auto *form = new QFormLayout(record);
    m_video = new QCheckBox(tr("Записывать веб-камеру"));
    form->addRow(m_video);
    m_camera = new QComboBox;
    form->addRow(tr("Камера"), m_camera);
    // What the camera sees, while the dialog is open: to place it.
    m_preview = new QLabel;
    m_preview->setFixedSize(256, 144);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setWordWrap(true);
    m_preview->setStyleSheet(QStringLiteral("QLabel { background: black; color: #c8c8c8; }"));
    form->addRow(QString(), m_preview);
    connect(m_camera, &QComboBox::currentIndexChanged, this, &VideoPropertiesDialog::cameraChanged);
    m_quality = new QComboBox;
    // The sizes - of the camera's picture in the quality's frame (updateSizes).
    for (int i = 0; i < 4; ++i)
        m_quality->addItem(QString());
    updateSizes();
    form->addRow(tr("Качество"), m_quality);

    auto *customRow = new QWidget;
    auto *custom = new QHBoxLayout(customRow);
    custom->setContentsMargins(0, 0, 0, 0);
    auto spin = [](int from, int to, int step, const QString &suffix) {
        auto *box = new QSpinBox;
        box->setRange(from, to);
        box->setSingleStep(step);
        box->setSuffix(suffix);
        return box;
    };
    m_width = spin(160, 1920, 2, {});
    m_height = spin(120, 1080, 2, {});
    m_fps = spin(1, 30, 1, tr(" к/с"));
    m_kbps = spin(10, 4000, 10, tr(" кбит/с"));
    m_width->setToolTip(tr("Ширина кадра; камера с другим размером масштабируется"));
    m_height->setToolTip(tr("Высота кадра"));
    m_fps->setToolTip(tr("Кадров в секунду"));
    m_kbps->setToolTip(tr("Поток видео: больше — чётче, но файл крупнее. Подбирается по размеру и частоте кадров, "
                          "пока не поставлен свой"));
    // "Авто": the bitrate follows the size and the rate of frames until one of the user's own is typed.
    m_autoKbps = new QToolButton;
    m_autoKbps->setText(tr("авто"));
    m_autoKbps->setCheckable(true);
    m_autoKbps->setChecked(true);
    m_autoKbps->setToolTip(tr("Подбирать поток по размеру и частоте кадров"));
    custom->addWidget(m_width);
    custom->addWidget(new QLabel(QStringLiteral("×")));
    custom->addWidget(m_height);
    custom->addWidget(m_fps);
    custom->addWidget(m_kbps);
    custom->addWidget(m_autoKbps);
    custom->addStretch(1);
    m_estimate = new QLabel;
    m_custom = new QWidget;
    auto *customRows = new QVBoxLayout(m_custom);
    customRows->setContentsMargins(0, 0, 0, 0);
    customRows->addWidget(customRow);
    customRows->addWidget(m_estimate);
    form->addRow(QString(), m_custom);
    for (QSpinBox *box : {m_width, m_height, m_fps, m_kbps}) {
        connect(box, &QSpinBox::valueChanged, this, &VideoPropertiesDialog::updateEstimate);
        connect(box, &QSpinBox::editingFinished, this, [this] { m_customTouched = true; });
    }
    for (QSpinBox *box : {m_width, m_height, m_fps})
        connect(box, &QSpinBox::valueChanged, this, &VideoPropertiesDialog::suggestKbps);
    connect(m_kbps, &QSpinBox::valueChanged, this, [this] {
        if (!m_settingKbps) // typed by the user: theirs from now on
            m_autoKbps->setChecked(false);
    });
    connect(m_autoKbps, &QToolButton::toggled, this, &VideoPropertiesDialog::suggestKbps);
    connect(m_quality, &QComboBox::currentIndexChanged, this, [this](int i) {
        // "Своё" starts from the quality chosen before (its bitrate, then following the size).
        if (i == WebcamRecorder::Custom && !m_customTouched) {
            const WebcamRecorder::Preset p = WebcamRecorder::preset(m_lastQuality);
            m_width->setValue(p.width);
            m_height->setValue(p.height);
            m_fps->setValue(p.fps);
            setKbps(p.kbps);
            m_autoKbps->setChecked(true);
        }
        if (i != WebcamRecorder::Custom)
            m_lastQuality = i;
        updateEnabled();
        emit cameraChanged();
    });
    m_audio = new QCheckBox(tr("Записывать звук с микрофона (~0,15 МБ/мин)"));
    form->addRow(m_audio);
    m_mic = new QComboBox;
    form->addRow(tr("Микрофон"), m_mic);
    auto *note = new QLabel(tr("Видео сохраняется внутри файла .tsf. «Сохранить блок» обрезает его по времени "
                               "выделенного текста. С метками времени видео заверяется вместе с набором."));
    note->setWordWrap(true);
    form->addRow(note);
    layout->addWidget(record);

    auto *sync = new QGroupBox(tr("Эта запись"));
    auto *syncForm = new QFormLayout(sync);
    m_shift = new QSpinBox;
    m_shift->setRange(-5000, 5000);
    m_shift->setSingleStep(10);
    m_shift->setSuffix(tr(" мс"));
    m_shift->setToolTip(tr("Положительный сдвиг показывает кадры позже: если на видео клавиша нажимается раньше, "
                           "чем на клавограмме"));
    syncForm->addRow(tr("Сдвиг видео"), m_shift);
    layout->addWidget(sync);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_video, &QCheckBox::toggled, this, &VideoPropertiesDialog::updateEnabled);
    connect(m_audio, &QCheckBox::toggled, this, &VideoPropertiesDialog::updateEnabled);
}

void VideoPropertiesDialog::setSettings(const WebcamRecorder::Settings &s)
{
    m_video->setChecked(s.video);
    fillDevices(m_camera, Camera::devices(), s.camera);
    m_customTouched = s.custom != WebcamRecorder::preset(WebcamRecorder::Normal);
    m_lastQuality = s.quality == WebcamRecorder::Custom ? WebcamRecorder::Normal : s.quality;
    {
        const QSignalBlocker b(m_autoKbps);
        m_autoKbps->setChecked(false); // the values as saved
    }
    m_width->setValue(s.custom.width);
    m_height->setValue(s.custom.height);
    m_fps->setValue(s.custom.fps);
    setKbps(s.custom.kbps);
    // Auto when never set, or when it is what auto would give.
    const QSignalBlocker b(m_autoKbps);
    m_autoKbps->setChecked(!m_customTouched
                           || s.custom.kbps == WebcamRecorder::Preset::suggestedKbps(s.custom.width, s.custom.height,
                                                                                    s.custom.fps));
    m_quality->setCurrentIndex(s.quality);
    m_audio->setChecked(s.audio);
    fillDevices(m_mic, Microphone::devices(), s.microphone);
    updateEnabled();
}

WebcamRecorder::Settings VideoPropertiesDialog::settings() const
{
    WebcamRecorder::Settings s;
    s.video = m_video->isChecked();
    s.camera = m_camera->currentData().toString();
    s.quality = m_quality->currentIndex();
    s.custom = WebcamRecorder::Preset{m_width->value(), m_height->value(), m_fps->value(), m_kbps->value()}.bounded();
    s.audio = m_audio->isChecked();
    s.microphone = m_mic->currentData().toString();
    return s;
}

void VideoPropertiesDialog::setShiftMs(int ms, bool enabled)
{
    m_shift->setValue(ms);
    m_shift->setEnabled(enabled);
}

int VideoPropertiesDialog::shiftMs() const
{
    return m_shift->value();
}

void VideoPropertiesDialog::updateEnabled()
{
    m_camera->setEnabled(m_video->isChecked());
    m_quality->setEnabled(m_video->isChecked());
    m_custom->setVisible(m_quality->currentIndex() == WebcamRecorder::Custom);
    m_custom->setEnabled(m_video->isChecked());
    m_mic->setEnabled(m_audio->isChecked());
    updateEstimate();
    adjustSize();
}

void VideoPropertiesDialog::setKbps(int kbps)
{
    m_settingKbps = true;
    m_kbps->setValue(kbps);
    m_settingKbps = false;
}

void VideoPropertiesDialog::suggestKbps()
{
    if (m_autoKbps->isChecked())
        setKbps(WebcamRecorder::Preset::suggestedKbps(m_width->value(), m_height->value(), m_fps->value()));
}

void VideoPropertiesDialog::setPicture(const QImage &image)
{
    QPixmap pm = QPixmap::fromImage(
        image.scaled(m_preview->size() * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pm.setDevicePixelRatio(devicePixelRatioF());
    m_preview->setPixmap(pm);
    if (image.size() != m_cameraSize) {
        m_cameraSize = image.size();
        updateSizes();
        updateEstimate();
    }
}

QSize VideoPropertiesDialog::recordedSize(const WebcamRecorder::Preset &p) const
{
    // The quality's size is a frame: the camera's picture goes into it whole (WebcamRecorder::fit).
    return WebcamRecorder::fit(m_cameraSize, QSize(p.width, p.height));
}

void VideoPropertiesDialog::updateSizes()
{
    // Sizes in the file are for a person at a keyboard: a still scene takes less.
    auto size = [this](int quality) {
        const QSize s = recordedSize(WebcamRecorder::preset(quality));
        return QStringLiteral("%1×%2").arg(s.width()).arg(s.height());
    };
    m_quality->setItemText(WebcamRecorder::Economy, tr("Экономно: %1, 10 к/с (~0,4 МБ/мин)").arg(size(WebcamRecorder::Economy)));
    m_quality->setItemText(WebcamRecorder::Normal, tr("Обычно: %1, 15 к/с (~0,9 МБ/мин)").arg(size(WebcamRecorder::Normal)));
    m_quality->setItemText(WebcamRecorder::Good, tr("Хорошо: %1, 24 к/с (~1,9 МБ/мин)").arg(size(WebcamRecorder::Good)));
    m_quality->setItemText(WebcamRecorder::Custom, tr("Своё…"));
}

void VideoPropertiesDialog::setPreviewMessage(const QString &text)
{
    m_preview->setText(text);
}

void VideoPropertiesDialog::updateEstimate()
{
    // As the presets: ~1 MB a minute per 100 kbit/s (a person at a keyboard, key frames included); the frame needs
    // the bits - too few for its size and the picture is blurred.
    const double mbPerMin = m_kbps->value() / 100.0;
    const double bitsPerPixel = m_kbps->value() * 1000.0 / (double(m_width->value()) * m_height->value() * m_fps->value());
    QString text = tr("~%1 МБ/мин").arg(QLocale().toString(mbPerMin, 'f', mbPerMin < 10 ? 1 : 0));
    // The frame typed is a frame too: what is recorded of this camera, when it is not the same.
    const WebcamRecorder::Preset p = WebcamRecorder::Preset{m_width->value(), m_height->value(), m_fps->value(), 10}.bounded();
    const QSize recorded = recordedSize(p);
    if (recorded != QSize(p.width, p.height))
        text = tr("запишется %1×%2").arg(recorded.width()).arg(recorded.height()) + QStringLiteral(", ") + text;
    if (bitsPerPixel < 0.01)
        text += QStringLiteral(" · ") + tr("мало для такого кадра: картинка будет размытой");
    m_estimate->setText(text);
}
