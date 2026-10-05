// The layout source of an X11 session (LayoutSources_p.h): xkbcommon-x11 and xcb-xkb.

#include "LayoutSources_p.h"

#ifdef TS_HAVE_X11
#include "XkbKeyboard.h"

#include <QSocketNotifier>

#include <xcb/xcb.h>
// xkb.h has a member named "explicit": not C++ without this.
#define explicit explicit_
#include <xcb/xkb.h>
#undef explicit
#include <xkbcommon/xkbcommon-x11.h>

#include <cstdlib>
#include <cstring>

struct X11Source::Connection
{
    xcb_connection_t *c = nullptr;
    xcb_window_t root = 0;
    int32_t device = -1;
    uint8_t xkbEvent = 0;
    xcb_atom_t activeWindow = 0, wmName = 0, utf8 = 0;
};

X11Source::~X11Source()
{
    if (m_x) {
        if (m_x->c)
            xcb_disconnect(m_x->c);
        delete m_x;
    }
}

quint32 X11Source::atom(const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(m_x->c, xcb_intern_atom(m_x->c, 0, uint16_t(strlen(name)), name), nullptr);
    const quint32 a = r ? r->atom : 0;
    free(r);
    return a;
}

bool X11Source::start()
{
    m_x = new Connection;
    int screen = 0;
    m_x->c = xcb_connect(nullptr, &screen);
    if (!m_x->c || xcb_connection_has_error(m_x->c))
        return false;
    uint8_t firstEvent = 0;
    if (!xkb_x11_setup_xkb_extension(m_x->c, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr, &firstEvent, nullptr))
        return false;
    m_x->xkbEvent = firstEvent;
    m_x->device = xkb_x11_get_core_keyboard_device_id(m_x->c);
    if (m_x->device < 0)
        return false;

    // The keymap, its changes and the state (the layout on).
    enum : uint16_t {
        Events = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY | XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
        MapParts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP
                   | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS
                   | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP,
        StateParts = XCB_XKB_STATE_PART_GROUP_BASE | XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK,
    };
    xcb_xkb_select_events_details_t details = {};
    details.affectNewKeyboard = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.newKeyboardDetails = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.affectState = StateParts;
    details.stateDetails = StateParts;
    xcb_xkb_select_events_aux(m_x->c, xcb_xkb_device_spec_t(m_x->device), Events, 0, 0, MapParts, MapParts, &details);

    // The active window: _NET_ACTIVE_WINDOW of the root window.
    const xcb_setup_t *setup = xcb_get_setup(m_x->c);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen && it.rem; ++i)
        xcb_screen_next(&it);
    if (!it.rem)
        return false;
    m_x->root = it.data->root;
    const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(m_x->c, m_x->root, XCB_CW_EVENT_MASK, &mask);
    m_x->activeWindow = atom("_NET_ACTIVE_WINDOW");
    m_x->wmName = atom("_NET_WM_NAME");
    m_x->utf8 = atom("UTF8_STRING");

    readKeymap();
    readActiveWindow();
    xcb_flush(m_x->c);
    m_notifier = new QSocketNotifier(xcb_get_file_descriptor(m_x->c), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11Source::processEvents);
    processEvents();
    return keymap() != nullptr;
}

void X11Source::readKeymap()
{
    if (xkb_keymap *keymap = xkb_x11_keymap_new_from_device(XkbKeyboard::sharedContext(), m_x->c, m_x->device,
                                                             XKB_KEYMAP_COMPILE_NO_FLAGS)) {
        if (xkb_state *state = xkb_x11_state_new_from_device(keymap, m_x->c, m_x->device)) {
            setKeymap(keymap);
            setGroup(int(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE)));
            xkb_state_unref(state);
        }
        xkb_keymap_unref(keymap);
    }
    if (!keymap())
        setNames(systemNames(), false);
}

void X11Source::readActiveWindow()
{
    xcb_get_property_reply_t *r = xcb_get_property_reply(
        m_x->c, xcb_get_property(m_x->c, 0, m_x->root, m_x->activeWindow, XCB_ATOM_WINDOW, 0, 1), nullptr);
    quint64 id = 0;
    if (r && xcb_get_property_value_length(r) >= int(sizeof(xcb_window_t)))
        id = *static_cast<const xcb_window_t *>(xcb_get_property_value(r));
    free(r);
    setWindow({id, {}});
}

QString X11Source::windowTitle()
{
    // Asked only when an auto comment is due: read then.
    const xcb_window_t w = xcb_window_t(window().id);
    if (!w)
        return {};
    auto property = [this, w](xcb_atom_t name, xcb_atom_t type) -> QByteArray {
        xcb_get_property_reply_t *r = xcb_get_property_reply(m_x->c, xcb_get_property(m_x->c, 0, w, name, type, 0, 256), nullptr);
        QByteArray value;
        if (r)
            value = QByteArray(static_cast<const char *>(xcb_get_property_value(r)), xcb_get_property_value_length(r));
        free(r);
        return value;
    };
    const QByteArray name = property(m_x->wmName, m_x->utf8);
    const QString title = !name.isEmpty() ? QString::fromUtf8(name) : QString::fromLatin1(property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING));
    processEvents(); // the replies may have brought events with them
    return title;
}

void X11Source::processEvents()
{
    // The xkb events share one event code; their own type is the second byte.
    struct XkbAny
    {
        uint8_t responseType;
        uint8_t xkbType;
        uint16_t sequence;
        xcb_timestamp_t time;
        uint8_t deviceID;
    };
    bool keymapChanged = false;
    for (;;) {
        xcb_generic_event_t *e = xcb_poll_for_event(m_x->c);
        if (!e) {
            if (!keymapChanged)
                break;
            // Read now: its replies may queue further events, which the socket would not announce.
            keymapChanged = false;
            readKeymap();
            continue;
        }
        const uint8_t type = e->response_type & 0x7F;
        if (type == m_x->xkbEvent) {
            const auto *any = reinterpret_cast<const XkbAny *>(e);
            if (any->deviceID == m_x->device) {
                if (any->xkbType == XCB_XKB_NEW_KEYBOARD_NOTIFY || any->xkbType == XCB_XKB_MAP_NOTIFY)
                    keymapChanged = true;
                else if (any->xkbType == XCB_XKB_STATE_NOTIFY)
                    setGroup(reinterpret_cast<const xcb_xkb_state_notify_event_t *>(e)->group);
            }
        } else if (type == XCB_PROPERTY_NOTIFY) {
            const auto *p = reinterpret_cast<const xcb_property_notify_event_t *>(e);
            if (p->window == m_x->root && p->atom == m_x->activeWindow)
                readActiveWindow();
        }
        free(e);
    }
    if (xcb_connection_has_error(m_x->c) && m_notifier)
        m_notifier->setEnabled(false);
}

#endif // TS_HAVE_X11
