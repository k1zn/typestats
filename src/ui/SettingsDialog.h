#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

// "Настройки" (Form8), see re/settings.md. The controls show the stored settings; save() stores them.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void save() const;
    // Another language is picked than the one the dialog opened with.
    bool languageChanged() const;

private:
    friend class TstUi;

    QCheckBox *m_globalClear, *m_globalOnOff;
    QSpinBox *m_textFont, *m_klavFont, *m_digits;
    QLineEdit *m_loSpeed, *m_hiSpeed;
    QListWidget *m_mainStats;
    QCheckBox *m_copyColor, *m_copyStrike, *m_copyNext;
    QCheckBox *m_tray, *m_journal, *m_autoComments, *m_autoMinimize, *m_askSave;
    QComboBox *m_language;
    QString m_openedLanguage;
};
