#include "VideoPropertiesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
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
    setWindowTitle(tr("Свойства видео"));
    auto *layout = new QVBoxLayout(this);

    auto *record = new QGroupBox(tr("Во время набора"));
    auto *form = new QFormLayout(record);
    m_video = new QCheckBox(tr("Записывать веб-камеру"));
    form->addRow(m_video);
    m_camera = new QComboBox;
    form->addRow(tr("Камера"), m_camera);
    m_quality = new QComboBox;
    // Sizes in the file are for a person at a keyboard: a still scene takes less.
    m_quality->addItem(tr("Экономно: 320×240, 10 к/с (~0,4 МБ/мин)"));
    m_quality->addItem(tr("Обычно: 640×360, 15 к/с (~0,9 МБ/мин)"));
    m_quality->addItem(tr("Хорошо: 640×480, 24 к/с (~1,9 МБ/мин)"));
    form->addRow(tr("Качество"), m_quality);
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
    m_mic->setEnabled(m_audio->isChecked());
}
