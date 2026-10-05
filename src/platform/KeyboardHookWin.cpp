#include "KeyboardHook.h"

#include "HookClock.h"
#include "core/KeyName.h"

#include <QPointer>

#include <qt_windows.h>

struct KeyboardHook::Impl
{
};

namespace {

QPointer<KeyboardHook> g_hook;

} // namespace

KeyboardHook::KeyboardHook(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<HookEvent>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

namespace {

HHOOK g_handle = nullptr;

// A dead key taken out of the input queue by ToUnicodeEx, to be put back for the hooked application.
struct DeadKey
{
    bool pending = false;
    UINT vk = 0, scan = 0;
    bool shift = false, alt = false, ctrl = false;
} g_dead;

bool asyncDown(int vk)
{
    return GetAsyncKeyState(vk) & 0x8000;
}

// KeyboardHookProc (0x404598).
HookEvent eventOf(WPARAM message, const KBDLLHOOKSTRUCT &k)
{
    HookEvent e;
    e.timeUs = hookNowUs();
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    quint32 flags = 0;
    if (k.flags & LLKHF_EXTENDED)
        flags |= KeyRecord::Extended;
    if (k.flags & LLKHF_INJECTED)
        flags |= KeyRecord::Injected;
    if (!down)
        flags |= KeyRecord::KeyUp;
    if (k.vkCode == VK_PACKET) { // Unicode input: the character travels in the scan code
        e.flags = flags | KeyRecord::Packet;
        e.ch = char16_t(k.scanCode);
        return e;
    }
    flags |= k.scanCode & 0xFF;

    // The layout of the window that has the keyboard focus.
    HWND window = GetForegroundWindow();
    if (const DWORD thread = GetWindowThreadProcessId(window, nullptr)) {
        GUITHREADINFO info = {};
        info.cbSize = sizeof(info);
        GetGUIThreadInfo(thread, &info);
        window = info.hwndFocus;
    }
    const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));

    BYTE state[256];
    GetKeyboardState(state);
    auto modifier = [&](int vk, quint32 flag) {
        const bool held = asyncDown(vk);
        state[vk] = held ? 0x80 : 0;
        if (held)
            flags |= flag;
    };
    modifier(VK_SHIFT, KeyRecord::Shift);
    modifier(VK_CONTROL, KeyRecord::Ctrl);
    modifier(VK_MENU, KeyRecord::Alt);
    state[VK_CAPITAL] = BYTE(GetKeyState(VK_CAPITAL) & 1);
    state[VK_LWIN] = state[VK_RWIN] = 0;

    int chars = 0;
    if (down) {
        WCHAR buffer[2] = {};
        chars = ToUnicodeEx(k.vkCode, k.scanCode, state, buffer, 2, 0, layout);
        if (chars != 0) {
            chars = std::min(chars, 2);
            flags |= KeyRecord::HasChar;
            e.ch = buffer[chars < 1 ? 0 : chars - 1];
            e.firstCh = buffer[0];
        }
        WORD unused[2];
        if (chars > 0 && g_dead.pending) {
            // ToUnicodeEx has eaten the dead key: put it back so that the application gets the composed character.
            BYTE deadState[256] = {};
            deadState[VK_SHIFT] = g_dead.shift ? 0x80 : 0;
            deadState[VK_MENU] = g_dead.alt ? 0x80 : 0;
            deadState[VK_CONTROL] = g_dead.ctrl ? 0x80 : 0;
            ToAsciiEx(g_dead.vk, g_dead.scan, deadState, unused, 0, layout);
            g_dead.pending = false;
        }
        if (chars < 0) {
            ToAsciiEx(k.vkCode, k.scanCode, state, unused, 0, layout);
            g_dead = {true, k.vkCode, k.scanCode, bool(flags & KeyRecord::Shift), bool(flags & KeyRecord::Alt),
                      bool(flags & KeyRecord::Ctrl)};
            flags |= KeyRecord::DeadKey;
        }
    }
    if (chars == 0)
        flags |= KeyRecord::NoChar;
    if (asyncDown(VK_LWIN) || asyncDown(VK_RWIN))
        flags |= KeyRecord::Win;
    e.flags = flags | (k.vkCode & 0xFF) << 16;
    e.chars = chars;
    return e;
}

LRESULT CALLBACK hookProc(int code, WPARAM message, LPARAM data)
{
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP))
        if (KeyboardHook *hook = g_hook.data())
            emit hook->key(eventOf(message, *reinterpret_cast<const KBDLLHOOKSTRUCT *>(data)));
    return CallNextHookEx(g_handle, code, message, data);
}

} // namespace

bool KeyboardHook::start()
{
    if (m_running)
        return true;
    g_hook = this;
    g_handle = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    m_running = g_handle != nullptr;
    if (m_running)
        emit started();
    else
        emit failed(tr("Не удалось перехватить клавиатуру (SetWindowsHookEx: ошибка %1).").arg(GetLastError()));
    return m_running;
}

void KeyboardHook::stop()
{
    if (!m_running)
        return;
    UnhookWindowsHookEx(g_handle);
    g_handle = nullptr;
    m_running = false;
}

quint64 KeyboardHook::foregroundWindow()
{
    return quint64(quintptr(GetForegroundWindow()));
}

QString KeyboardHook::foregroundTitle()
{
    HWND window = GetForegroundWindow();
    while (HWND parent = GetParent(window))
        window = parent;
    WCHAR title[80] = {};
    const int length = GetWindowTextW(window, title, 80);
    return QString::fromWCharArray(title, length);
}

QString KeyboardHook::layoutKeyName(quint8 scan, bool *dead)
{
    const HKL layout = GetKeyboardLayout(0);
    const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
    BYTE state[256] = {};
    GetKeyboardState(state);
    state[VK_CONTROL] = state[VK_MENU] = 0;
    WCHAR chars[2] = {};
    const int n = ToUnicodeEx(vk, scan, state, chars, 2, 0, layout);
    if (dead)
        *dead = n < 0;
    quint32 flags = scan | (vk & 0xFF) << 16;
    if (n == 0)
        flags |= KeyRecord::NoChar;
    else if (n < 0)
        ToUnicodeEx(vk, scan, state, chars, 2, 0, layout); // takes the dead key out of the keyboard state
    return keyDisplayName(flags, n != 0 ? char16_t(chars[0]) : u'\0');
}

int KeyboardHook::toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2])
{
    const HKL layout = GetKeyboardLayout(0);
    BYTE state[256] = {};
    GetKeyboardState(state);
    for (int vk : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
                   VK_LMENU, VK_RMENU})
        state[vk] = 0;
    if (shift)
        state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
    state[VK_CAPITAL] = caps ? 1 : 0;
    const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, layout);
    WCHAR chars[2] = {};
    const int n = ToUnicodeEx(vk & 0xFF, scan, state, chars, 2, 0, layout);
    out[0] = chars[0];
    out[1] = chars[1];
    return n;
}

void KeyboardHook::clearDeadKey()
{
    // A space after a dead key takes it out of the keyboard state; without one it changes nothing.
    const HKL layout = GetKeyboardLayout(0);
    const BYTE state[256] = {};
    WCHAR chars[2];
    ToUnicodeEx(VK_SPACE, MapVirtualKeyExW(VK_SPACE, MAPVK_VK_TO_VSC, layout), state, chars, 2, 0, layout);
}

bool KeyboardHook::capsLock()
{
    return GetKeyState(VK_CAPITAL) & 1;
}
