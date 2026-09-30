#include "TextInputWindow.h"

#include <QKeyEvent>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>

TextInputWindow::TextInputWindow(QWidget *parent) : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Ввод текста"));
    m_edit = new QTextEdit;
    m_edit->setAcceptRichText(false);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_edit->setFont(QFont(QStringLiteral("Arial")));
    m_edit->installEventFilter(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_edit);
    setFontSize(12);
    resize(451, 74);
}

void TextInputWindow::clear()
{
    m_edit->clear();
}

void TextInputWindow::setFontSize(int points)
{
    QFont f = m_edit->font();
    f.setPixelSize(qRound(points * 96.0 / 72.0));
    m_edit->setFont(f);
}

bool TextInputWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        if (key == Qt::Key_Escape) {
            hide();
            return true;
        }
        if (key == Qt::Key_F2) {
            clear();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void TextInputWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_placed = true;
    m_edit->setFocus();
}

void TextInputWindow::loadSettings()
{
    const QSettings s;
    if (!s.contains(QStringLiteral("TextWinLeft")))
        return;
    move(s.value(QStringLiteral("TextWinLeft")).toInt(), s.value(QStringLiteral("TextWinTop")).toInt());
    resize(s.value(QStringLiteral("TextWinWidth"), 451).toInt(), s.value(QStringLiteral("TextWHeight"), 74).toInt());
}

void TextInputWindow::saveSettings() const
{
    if (!m_placed)
        return;
    QSettings s;
    s.setValue(QStringLiteral("TextWinLeft"), x());
    s.setValue(QStringLiteral("TextWinTop"), y());
    s.setValue(QStringLiteral("TextWinWidth"), width());
    s.setValue(QStringLiteral("TextWHeight"), height());
}
