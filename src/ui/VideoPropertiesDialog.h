#pragma once

#include "WebcamRecorder.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

// "Свойства камеры" (the menu of the webcam's button; re/webcam.md): what is recorded while typing, the picture of the
// camera chosen (to place it), and the synchronization of the recording looked at.
class VideoPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VideoPropertiesDialog(QWidget *parent = nullptr);

    void setSettings(const WebcamRecorder::Settings &s);
    WebcamRecorder::Settings settings() const;
    // The correction of the document's clip; disabled when there is no video.
    void setShiftMs(int ms, bool enabled);
    int shiftMs() const;
    // The picture of the camera, or why there is none.
    void setPicture(const QImage &image);
    void setPreviewMessage(const QString &text);

signals:
    void cameraChanged(); // another camera or size: the picture follows

private:
    friend class TstUi;
    void updateEnabled();
    void updateEstimate();

    QCheckBox *m_video = nullptr;
    QComboBox *m_camera = nullptr;
    QLabel *m_preview = nullptr;
    QComboBox *m_quality = nullptr;
    QWidget *m_custom = nullptr; // "Своё": size, fps, bitrate
    QSpinBox *m_width = nullptr, *m_height = nullptr, *m_fps = nullptr, *m_kbps = nullptr;
    QLabel *m_estimate = nullptr;
    bool m_customTouched = false; // "Своё" has its own values (else it starts from the quality chosen before)
    int m_lastQuality = WebcamRecorder::Normal;
    QCheckBox *m_audio = nullptr;
    QComboBox *m_mic = nullptr;
    QSpinBox *m_shift = nullptr;
};
