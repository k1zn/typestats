#include "FilePropertiesDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

FilePropertiesDialog::FilePropertiesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Свойства файла"));
    setFixedSize(259, 198);
    (new QLabel(tr("Автор"), this))->move(8, 0);
    (new QLabel(tr("Дата"), this))->move(152, 0);
    (new QLabel(tr("Описание"), this))->move(8, 40);
    m_author = new QLineEdit(this);
    m_author->setGeometry(0, 16, 137, 21);
    m_date = new QLineEdit(this);
    m_date->setGeometry(144, 16, 113, 21);
    m_date->setReadOnly(true);
    m_description = new QPlainTextEdit(this);
    m_description->setGeometry(0, 56, 257, 105);
    auto *ok = new QPushButton(tr("OK"), this);
    ok->setGeometry(44, 168, 75, 25);
    ok->setDefault(true);
    auto *cancel = new QPushButton(tr("Отмена"), this);
    cancel->setGeometry(144, 168, 75, 25);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_description->setFocus();
}

void FilePropertiesDialog::setProperties(const QString &author, const QString &date, const QString &description,
                                         bool authorReadOnly)
{
    m_author->setText(author);
    m_author->setReadOnly(authorReadOnly);
    m_date->setText(date);
    m_description->setPlainText(description);
}

QString FilePropertiesDialog::author() const
{
    return m_author->text();
}

QString FilePropertiesDialog::date() const
{
    return m_date->text();
}

QString FilePropertiesDialog::description() const
{
    return m_description->toPlainText();
}
