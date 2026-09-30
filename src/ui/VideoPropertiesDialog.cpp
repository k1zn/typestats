#include "VideoPropertiesDialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

VideoPropertiesDialog::VideoPropertiesDialog(QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint)
{
    // No system buttons and no "Cancel": OK applies what is in the fields.
    setWindowTitle(tr("Свойства видео"));
    setFixedSize(140, 142);
    m_attached = new QCheckBox(tr("Прикрепить видео"), this);
    m_attached->setGeometry(8, 8, 121, 17);
    auto *nameLabel = new QLabel(tr("Имя файла"), this);
    nameLabel->move(8, 32);
    m_fileName = new QLineEdit(this);
    m_fileName->setGeometry(8, 48, 121, 21);
    nameLabel->setBuddy(m_fileName);
    auto *shiftLabel = new QLabel(tr("Сдвиг времени, мс"), this);
    shiftLabel->move(8, 72);
    m_shift = new QLineEdit(this);
    m_shift->setGeometry(8, 88, 121, 21);
    shiftLabel->setBuddy(m_shift);
    auto *ok = new QPushButton(tr("OK"), this);
    ok->setGeometry(32, 112, 75, 25);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
}

void VideoPropertiesDialog::setProperties(bool attached, const QString &fileName, int shiftMs)
{
    m_attached->setChecked(attached);
    m_fileName->setText(fileName);
    m_shift->setText(QString::number(shiftMs));
}

bool VideoPropertiesDialog::attached() const
{
    return m_attached->isChecked();
}

QString VideoPropertiesDialog::fileName() const
{
    return m_fileName->text();
}

int VideoPropertiesDialog::shiftMs() const
{
    bool ok = false;
    const int v = m_shift->text().trimmed().toInt(&ok);
    return ok ? v : 0;
}
