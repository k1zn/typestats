#include "TextView.h"

#include "Hotkeys.h"
#include "Look.h"

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
    f.setPixelSize(Look::pointsToPixels(points));
    setFont(f);
}

void TextView::keyPressEvent(QKeyEvent *e)
{
    // Memo4KeyDown.
    switch (Hotkeys::textAction(e)) {
    case Hotkeys::TextAction::Delete:
        emit deleteRequested();
        break;
    case Hotkeys::TextAction::Mark:
        emit markRequested();
        break;
    case Hotkeys::TextAction::Copy:
        if (textCursor().hasSelection())
            emit copyRequested();
        break;
    case Hotkeys::TextAction::None:
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
    // The text goes in piece by piece with its formats: restyling runs of a ready text costs far more.
    setPlainText(QString());
    QTextDocument *doc = document();
    doc->setUndoRedoEnabled(false);
    QTextCursor c(doc);
    const QTextCharFormat base = c.charFormat();
    c.beginEditBlock();
    qsizetype pos = 0;
    for (const TextRun &r : m.runs) {
        if (r.start > pos)
            c.insertText(m.text.mid(pos, r.start - pos), base);
        QTextCharFormat f = base;
        f.merge(formatOf(r.style));
        c.insertText(m.text.mid(r.start, r.length), f);
        pos = r.start + r.length;
    }
    if (pos < m.text.size())
        c.insertText(m.text.mid(pos), base);
    c.endEditBlock();
    doc->setUndoRedoEnabled(true);
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
