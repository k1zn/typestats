// macOS: a listen-only event tap of its own (re/crossplatform.md, step 6). Only C APIs: Core Graphics
// for the tap and the windows, Carbon's Text Input Sources and UCKeyTranslate for the characters.
// Written without a Mac at hand: checked by CI builds and by hand (re/crossplatform.md).

#include "platform/KeyboardHook.h"

#include "core/KeyName.h"
#include "core/Keyboard.h"
#include "platform/HookClock.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

// No check()/verify()/require() macros from AssertMacros.h.
#ifndef __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#endif
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

#include <pthread/qos.h>

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

namespace {

// --- the keyboard layout: Text Input Sources live in the main thread; the tap thread gets a copy ---

class Layout
{
public:
    static Layout &instance()
    {
        static Layout layout;
        return layout;
    }

    // In the main thread: the layout of the current input source, and from now on its changes.
    void follow()
    {
        if (m_following)
            return;
        m_following = true;
        update();
        CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(), this, &Layout::changed,
                                        kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
                                        CFNotificationSuspensionBehaviorDeliverImmediately);
    }

    ~Layout()
    {
        if (m_following)
            CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetDistributedCenter(), this);
        if (m_data)
            CFRelease(m_data);
    }

    // The 'uchr' data of the layout (retained; the caller releases it), or null.
    CFDataRef data()
    {
        const std::lock_guard lock(m_mutex);
        if (m_data)
            CFRetain(m_data);
        return m_data;
    }

    // ISO keyboards swap the codes of the keys left of "1" and of "Z".
    bool iso() const { return m_iso; }
    // The keyboard type for UCKeyTranslate (LMGetKbdType is read in the main thread only).
    UInt32 keyboardType() const { return m_kbdType; }

private:
    static void changed(CFNotificationCenterRef, void *observer, CFNotificationName, const void *, CFDictionaryRef)
    {
        static_cast<Layout *>(observer)->update();
    }

    void update()
    {
        CFDataRef data = nullptr;
        if (TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource()) {
            if (auto d = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData))) {
                data = d;
                CFRetain(data);
            }
            CFRelease(source);
        }
        const UInt8 type = LMGetKbdType();
        m_kbdType = type;
        m_iso = KBGetLayoutType(type) == kKeyboardISO;
        const std::lock_guard lock(m_mutex);
        if (m_data)
            CFRelease(m_data);
        m_data = data;
    }

    std::mutex m_mutex;
    CFDataRef m_data = nullptr;
    std::atomic<bool> m_iso{false};
    std::atomic<UInt32> m_kbdType{0};
    bool m_following = false;
};

// UCKeyTranslate: the characters of a key with Carbon modifiers (shiftKey...), a dead key pending in `dead`.
int translate(CFDataRef data, quint16 keycode, UInt32 modifiers, UInt32 &dead, bool noDeadKeys, UniChar out[4])
{
    if (!data)
        return 0;
    const auto *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
    UniCharCount length = 0;
    const OptionBits options = noDeadKeys ? kUCKeyTranslateNoDeadKeysMask : 0;
    if (UCKeyTranslate(layout, keycode, kUCKeyActionDown, (modifiers >> 8) & 0xFF, Layout::instance().keyboardType(), options, &dead, 4,
                       &length, out) != noErr)
        return 0;
    return int(length);
}

// What ToUnicodeEx gives for a character of UCKeyTranslate: the controls of the arrows, Home, F-keys... none.
char16_t usable(UniChar c)
{
    if (c == 0x03) // keypad Enter
        return 0x0D;
    if (c == 0x7F || (c < 0x20 && c != 0x08 && c != 0x09 && c != 0x0D && c != 0x1B))
        return 0;
    return char16_t(c);
}

// The character of Ctrl and a key, as ToUnicodeEx (the Ctrl column of the US layout), by VK.
char16_t ctrlChar(quint8 vk, bool shift)
{
    if (vk >= 'A' && vk <= 'Z')
        return char16_t(vk - 0x40);
    switch (vk) {
    case 0xDB: return 0x1B;
    case 0xDC: case 0xE2: return 0x1C;
    case 0xDD: return 0x1D;
    case '6': return shift ? 0x1E : 0;
    case 0xBD: return shift ? 0x1F : 0;
    case 0x08: return 0x7F;
    case 0x0D: return 0x0A;
    case 0x1B: return 0x1B;
    case 0x20: return 0x20;
    }
    return 0;
}

// The swap is its own inverse: the same for the hook's codes and for the table's.
int isoSwapped(int keycode)
{
    if (!Layout::instance().iso())
        return keycode;
    if (keycode == int(kVK_ISO_Section))
        return int(kVK_ANSI_Grave);
    if (keycode == int(kVK_ANSI_Grave))
        return int(kVK_ISO_Section);
    return keycode;
}

std::optional<Keyboard::ScanCode> scanOf(quint16 keycode)
{
    return Keyboard::macToScan(isoSwapped(keycode));
}

int keycodeOf(Keyboard::ScanCode scan)
{
    return isoSwapped(Keyboard::scanToMac(scan));
}

// --- the tap ---

// Device-dependent modifier bits of CGEventFlags (IOKit's NX_DEVICE*KEYMASK).
struct ModifierKey
{
    quint16 keycode;
    CGEventFlags bit;
};
constexpr ModifierKey kModifiers[] = {
    {kVK_Shift, 0x02}, {kVK_RightShift, 0x04}, {kVK_Control, 0x01}, {kVK_RightControl, 0x2000},
    {kVK_Option, 0x20}, {kVK_RightOption, 0x40}, {kVK_Command, 0x08}, {kVK_RightCommand, 0x10},
};

class Tap
{
public:
    explicit Tap(KeyboardHook *hook) : m_hook(hook) {}
    ~Tap() { stop(); }

    // Creates the tap in a thread of its own; false without the permission (Input Monitoring).
    bool start()
    {
        std::promise<bool> created;
        std::future<bool> result = created.get_future();
        m_thread = std::thread([this, created = std::move(created)]() mutable { run(created); });
        if (!result.get()) {
            m_thread.join();
            return false;
        }
        return true;
    }

    void stop()
    {
        if (!m_thread.joinable())
            return;
        // A stop before the loop runs is not lost: the loop looks at the flag between short runs.
        m_stopping = true;
        CFRunLoopStop(m_loop);
        m_thread.join();
    }

    bool capsLock() const { return m_caps; }

private:
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void *self)
    {
        static_cast<Tap *>(self)->handle(type, event);
        return event; // listen only: the event goes on unchanged
    }

    void run(std::promise<bool> &created)
    {
        // Key times are taken here: the thread should not wait behind others.
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) | CGEventMaskBit(kCGEventFlagsChanged);
        m_port = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask, &Tap::callback, this);
        if (!m_port) {
            created.set_value(false);
            return;
        }
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_port, 0);
        m_loop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(m_loop, source, kCFRunLoopCommonModes);
        CGEventTapEnable(m_port, true);
        created.set_value(true);
        while (!m_stopping)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
        CGEventTapEnable(m_port, false);
        CFRunLoopRemoveSource(m_loop, source, kCFRunLoopCommonModes);
        CFRelease(source);
        CFMachPortInvalidate(m_port);
        CFRelease(m_port);
        m_port = nullptr;
    }

    bool held(Keyboard::ScanCode s) const { return m_held[s.code + (s.extended ? 256 : 0)]; }
    void setHeld(Keyboard::ScanCode s, bool down) { m_held[s.code + (s.extended ? 256 : 0)] = down; }

    void handle(CGEventType type, CGEventRef event)
    {
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            CGEventTapEnable(m_port, true); // the system switched the tap off: on again
            return;
        }
        const qint64 timeUs = hookNowUs();
        const auto keycode = quint16(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        const CGEventFlags flags = CGEventGetFlags(event);
        m_caps = flags & kCGEventFlagMaskAlphaShift;
        // Events another program posts come from a source state of its own, not the HID system's.
        const bool injected = CGEventGetIntegerValueField(event, kCGEventSourceStateID) != kCGEventSourceStateHIDSystemState;
        const std::optional<Keyboard::ScanCode> scan = scanOf(keycode);
        if (!scan)
            return; // Fn and keys the Windows hook has no code for
        if (type == kCGEventFlagsChanged) {
            if (keycode == kVK_CapsLock) { // one event a switch: a press and a release
                emitKey(*scan, true, injected, timeUs, false);
                emitKey(*scan, false, injected, timeUs, false);
                return;
            }
            for (const ModifierKey &m : kModifiers)
                if (m.keycode == keycode) {
                    const bool down = flags & m.bit;
                    if (down != held(*scan))
                        emitKey(*scan, down, injected, timeUs, false);
                    return;
                }
            return;
        }
        const bool down = type == kCGEventKeyDown;
        emitKey(*scan, down, injected, timeUs, down, keycode, flags);
    }

    void emitKey(Keyboard::ScanCode scan, bool down, bool injected, qint64 timeUs, bool withChar, quint16 keycode = 0,
                 CGEventFlags eventFlags = 0)
    {
        HookEvent e;
        e.timeUs = timeUs;
        quint32 flags = scan.code;
        if (scan.extended)
            flags |= KeyRecord::Extended;
        if (injected)
            flags |= KeyRecord::Injected;
        if (!down)
            flags |= KeyRecord::KeyUp;
        // The modifiers held before this event (a press of Shift has no Shift bit), as the Windows hook.
        const bool shift = held({0x2A, false}) || held({0x36, false}), ctrl = held({0x1D, false}) || held({0x1D, true}),
                   alt = held({0x38, false}) || held({0x38, true}), win = held({0x5B, true}) || held({0x5C, true});
        if (shift)
            flags |= KeyRecord::Shift;
        if (ctrl)
            flags |= KeyRecord::Ctrl;
        if (alt)
            flags |= KeyRecord::Alt;
        if (win)
            flags |= KeyRecord::Win;
        const quint8 vk = Keyboard::scanToVk(scan, true); // a Mac keypad always types digits
        int chars = 0;
        // Clear, in the place of NumLock, gives Esc in UCKeyTranslate; Windows' NumLock types nothing.
        if (withChar && !(scan.code == 0x45 && scan.extended)) {
            if (ctrl) {
                if (const char16_t c = ctrlChar(vk, shift)) {
                    e.ch = e.firstCh = c;
                    chars = 1;
                }
            } else {
                // Option is part of the character on a Mac (å, dead keys): it is kept; Command is not.
                UInt32 modifiers = 0;
                if (eventFlags & kCGEventFlagMaskShift)
                    modifiers |= shiftKey;
                if (eventFlags & kCGEventFlagMaskAlphaShift)
                    modifiers |= alphaLock;
                if (eventFlags & kCGEventFlagMaskAlternate)
                    modifiers |= optionKey;
                CFDataRef data = Layout::instance().data();
                UniChar out[4] = {};
                const int n = translate(data, keycode, modifiers, m_dead, false, out);
                if (n == 0 && m_dead != 0) { // a dead key: its own character waits, as on Windows
                    UInt32 none = 0;
                    UniChar spacing[4] = {};
                    if (translate(data, keycode, modifiers, none, true, spacing) > 0)
                        e.ch = e.firstCh = spacing[0];
                    chars = -1;
                } else if (n == 2 && usable(out[0]) && usable(out[1])) { // a dead key that did not combine
                    e.firstCh = out[0];
                    e.ch = out[1];
                    chars = 2;
                } else if (n >= 1 && usable(out[0])) {
                    e.ch = e.firstCh = usable(out[0]);
                    chars = 1;
                }
                if (data)
                    CFRelease(data);
            }
        }
        if (chars != 0)
            flags |= KeyRecord::HasChar;
        else
            flags |= KeyRecord::NoChar;
        if (chars < 0)
            flags |= KeyRecord::DeadKey;
        e.flags = flags | quint32(vk) << 16;
        e.chars = chars;
        setHeld(scan, down);
        KeyboardHook *hook = m_hook;
        QMetaObject::invokeMethod(hook, [hook, e] { emit hook->key(e); }, Qt::QueuedConnection);
    }

    KeyboardHook *m_hook;
    std::thread m_thread;
    CFMachPortRef m_port = nullptr;
    CFRunLoopRef m_loop = nullptr;
    std::atomic<bool> m_stopping{false};
    bool m_held[512] = {};
    UInt32 m_dead = 0;
    std::atomic<bool> m_caps{false};
};

// The front window: CGWindowList, front to back; the first of layer 0. Its title needs the Screen
// Recording permission (not asked for): the name of the program instead. Asked at most every 200 ms
// (every key asks for the window).
struct FrontWindow
{
    quint64 id = 0;
    QString title;
    qint64 timeUs = -1;
};

FrontWindow frontWindow()
{
    static FrontWindow cached;
    const qint64 now = hookNowUs();
    if (cached.timeUs >= 0 && now - cached.timeUs < 200000)
        return cached;
    cached = {};
    cached.timeUs = now;
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows)
        return cached;
    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
        int layer = -1;
        if (auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer)))
            CFNumberGetValue(n, kCFNumberIntType, &layer);
        if (layer != 0)
            continue;
        qint64 number = 0;
        if (auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber)))
            CFNumberGetValue(n, kCFNumberSInt64Type, &number);
        cached.id = quint64(number);
        auto text = [info](CFStringRef key) {
            auto s = static_cast<CFStringRef>(CFDictionaryGetValue(info, key));
            return s ? QString::fromCFString(s) : QString();
        };
        const QString owner = text(kCGWindowOwnerName), name = text(kCGWindowName);
        cached.title = name.isEmpty() ? owner : owner + QStringLiteral(" - ") + name;
        break;
    }
    CFRelease(windows);
    return cached;
}

} // namespace

struct KeyboardHook::Impl
{
    std::unique_ptr<Tap> tap;
    QTimer *retry = nullptr; // waits for the permission
};

KeyboardHook::KeyboardHook(QObject *parent) : QObject(parent), m_impl(std::make_unique<Impl>())
{
    qRegisterMetaType<HookEvent>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

bool KeyboardHook::start()
{
    if (m_running)
        return true;
    Layout::instance().follow();
    auto tryStart = [this] {
        if (!CGPreflightListenEventAccess())
            return false;
        auto tap = std::make_unique<Tap>(this);
        if (!tap->start())
            return false;
        m_impl->tap = std::move(tap);
        return true;
    };
    if (tryStart()) {
        m_running = true;
        emit started();
        return true;
    }
    // Asks once (the system's dialog), then looks every 2 s whether the permission came.
    CGRequestListenEventAccess();
    if (!m_impl->retry) {
        m_impl->retry = new QTimer(this);
        m_impl->retry->setInterval(2000);
        connect(m_impl->retry, &QTimer::timeout, this, [this, tryStart] {
            if (tryStart()) {
                m_impl->retry->stop();
                m_running = true;
                emit started();
            }
        });
    }
    m_impl->retry->start();
    emit failed(tr("Нет разрешения на чтение клавиатуры.\n\nОткройте «Системные настройки» → «Конфиденциальность и "
                   "безопасность» → «Мониторинг ввода» и включите Typing statistics. Запись начнётся сама; если нет — "
                   "перезапустите программу."));
    return false;
}

void KeyboardHook::stop()
{
    if (m_impl->retry)
        m_impl->retry->stop();
    m_impl->tap.reset();
    m_running = false;
}

quint64 KeyboardHook::foregroundWindow()
{
    return frontWindow().id;
}

QString KeyboardHook::windowTitle(quint64)
{
    return foregroundTitle();
}

QString KeyboardHook::foregroundTitle()
{
    return frontWindow().title;
}

QString KeyboardHook::layoutKeyName(quint8 scan, bool *dead)
{
    if (dead)
        *dead = false;
    Layout::instance().follow();
    const int keycode = keycodeOf({scan, false});
    CFDataRef data = Layout::instance().data();
    if (!data || keycode < 0) {
        if (data)
            CFRelease(data);
        return UsLayout::keyName(scan);
    }
    UInt32 deadState = 0;
    UniChar out[4] = {};
    int n = translate(data, quint16(keycode), 0, deadState, false, out);
    if (n == 0 && deadState != 0) {
        if (dead)
            *dead = true;
        UInt32 none = 0;
        n = translate(data, quint16(keycode), 0, none, true, out);
    }
    CFRelease(data);
    const char16_t ch = n > 0 ? usable(out[0]) : 0;
    quint32 flags = scan | quint32(Keyboard::scanToVk({scan, false}, true)) << 16;
    if (!ch)
        flags |= KeyRecord::NoChar;
    return keyDisplayName(flags, ch);
}

namespace {
UInt32 g_convertDead = 0; // the dead key pending between toUnicode() calls
}

int KeyboardHook::toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2])
{
    out[0] = out[1] = 0;
    Layout::instance().follow();
    const int keycode = keycodeOf({scan, false});
    CFDataRef data = Layout::instance().data();
    if (!data || keycode < 0) {
        if (data)
            CFRelease(data);
        return UsLayout::toUnicode(scan, shift, caps, out);
    }
    UInt32 modifiers = (shift ? shiftKey : 0) | (caps ? alphaLock : 0);
    UniChar chars[4] = {};
    int n = translate(data, quint16(keycode), modifiers, g_convertDead, false, chars);
    int result = 0;
    if (n == 0 && g_convertDead != 0) {
        UInt32 none = 0;
        if (translate(data, quint16(keycode), modifiers, none, true, chars) > 0)
            out[0] = chars[0];
        result = -1;
    } else {
        result = std::min(n, 2);
        out[0] = usable(chars[0]);
        out[1] = n > 1 ? usable(chars[1]) : 0;
        if (!out[0])
            result = 0;
    }
    CFRelease(data);
    return result;
}

void KeyboardHook::clearDeadKey()
{
    g_convertDead = 0;
}

bool KeyboardHook::capsLock()
{
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState) & kCGEventFlagMaskAlphaShift;
}
