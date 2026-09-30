#pragma once

#include <QDialog>

// "О программе" (Form7).
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
