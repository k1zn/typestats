#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

// "Свойства видео" (Form6): whether a video is attached, its file and its time shift (re/video.md).
class VideoPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VideoPropertiesDialog(QWidget *parent = nullptr);

    void setProperties(bool attached, const QString &fileName, int shiftMs);
    bool attached() const;
    QString fileName() const;
    // StrToIntDef(text, 0).
    int shiftMs() const;

private:
    QCheckBox *m_attached;
    QLineEdit *m_fileName, *m_shift;
};
