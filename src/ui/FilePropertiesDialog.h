#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

// "Свойства файла" (Form2): author, date and description, asked for before a file is saved.
class FilePropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilePropertiesDialog(QWidget *parent = nullptr);

    void setProperties(const QString &author, const QString &date, const QString &description, bool authorReadOnly);
    QString author() const;
    QString date() const;
    QString description() const;

private:
    QLineEdit *m_author, *m_date;
    QPlainTextEdit *m_description;
};
