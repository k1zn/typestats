#pragma once

#include <QObject>
#include <thread>

// Raw keyboard event captured system-wide.
struct HookKey
{
    quint16 vk = 0;         // Windows virtual-key code (translated on non-Windows platforms)
    quint16 scan = 0;       // hardware scan code (set 1), 0 if unknown
    enum Kind : quint8 { Press, Release, Typed } kind = Press;
    qint64 timeUs = 0;      // monotonic timestamp, microseconds
    char16_t ch = 0;        // Typed only: character produced by the preceding Press
};
Q_DECLARE_METATYPE(HookKey)

// Global keyboard hook built on libuiohook. Runs the hook loop in a worker thread
// and delivers events to the GUI thread through a queued signal.
class KeyboardHook : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardHook(QObject *parent = nullptr);
    ~KeyboardHook() override;

    bool start();
    void stop();
    bool isRunning() const { return m_thread.joinable(); }

signals:
    void key(const HookKey &k);
    void failed(const QString &reason);

private:
    std::thread m_thread;
};
