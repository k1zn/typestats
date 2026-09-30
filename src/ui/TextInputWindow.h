#pragma once

#include <QWidget>

class QTextEdit;

// "Ввод текста" (Form9): a small window that stays on top to type into; what is typed there is
// recorded like typing anywhere else. Esc hides it, F2 empties it.
class TextInputWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TextInputWindow(QWidget *parent = nullptr);

    void clear();
    void setFontSize(int points);

    void loadSettings();
    void saveSettings() const;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    friend class TstUi;

    QTextEdit *m_edit;
    bool m_placed = false;
};
