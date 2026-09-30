#pragma once

#include "core/Recalc.h"

#include <QTextEdit>

// The text of the recording (Form1.Memo4): read-only, erased characters in red, fragment separators
// and injected input in blue, comments in green, marked characters underlined.
class TextView : public QTextEdit
{
    Q_OBJECT
public:
    explicit TextView(QWidget *parent = nullptr);

    void setModel(const TextModel &m);

    // Selection in text positions of the model (a paragraph break is one position).
    int selectionStart() const;
    int selectionLength() const;

    // Highlights the part of the text shown on the klavogram.
    void setVisibleRange(int from, int to);

signals:
    // Del, Ins and Ctrl+C / Ctrl+Ins (with a selection) in the text.
    void deleteRequested();
    void markRequested();
    void copyRequested();
    void menuRequested(const QPoint &globalPos);
    // The mouse is over the character at textPos (-1: over none).
    void hovered(int textPos, const QPoint &globalPos);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
};
