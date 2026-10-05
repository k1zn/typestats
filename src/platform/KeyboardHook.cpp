#include "KeyboardHook.h"

#include "core/KeyName.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <chrono>

namespace {

QPointer<KeyboardHook> g_hook;

qint64 nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

KeyboardHook::KeyboardHook(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<HookEvent>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

#ifdef Q_OS_WIN

#include <qt_windows.h>

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
    e.timeUs = nowUs();
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
            hook->deliver(eventOf(message, *reinterpret_cast<const KBDLLHOOKSTRUCT *>(data)));
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

void KeyboardHook::uiohookEvent(int, quint16, quint16, char16_t, qint64) {}
void KeyboardHook::flushPending() {}

#else // libuiohook

#include <uiohook.h>

#include <unordered_map>

namespace {

// libuiohook VC_* codes are set-1 scan codes; they are mapped to Windows VK codes so that the
// rest of the program (and .tsf files) work with one key namespace.
quint16 vcToVk(quint16 vc)
{
    static const std::unordered_map<quint16, quint16> map = {
        {VC_ESCAPE, 0x1B}, {VC_F1, 0x70}, {VC_F2, 0x71}, {VC_F3, 0x72}, {VC_F4, 0x73},
        {VC_F5, 0x74}, {VC_F6, 0x75}, {VC_F7, 0x76}, {VC_F8, 0x77}, {VC_F9, 0x78},
        {VC_F10, 0x79}, {VC_F11, 0x7A}, {VC_F12, 0x7B},
        {VC_BACKQUOTE, 0xC0}, {VC_1, '1'}, {VC_2, '2'}, {VC_3, '3'}, {VC_4, '4'}, {VC_5, '5'},
        {VC_6, '6'}, {VC_7, '7'}, {VC_8, '8'}, {VC_9, '9'}, {VC_0, '0'},
        {VC_MINUS, 0xBD}, {VC_EQUALS, 0xBB}, {VC_BACKSPACE, 0x08}, {VC_TAB, 0x09},
        {VC_CAPS_LOCK, 0x14},
        {VC_A, 'A'}, {VC_B, 'B'}, {VC_C, 'C'}, {VC_D, 'D'}, {VC_E, 'E'}, {VC_F, 'F'},
        {VC_G, 'G'}, {VC_H, 'H'}, {VC_I, 'I'}, {VC_J, 'J'}, {VC_K, 'K'}, {VC_L, 'L'},
        {VC_M, 'M'}, {VC_N, 'N'}, {VC_O, 'O'}, {VC_P, 'P'}, {VC_Q, 'Q'}, {VC_R, 'R'},
        {VC_S, 'S'}, {VC_T, 'T'}, {VC_U, 'U'}, {VC_V, 'V'}, {VC_W, 'W'}, {VC_X, 'X'},
        {VC_Y, 'Y'}, {VC_Z, 'Z'},
        {VC_OPEN_BRACKET, 0xDB}, {VC_CLOSE_BRACKET, 0xDD}, {VC_BACK_SLASH, 0xDC},
        {VC_SEMICOLON, 0xBA}, {VC_QUOTE, 0xDE}, {VC_ENTER, 0x0D},
        {VC_COMMA, 0xBC}, {VC_PERIOD, 0xBE}, {VC_SLASH, 0xBF}, {VC_SPACE, 0x20},
        {VC_PRINTSCREEN, 0x2C}, {VC_SCROLL_LOCK, 0x91}, {VC_PAUSE, 0x13}, {VC_LESSER_GREATER, 0xE2},
        {VC_INSERT, 0x2D}, {VC_DELETE, 0x2E}, {VC_HOME, 0x24}, {VC_END, 0x23},
        {VC_PAGE_UP, 0x21}, {VC_PAGE_DOWN, 0x22},
        {VC_UP, 0x26}, {VC_LEFT, 0x25}, {VC_RIGHT, 0x27}, {VC_DOWN, 0x28},
        {VC_NUM_LOCK, 0x90}, {VC_KP_DIVIDE, 0x6F}, {VC_KP_MULTIPLY, 0x6A}, {VC_KP_SUBTRACT, 0x6D},
        {VC_KP_ADD, 0x6B}, {VC_KP_ENTER, 0x0D}, {VC_KP_SEPARATOR, 0x6E},
        {VC_KP_1, 0x61}, {VC_KP_2, 0x62}, {VC_KP_3, 0x63}, {VC_KP_4, 0x64}, {VC_KP_5, 0x65},
        {VC_KP_6, 0x66}, {VC_KP_7, 0x67}, {VC_KP_8, 0x68}, {VC_KP_9, 0x69}, {VC_KP_0, 0x60},
        {VC_SHIFT_L, 0xA0}, {VC_SHIFT_R, 0xA1}, {VC_CONTROL_L, 0xA2}, {VC_CONTROL_R, 0xA3},
        {VC_ALT_L, 0xA4}, {VC_ALT_R, 0xA5}, {VC_META_L, 0x5B}, {VC_META_R, 0x5C},
        {VC_CONTEXT_MENU, 0x5D},
    };
    auto it = map.find(vc);
    return it == map.end() ? 0 : it->second;
}

void dispatch(uiohook_event *const e)
{
    int kind = 0;
    char16_t ch = 0;
    switch (e->type) {
    case EVENT_KEY_PRESSED: kind = 0; break;
    case EVENT_KEY_RELEASED: kind = 1; break;
    case EVENT_KEY_TYPED: kind = 2; ch = e->data.keyboard.keychar; break;
    default: return;
    }
    const qint64 time = nowUs();
    const quint16 vk = vcToVk(e->data.keyboard.keycode), scan = e->data.keyboard.keycode;
    if (KeyboardHook *h = g_hook.data())
        QMetaObject::invokeMethod(h, [h, kind, vk, scan, ch, time] { h->uiohookEvent(kind, vk, scan, ch, time); },
                                  Qt::QueuedConnection);
}

bool quietLogger(unsigned int, const char *, ...) { return true; }

} // namespace

void KeyboardHook::flushPending()
{
    if (!m_pending)
        return;
    const HookEvent e = *m_pending;
    m_pending.reset();
    emit key(e);
}

void KeyboardHook::uiohookEvent(int kind, quint16 vk, quint16 scan, char16_t ch, qint64 timeUs)
{
    if (kind == 2) { // the character of the press that is waiting
        if (m_pending && ch) {
            m_pending->ch = ch;
            m_pending->chars = 1;
            m_pending->flags = (m_pending->flags & ~quint32(KeyRecord::NoChar)) | KeyRecord::HasChar;
        }
        flushPending();
        return;
    }
    flushPending();
    const bool down = kind == 0;
    auto held = [this](quint8 key) { return bool(m_held[key >> 3] & (1 << (key & 7))); };
    if (down)
        m_held[(vk & 0xFF) >> 3] |= quint8(1 << (vk & 7));
    else
        m_held[(vk & 0xFF) >> 3] &= quint8(~(1 << (vk & 7)));

    HookEvent e;
    e.timeUs = timeUs;
    e.flags = (scan & 0xFF) | quint32(vk & 0xFF) << 16 | KeyRecord::NoChar;
    if (scan & 0xFF00)
        e.flags |= KeyRecord::Extended;
    if (!down)
        e.flags |= KeyRecord::KeyUp;
    if (held(Vk::LShift) || held(Vk::RShift))
        e.flags |= KeyRecord::Shift;
    if (held(Vk::LControl) || held(Vk::RControl))
        e.flags |= KeyRecord::Ctrl;
    if (held(Vk::LMenu) || held(Vk::RMenu))
        e.flags |= KeyRecord::Alt;
    if (held(Vk::LWin) || held(Vk::RWin))
        e.flags |= KeyRecord::Win;
    if (!down) {
        emit key(e);
        return;
    }
    // The "typed" event, if any, is already queued behind this one.
    m_pending = e;
    QTimer::singleShot(0, this, &KeyboardHook::flushPending);
}

bool KeyboardHook::start()
{
    if (m_running)
        return true;
    g_hook = this;
    hook_set_logger_proc(&quietLogger);
    hook_set_dispatch_proc(&dispatch);
    m_thread = std::thread([this] {
        const int status = hook_run();
        if (status != UIOHOOK_SUCCESS) {
            const QString reason = QStringLiteral("libuiohook error %1").arg(status);
            QMetaObject::invokeMethod(this, [this, reason] { emit failed(reason); }, Qt::QueuedConnection);
        }
    });
    m_running = true;
    return true;
}

void KeyboardHook::stop()
{
    if (!m_running)
        return;
    hook_stop();
    m_thread.join();
    m_running = false;
}

quint64 KeyboardHook::foregroundWindow()
{
    return 0;
}

QString KeyboardHook::foregroundTitle()
{
    return {};
}

QString KeyboardHook::layoutKeyName(quint8 scan, bool *dead)
{
    if (dead)
        *dead = false;
    static const struct { quint8 first; const char *chars; } rows[] = {
        {0x02, "1234567890-="}, {0x10, "qwertyuiop[]"}, {0x1E, "asdfghjkl;'"}, {0x2C, "zxcvbnm,./"}};
    for (const auto &row : rows)
        if (scan >= row.first && scan < row.first + qstrlen(row.chars))
            return QString(QLatin1Char(row.chars[scan - row.first]));
    switch (scan) {
    case 0x29: return QStringLiteral("`");
    case 0x2B: return QStringLiteral("\\");
    case 0x0E: return QStringLiteral("[BackSpace]");
    case 0x0F: return QStringLiteral("[Tab]");
    case 0x3A: return QStringLiteral("[CapsLock]");
    case 0x2A: return QStringLiteral("[LShift]");
    case 0x36: return QStringLiteral("[RShift]");
    }
    return {};
}

int KeyboardHook::toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2])
{
    static const struct { quint8 first; const char *plain, *shifted; } rows[] = {
        {0x02, "1234567890-=", "!@#$%^&*()_+"}, {0x10, "qwertyuiop[]", "QWERTYUIOP{}"},
        {0x1E, "asdfghjkl;'`", "ASDFGHJKL:\"~"}, {0x2B, "\\zxcvbnm,./", "|ZXCVBNM<>?"}, {0x39, " ", " "}};
    for (const auto &row : rows)
        if (scan >= row.first && scan < row.first + qstrlen(row.plain)) {
            const char c = row.plain[scan - row.first];
            const bool letter = c >= 'a' && c <= 'z';
            out[0] = char16_t((shift != (caps && letter) ? row.shifted : row.plain)[scan - row.first]);
            return 1;
        }
    return 0;
}

void KeyboardHook::clearDeadKey() {}

bool KeyboardHook::capsLock()
{
    return false;
}

#endif
