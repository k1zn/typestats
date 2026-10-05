#pragma once

#include "XkbKeyboard.h"

#include <QString>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

// Reads the keyboards of /dev/input/event* in a thread of its own and turns their events into hook
// events (XkbKeyboard). Keyboards are told by their capabilities in sysfs (letter keys), without
// opening them; new ones and new access rights (udev's uaccess ACL) are seen through inotify on
// /dev/input. Nothing is sent to the devices and nothing is grabbed.
class EvdevReader
{
public:
    // `onEvent` and `onOpened` (the first keyboard is open) are called in the reader's thread.
    using EventSink = std::function<void(const HookEvent &)>;
    EvdevReader(EventSink onEvent, std::function<void()> onOpened, const QString &sysRoot = QStringLiteral("/sys/class/input"),
                const QString &devRoot = QStringLiteral("/dev/input"));
    ~EvdevReader();

    struct Status
    {
        int keyboards = 0; // keyboards found
        int open = 0;      // of them readable
        int denied = 0;    // not readable: no access
    };
    // Opens the keyboards there are and starts the thread, which also waits for keyboards to come or
    // to become readable. False when the thread cannot start (no inotify, eventfd).
    bool start(Status *status = nullptr);
    void stop();

    // From another thread: applied before the next event.
    void setKeymap(xkb_keymap *keymap);
    void setGroup(int group);
    bool capsLock() const { return m_caps; }

    // A keyboard by its sysfs capabilities: EV_KEY with the letter keys, Space and Enter.
    static bool isKeyboard(const QString &evBits, const QString &keyBits);
    // Bits of a sysfs bitmap ("120013 ffff..." - words of the size of long, the most significant first).
    static bool testBit(const QString &bitmap, int bit);

private:
    struct Device
    {
        int fd = -1;
        bool injected = false; // a virtual device (uinput: ydotool, keyd...)
        bool dropping = false; // SYN_DROPPED: events up to the next SYN_REPORT are lost
    };

    enum class Open { Ok, NotKeyboard, Denied, Failed };
    Open open(const QString &name);
    void close(const QString &name);
    void run();
    void readDevice(const QString &name, Device &d);
    void readInotify();
    void applyPending();

    EventSink m_onEvent;
    std::function<void()> m_onOpened;
    QString m_sysRoot, m_devRoot;
    std::map<QString, Device> m_devices; // by name: "event3"
    int m_inotify = -1, m_wake = -1;
    std::thread m_thread;
    std::atomic<bool> m_stop{false}, m_caps{false};
    bool m_openedOnce = false;

    XkbKeyboard m_keyboard; // used in the thread only

    std::mutex m_mutex; // guards the pending keymap and group
    xkb_keymap *m_pendingKeymap = nullptr;
    int m_pendingGroup = -1;
    bool m_groupPending = false;
};
