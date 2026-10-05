#pragma once

#include <QString>

class QKeyEvent;

// The keys of the program as this system names them. The global hotkeys (Recorder) are the same keys
// everywhere, by position: Win is Super on Linux and Command on macOS, Alt is Option.
namespace Hotkeys {

QString clear();      // LCtrl+LWin: clears the recording
QString onOff();      // F8+F9: switches the capture
// Keys of the text pane (Memo4KeyDown) and of the main window. macOS has no Insert key: there marking
// is Cmd+I, and the delete key (Backspace) deletes too.
QString deleteKey();
QString mark();
QString copy();
QString undo();
QString textInput();  // F4

enum class TextAction { None, Delete, Mark, Copy };
TextAction textAction(const QKeyEvent *e);

}
