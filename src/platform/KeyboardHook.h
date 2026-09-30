#pragma once

#include "core/Recorder.h"

#include <QObject>

#include <optional>
#include <thread>

Q_DECLARE_METATYPE(HookEvent)

// The system-wide keyboard hook. Events come with the record flags the original's hook builds
// (re/recording.md) and are delivered in the GUI thread.
//
// Windows: a low-level keyboard hook of its own in the GUI thread, the characters from ToUnicodeEx
// with the layout of the focused window. Elsewhere: libuiohook in a worker thread; modifiers are
// tracked by the keys seen, the character comes with the "typed" event.
class KeyboardHook : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardHook(QObject *parent = nullptr);
    ~KeyboardHook() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    // The active window of the system: a value that changes with it, and the title of its top-level window.
    static quint64 foregroundWindow();
    static QString foregroundTitle();

    // Used by the platform callbacks.
    void deliver(const HookEvent &e) { emit key(e); }
    void uiohookEvent(int kind, quint16 vk, quint16 scan, char16_t ch, qint64 timeUs);

signals:
    void key(const HookEvent &e);
    void failed(const QString &reason);

private:
    void flushPending();

    bool m_running = false;
    std::thread m_thread;                // libuiohook loop
    std::optional<HookEvent> m_pending;  // a press waiting for its character
    quint8 m_held[32] = {};              // pressed keys by VK
};
