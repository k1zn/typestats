#include "TextView.h"

#include <QTextCursor>

namespace {

QTextCharFormat formatOf(quint8 style)
{
    QTextCharFormat f;
    if (style & (TextStyle::Separator | TextStyle::Injected))
        f.setForeground(QColor(0, 0, 255));
    else if (style & TextStyle::Comment)
        f.setForeground(QColor(0, 128, 0));
    else if (style & TextStyle::Erased)
        f.setForeground(QColor(255, 0, 0));
    if (style & TextStyle::Marked)
        f.setFontUnderline(true);
    return f;
}

} // namespace

TextView::TextView(QWidget *parent) : QTextEdit(parent)
{
    setReadOnly(true);
    setAcceptRichText(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont f(QStringLiteral("Arial"));
    f.setPixelSize(16);
    setFont(f);
}

void TextView::setModel(const TextModel &m)
{
    const QSignalBlocker blocker(this);
    setPlainText(m.text);
    QTextCursor c(document());
    c.beginEditBlock();
    for (const TextRun &r : m.runs) {
        c.setPosition(r.start);
        c.setPosition(r.start + r.length, QTextCursor::KeepAnchor);
        c.mergeCharFormat(formatOf(r.style));
    }
    c.endEditBlock();
    moveCursor(QTextCursor::Start);
}

int TextView::selectionStart() const
{
    return textCursor().selectionStart();
}

int TextView::selectionLength() const
{
    const QTextCursor c = textCursor();
    return c.selectionEnd() - c.selectionStart();
}
