#pragma once

#include "core/Keyboard.h"
#include "core/Recorder.h"
#include "platform/DesktopParsers.h"

#include <bitset>
#include <optional>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;

// Turns Linux key events (evdev codes) into the events of the Windows hook: the same scan and VK codes,
// modifier bits, characters (re/crossplatform.md, step 5). The characters come from an XKB keymap:
// the layout the desktop has on (setGroup), dead keys and Compose through xkb_compose. One object per
// thread: xkb states are not shared.
class XkbKeyboard
{
public:
    XkbKeyboard();
    ~XkbKeyboard();
    XkbKeyboard(const XkbKeyboard &) = delete;
    XkbKeyboard &operator=(const XkbKeyboard &) = delete;

    // A keymap from XKB names (rules "evdev"); nullptr when it does not compile. The caller owns it.
    static xkb_keymap *compile(const Desktop::XkbNames &names);
    static xkb_context *sharedContext();

    // Takes a reference to the keymap; the state starts anew, with Caps Lock and NumLock as they were.
    void setKeymap(xkb_keymap *keymap);
    bool setKeymap(const Desktop::XkbNames &names);
    xkb_keymap *keymap() const { return m_keymap; }
    // The layout the desktop says is on; -1: the keymap switches by itself (its grp: options).
    void setGroup(int group) { m_group = group; }
    int group() const { return m_group; }
    // Caps Lock and NumLock as the keyboard's LEDs show them.
    void setLocks(bool caps, bool num);
    bool capsLock() const;
    bool numLock() const;

    // One evdev key event (value: 0 release, 1 press, 2 autorepeat). Empty for a key the Windows hook
    // has no code for (the state is still updated).
    std::optional<HookEvent> event(int code, int value, qint64 timeUs, bool injected);

    // For KeyboardHook's static helpers: the character of a key with only Shift and Caps Lock, a dead
    // key pending for the next call (ToUnicodeEx), and the name of a key for the keyboard picture.
    int toUnicode(Keyboard::ScanCode scan, bool shift, bool caps, char16_t out[2]);
    void clearDeadKey();
    QString keyName(quint8 scan, bool *dead);

    // The layout (group) whose name in the keymap is this description ("Russian"); -1 when none.
    int groupOfName(const QString &description) const;

private:
    enum class Compose { None, Pending, Composed, Cancelled };
    struct Char
    {
        int chars = 0;      // ToUnicodeEx: 0, 1, 2, -1 for a dead key
        char16_t ch = 0;
        char16_t first = 0; // chars == 2
    };

    Char characterOf(int keycode, quint8 vk, bool ctrl, bool alt, bool shift);
    Char typed(quint32 keysym);
    char16_t spacingOf(quint32 deadKeysym) const;
    void applyGroup();
    bool held(int code) const { return code >= 0 && code < int(m_held.size()) && m_held[code]; }

    xkb_keymap *m_keymap = nullptr;
    xkb_state *m_state = nullptr;
    xkb_compose_table *m_composeTable = nullptr;
    xkb_compose_state *m_compose = nullptr;
    quint32 m_deadKeysym = 0; // the dead key that is pending in m_compose
    int m_group = -1;
    std::bitset<768> m_held;  // evdev codes down
};
