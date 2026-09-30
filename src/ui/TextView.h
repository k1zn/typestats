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
};
