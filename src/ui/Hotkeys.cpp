#include "Hotkeys.h"

#include <QKeyEvent>
#include <QKeySequence>

namespace Hotkeys {

namespace {

QString native(QKeyCombination key)
{
    // Ctrl is Command on macOS (Qt swaps them), and the sequence is written as the system writes it: ⌘C.
    return QKeySequence(key).toString(QKeySequence::NativeText);
}

} // namespace

QString clear()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("L⌃+L⌘");
#elif defined(Q_OS_WIN)
    return QStringLiteral("LCtrl+LWin");
#else
    return QStringLiteral("LCtrl+LSuper");
#endif
}

QString onOff()
{
    return QStringLiteral("F8+F9");
}

QString deleteKey()
{
#ifdef Q_OS_MACOS
    return native(Qt::Key_Backspace);
#else
    return native(Qt::Key_Delete);
#endif
}

QString mark()
{
#ifdef Q_OS_MACOS
    return native(Qt::CTRL | Qt::Key_I);
#else
    return native(Qt::Key_Insert);
#endif
}

QString copy()
{
    return native(Qt::CTRL | Qt::Key_C);
}

QString undo()
{
    return native(Qt::CTRL | Qt::Key_Z);
}

QString textInput()
{
    return native(Qt::Key_F4);
}

TextAction textAction(const QKeyEvent *e)
{
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const int key = e->key();
    if (mods == Qt::NoModifier && key == Qt::Key_Delete)
        return TextAction::Delete;
    if (mods == Qt::NoModifier && key == Qt::Key_Insert)
        return TextAction::Mark;
    if (mods == Qt::ControlModifier && (key == Qt::Key_Insert || key == Qt::Key_C))
        return TextAction::Copy;
#ifdef Q_OS_MACOS
    if (mods == Qt::NoModifier && key == Qt::Key_Backspace)
        return TextAction::Delete;
    if (mods == Qt::ControlModifier && key == Qt::Key_I)
        return TextAction::Mark;
#endif
    return TextAction::None;
}

}
