#pragma once

#include "core/Recorder.h"

#include <QObject>

#include <memory>

Q_DECLARE_METATYPE(HookEvent)

// The system-wide keyboard hook. Events come with the record flags the original's hook builds
// (re/recording.md) and are delivered in the GUI thread.
//
// Windows (KeyboardHookWin.cpp): a low-level keyboard hook in a thread of its own (the time of a key
// does not depend on how busy the GUI is, and a long GUI task does not make Windows drop the hook), the
// characters from ToUnicodeEx with the layout of the focused window.
// Linux (linux/KeyboardHookLinux.cpp): the keyboards of /dev/input read in a thread, the characters
// from xkbcommon with the layout the desktop has on (re/crossplatform.md, step 5).
// macOS (mac/KeyboardHookMac.cpp): a listen-only event tap in a thread, the characters from
// UCKeyTranslate with the current input source (step 6).
class KeyboardHook : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardHook(QObject *parent = nullptr);
    ~KeyboardHook() override;

    // Starts the capture; false (and failed()) when it cannot start now. The hook may still start
    // by itself later (the access is granted): then started() comes.
    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    // The active window of the system: a value that changes with it, and the title of its top-level window.
    static quint64 foregroundWindow();
    static QString foregroundTitle();
    // The title of the top-level window of `window` (a value of foregroundWindow()); outside Windows the
    // windows have no such ids: the title of the active window.
    static QString windowTitle(quint64 window);
    // Name of the key with this scan code in the current keyboard layout, as the text shows it
    // (the original's 0x448fe8); `dead` tells a dead key. Without layout access: the US layout.
    static QString layoutKeyName(quint8 scan, bool *dead = nullptr);
    // Editing::ToUnicode for the current keyboard layout with only Shift and CapsLock as given
    // (ConvCurLayout 0x429e80); a dead key stays pending for the next call, as in typing, until
    // clearDeadKey(). Without layout access: the US layout.
    static int toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2]);
    static void clearDeadKey();
    static bool capsLock();

    struct Impl;

signals:
    void key(const HookEvent &e);
    // The capture is not running: the reason and what to do, for the user.
    void failed(const QString &reason);
    void started();

private:
    friend struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_running = false;
};

// The US layout, for systems without access to the layout (and before the hook starts).
namespace UsLayout {
QString keyName(quint8 scan);
int toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2]);
}
