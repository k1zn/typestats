#pragma once

#include "WebcamRecorder.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

// "Свойства видео" (Form6, button 21; re/webcam.md): what is recorded while typing, and the synchronization of the
// recording looked at.
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

private:
    void updateEnabled();

    QCheckBox *m_video = nullptr;
    QComboBox *m_camera = nullptr;
    QComboBox *m_quality = nullptr;
    QCheckBox *m_audio = nullptr;
    QComboBox *m_mic = nullptr;
    QSpinBox *m_shift = nullptr;
};
