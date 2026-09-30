#include "TextView.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QScrollBar>
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
    setFont(QFont(QStringLiteral("Arial")));
    setFontSize(12);
    viewport()->setMouseTracking(true);
}

void TextView::setFontSize(int points)
{
    // The size the original gets at 96 dpi: 12 pt is 16 px.
    QFont f = font();
    f.setPixelSize(qRound(points * 96.0 / 72.0));
    setFont(f);
}

void TextView::keyPressEvent(QKeyEvent *e)
{
    // Memo4KeyDown.
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    if (mods == Qt::NoModifier && e->key() == Qt::Key_Delete) {
        emit deleteRequested();
    } else if (mods == Qt::NoModifier && e->key() == Qt::Key_Insert) {
        emit markRequested();
    } else if (mods == Qt::ControlModifier && (e->key() == Qt::Key_Insert || e->key() == Qt::Key_C)) {
        if (textCursor().hasSelection())
            emit copyRequested();
    } else {
        QTextEdit::keyPressEvent(e);
        return;
    }
    e->accept();
}

void TextView::contextMenuEvent(QContextMenuEvent *e)
{
    emit menuRequested(e->globalPos());
}

void TextView::mouseMoveEvent(QMouseEvent *e)
{
    QTextEdit::mouseMoveEvent(e);
    const QPointF point = e->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    emit hovered(document()->documentLayout()->hitTest(point, Qt::ExactHit), e->globalPosition().toPoint());
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

void TextView::setVisibleRange(int from, int to)
{
    QList<QTextEdit::ExtraSelection> selections;
    if (from < to) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(from);
        s.cursor.setPosition(std::min(to, document()->characterCount() - 1), QTextCursor::KeepAnchor);
        s.format.setBackground(QColor(255, 255, 0));
        selections.append(s);
    }
    setExtraSelections(selections);
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
