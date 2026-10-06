#include "KeyboardHook.h"

#include "HookClock.h"
#include "core/KeyName.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>

#include <qt_windows.h>

// The hook lives in a thread of its own with a message loop (Windows calls a low-level hook in the thread
// that set it, from its message loop). The callback takes the time and the flags at once and queues the
// event to the GUI thread: the time of a key no longer waits for the GUI, and a GUI busy for longer than
// LowLevelHooksTimeout (an export, a large file) no longer makes Windows drop the hook silently.
struct KeyboardHook::Impl
{
    std::thread thread;
    DWORD threadId = 0;
    // Events queued by an earlier start() are dropped: stop() does not wait for the queue of the GUI.
    quint64 session = 0;

    static void deliver(KeyboardHook *hook, quint64 session, const HookEvent &e)
    {
        if (hook->m_running && hook->m_impl->session == session)
            emit hook->key(e);
    }
};

namespace {

std::atomic<KeyboardHook *> g_target{nullptr}; // set before the thread starts, cleared after it ends
quint64 g_session = 0;                          // the same
// ToUnicodeEx and ToAsciiEx change the dead key state of the system: the hook thread and the static
// functions below (the GUI thread) take turns, as when they ran in one thread.
std::mutex g_layoutMutex;

} // namespace

KeyboardHook::KeyboardHook(QObject *parent) : QObject(parent), m_impl(std::make_unique<Impl>())
{
    qRegisterMetaType<HookEvent>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

namespace {

HHOOK g_handle = nullptr; // the hook thread only

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
    // The window typed into, now: the GUI sees the event later, maybe after a switch.
    HWND window = GetForegroundWindow();
    e.window = quint64(quintptr(window));
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    e.ownWindow = window != nullptr && process == GetCurrentProcessId();
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
        const std::lock_guard lock(g_layoutMutex);
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
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
        const HookEvent e = eventOf(message, *reinterpret_cast<const KBDLLHOOKSTRUCT *>(data));
        if (KeyboardHook *hook = g_target.load()) {
            const quint64 session = g_session;
            QMetaObject::invokeMethod(hook, [hook, session, e] { KeyboardHook::Impl::deliver(hook, session, e); },
                                      Qt::QueuedConnection);
        }
    }
    return CallNextHookEx(g_handle, code, message, data);
}

struct Started
{
    DWORD error = 0;
    DWORD threadId = 0;
};

void hookThread(std::promise<Started> *started)
{
    // The time of a key is taken in the callback: the thread should not wait behind others. It runs for
    // microseconds a key.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // the message queue: stop() posts WM_QUIT to it
    g_handle = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    const DWORD error = g_handle ? 0 : std::max<DWORD>(GetLastError(), ERROR_GEN_FAILURE); // never 0 on a failure
    started->set_value({error, GetCurrentThreadId()}); // `started` is gone after this
    if (!g_handle)
        return;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    UnhookWindowsHookEx(g_handle);
    g_handle = nullptr;
}

} // namespace

bool KeyboardHook::start()
{
    if (m_running)
        return true;
    g_target = this;
    g_session = ++m_impl->session;
    std::promise<Started> promise;
    std::future<Started> future = promise.get_future();
    m_impl->thread = std::thread(hookThread, &promise);
    const Started result = future.get();
    if (result.error != 0) {
        m_impl->thread.join();
        g_target = nullptr;
        emit failed(tr("Не удалось начать запись нажатий (ошибка %1). Перезапустите программу.").arg(result.error));
        return false;
    }
    m_impl->threadId = result.threadId;
    m_running = true;
    emit started();
    return true;
}

void KeyboardHook::stop()
{
    if (!m_running)
        return;
    PostThreadMessageW(m_impl->threadId, WM_QUIT, 0, 0);
    m_impl->thread.join();
    g_target = nullptr;
    m_running = false; // the events still queued to the GUI are dropped (Impl::deliver)
}

quint64 KeyboardHook::foregroundWindow()
{
    return quint64(quintptr(GetForegroundWindow()));
}

QString KeyboardHook::foregroundTitle()
{
    return windowTitle(foregroundWindow());
}

QString KeyboardHook::windowTitle(quint64 id)
{
    HWND window = HWND(quintptr(id));
    while (HWND parent = GetParent(window))
        window = parent;
    WCHAR title[80] = {};
    const int length = GetWindowTextW(window, title, 80);
    return QString::fromWCharArray(title, length);
}

QString KeyboardHook::layoutKeyName(quint8 scan, bool *dead)
{
    const std::lock_guard lock(g_layoutMutex);
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
    const std::lock_guard lock(g_layoutMutex);
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
    const std::lock_guard lock(g_layoutMutex);
    const HKL layout = GetKeyboardLayout(0);
    const BYTE state[256] = {};
    WCHAR chars[2];
    ToUnicodeEx(VK_SPACE, MapVirtualKeyExW(VK_SPACE, MAPVK_VK_TO_VSC, layout), state, chars, 2, 0, layout);
}

bool KeyboardHook::capsLock()
{
    return GetKeyState(VK_CAPITAL) & 1;
}
