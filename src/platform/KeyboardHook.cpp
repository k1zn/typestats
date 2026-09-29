#include "KeyboardHook.h"

#include <uiohook.h>

#include <QMetaObject>
#include <QPointer>
#include <chrono>
#include <cstdarg>
#include <unordered_map>

namespace {

QPointer<KeyboardHook> g_hook;

qint64 nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

#ifndef _WIN32
// libuiohook VC_* codes are set-1 scan codes; map them to Windows VK codes so that
// the rest of the program (and .tsf files) work with one key namespace.
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
#endif

void dispatch(uiohook_event *const e)
{
    HookKey k;
    switch (e->type) {
    case EVENT_KEY_PRESSED: k.kind = HookKey::Press; break;
    case EVENT_KEY_RELEASED: k.kind = HookKey::Release; break;
    case EVENT_KEY_TYPED: k.kind = HookKey::Typed; k.ch = e->data.keyboard.keychar; break;
    default: return;
    }
    k.timeUs = nowUs();
#ifdef _WIN32
    k.vk = e->data.keyboard.rawcode;
#else
    k.vk = vcToVk(e->data.keyboard.keycode);
#endif
    k.scan = e->data.keyboard.keycode;
    if (KeyboardHook *h = g_hook.data())
        QMetaObject::invokeMethod(h, [h, k] { emit h->key(k); }, Qt::QueuedConnection);
}

bool quietLogger(unsigned int, const char *, ...) { return true; }

} // namespace

KeyboardHook::KeyboardHook(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<HookKey>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

bool KeyboardHook::start()
{
    if (isRunning())
        return true;
    g_hook = this;
    hook_set_logger_proc(&quietLogger);
    hook_set_dispatch_proc(&dispatch);
    m_thread = std::thread([this] {
        int status = hook_run();
        if (status != UIOHOOK_SUCCESS) {
            QString reason = QStringLiteral("libuiohook error %1").arg(status);
            QMetaObject::invokeMethod(this, [this, reason] { emit failed(reason); }, Qt::QueuedConnection);
        }
    });
    return true;
}

void KeyboardHook::stop()
{
    if (!isRunning())
        return;
    hook_stop();
    m_thread.join();
}
