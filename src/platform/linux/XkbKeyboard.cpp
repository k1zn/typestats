#include "XkbKeyboard.h"

#include "core/KeyName.h"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdlib>

namespace {

// evdev codes of the modifiers.
enum : int { LeftCtrl = 29, LeftShift = 42, RightShift = 54, LeftAlt = 56, CapsLockKey = 58, NumLockKey = 69,
             RightCtrl = 97, RightAlt = 100, LeftMeta = 125, RightMeta = 126 };
constexpr int kKeycodeOffset = 8; // XKB keycode = evdev code + 8

bool isDeadKeysym(xkb_keysym_t sym)
{
    return sym >= XKB_KEY_dead_grave && sym <= XKB_KEY_dead_greek;
}

// The character ToUnicodeEx gives for a key without Ctrl, from its keysym.
char16_t plainChar(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Delete:
    case XKB_KEY_KP_Delete:
        return 0; // xkb gives DEL (0x7F); Windows nothing
    case XKB_KEY_BackSpace:
        return 0x08;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
    case XKB_KEY_KP_Tab:
        return 0x09;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        return 0x0D;
    case XKB_KEY_Escape:
        return 0x1B;
    }
    const quint32 u = xkb_keysym_to_utf32(sym);
    if (u < 0x20 || u == 0x7F || u > 0xFFFF)
        return 0;
    return char16_t(u);
}

// The character ToUnicodeEx gives for Ctrl and a key (the Ctrl columns of the US layout), by VK.
char16_t ctrlChar(quint8 vk, bool shift)
{
    if (vk >= 'A' && vk <= 'Z')
        return char16_t(vk - 0x40);
    switch (vk) {
    case 0xDB: return 0x1B;              // [
    case 0xDC: case 0xE2: return 0x1C;   // backslashes
    case 0xDD: return 0x1D;              // ]
    case '6': return shift ? 0x1E : 0;   // Ctrl+^
    case 0xBD: return shift ? 0x1F : 0;  // Ctrl+_
    case 0x08: return 0x7F;              // Ctrl+BackSpace
    case 0x0D: return 0x0A;              // Ctrl+Enter
    case 0x1B: return 0x1B;
    case 0x20: return 0x20;
    }
    return 0;
}

QString utf8Of(xkb_compose_state *compose)
{
    char buffer[64];
    const int n = xkb_compose_state_get_utf8(compose, buffer, sizeof(buffer));
    return n > 0 ? QString::fromUtf8(buffer, std::min<int>(n, sizeof(buffer) - 1)) : QString();
}

QByteArray locale()
{
    for (const char *name : {"LC_ALL", "LC_CTYPE", "LANG"})
        if (const char *value = std::getenv(name); value && *value)
            return value;
    return "C";
}

} // namespace

xkb_context *XkbKeyboard::sharedContext()
{
    static xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    return context;
}

xkb_keymap *XkbKeyboard::compile(const Desktop::XkbNames &names)
{
    if (!sharedContext())
        return nullptr;
    const QByteArray model = names.model.toUtf8(), layout = names.layout.toUtf8(), variant = names.variant.toUtf8(),
                     options = names.options.toUtf8();
    xkb_rule_names rmlvo = {};
    rmlvo.rules = "evdev";
    rmlvo.model = model.isEmpty() ? "pc105" : model.constData();
    rmlvo.layout = layout.isEmpty() ? nullptr : layout.constData();
    rmlvo.variant = variant.isEmpty() ? nullptr : variant.constData();
    rmlvo.options = options.isEmpty() ? nullptr : options.constData();
    return xkb_keymap_new_from_names(sharedContext(), &rmlvo, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

XkbKeyboard::XkbKeyboard()
{
    if (xkb_context *context = sharedContext()) {
        m_composeTable = xkb_compose_table_new_from_locale(context, locale().constData(), XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (!m_composeTable)
            m_composeTable = xkb_compose_table_new_from_locale(context, "en_US.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (m_composeTable)
            m_compose = xkb_compose_state_new(m_composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

XkbKeyboard::~XkbKeyboard()
{
    xkb_compose_state_unref(m_compose);
    xkb_compose_table_unref(m_composeTable);
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
}

bool XkbKeyboard::setKeymap(const Desktop::XkbNames &names)
{
    xkb_keymap *keymap = compile(names);
    if (!keymap)
        return false;
    setKeymap(keymap);
    xkb_keymap_unref(keymap);
    return true;
}

void XkbKeyboard::setKeymap(xkb_keymap *keymap)
{
    if (!keymap || keymap == m_keymap)
        return;
    const bool caps = capsLock(), num = numLock();
    xkb_keymap_ref(keymap);
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    m_keymap = keymap;
    m_state = xkb_state_new(keymap);
    // The keys still held (Shift...) stay held in the new state; the locks are set, not toggled.
    for (int code = 0; code < int(m_held.size()); ++code)
        if (m_held[code] && code != CapsLockKey && code != NumLockKey)
            xkb_state_update_key(m_state, code + kKeycodeOffset, XKB_KEY_DOWN);
    setLocks(caps, num);
    if (m_compose)
        xkb_compose_state_reset(m_compose);
    m_deadKeysym = 0;
}

void XkbKeyboard::setLocks(bool caps, bool num)
{
    if (!m_state)
        return;
    xkb_mod_mask_t locked = xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LOCKED);
    auto set = [&](const char *name, bool on) {
        const xkb_mod_index_t i = xkb_keymap_mod_get_index(m_keymap, name);
        if (i == XKB_MOD_INVALID)
            return;
        if (on)
            locked |= xkb_mod_mask_t(1) << i;
        else
            locked &= ~(xkb_mod_mask_t(1) << i);
    };
    set(XKB_MOD_NAME_CAPS, caps);
    set(XKB_MOD_NAME_NUM, num);
    xkb_state_update_mask(m_state, xkb_state_serialize_mods(m_state, XKB_STATE_MODS_DEPRESSED),
                          xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LATCHED), locked,
                          xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_DEPRESSED),
                          xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_LATCHED),
                          xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_LOCKED));
}

bool XkbKeyboard::capsLock() const
{
    return m_state && xkb_state_mod_name_is_active(m_state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0;
}

bool XkbKeyboard::numLock() const
{
    return m_state && xkb_state_mod_name_is_active(m_state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0;
}

void XkbKeyboard::applyGroup()
{
    // The desktop's layout: the keymap's own switching (its grp: options seen in our state) is overruled.
    if (m_group < 0 || !m_state || xkb_layout_index_t(m_group) >= xkb_keymap_num_layouts(m_keymap))
        return;
    if (xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_EFFECTIVE) == xkb_layout_index_t(m_group))
        return;
    xkb_state_update_mask(m_state, xkb_state_serialize_mods(m_state, XKB_STATE_MODS_DEPRESSED),
                          xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LATCHED),
                          xkb_state_serialize_mods(m_state, XKB_STATE_MODS_LOCKED), 0, 0, xkb_layout_index_t(m_group));
}

char16_t XkbKeyboard::spacingOf(quint32 deadKeysym) const
{
    // What the layout types for the dead key and a space (a dead key alone, as Windows gives it).
    if (m_composeTable) {
        xkb_compose_state *probe = xkb_compose_state_new(m_composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
        xkb_compose_state_feed(probe, deadKeysym);
        xkb_compose_state_feed(probe, XKB_KEY_space);
        const QString s = xkb_compose_state_get_status(probe) == XKB_COMPOSE_COMPOSED ? utf8Of(probe) : QString();
        xkb_compose_state_unref(probe);
        if (s.size() == 1)
            return s[0].unicode();
    }
    switch (deadKeysym) {
    case XKB_KEY_dead_grave: return u'`';
    case XKB_KEY_dead_acute: return u'´';
    case XKB_KEY_dead_circumflex: return u'^';
    case XKB_KEY_dead_tilde: return u'~';
    case XKB_KEY_dead_diaeresis: return u'¨';
    case XKB_KEY_dead_cedilla: return u'¸';
    case XKB_KEY_dead_caron: return u'ˇ';
    case XKB_KEY_dead_abovering: return u'˚';
    }
    return plainChar(deadKeysym);
}

XkbKeyboard::Char XkbKeyboard::typed(quint32 sym)
{
    if (m_compose && sym != XKB_KEY_NoSymbol
        && xkb_compose_state_feed(m_compose, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
        switch (xkb_compose_state_get_status(m_compose)) {
        case XKB_COMPOSE_COMPOSING:
            // A dead key (or Compose): its character waits, as Windows' dead keys do.
            if (!m_deadKeysym)
                m_deadKeysym = isDeadKeysym(sym) ? sym : 0;
            return {-1, isDeadKeysym(sym) ? spacingOf(sym) : char16_t(0), 0};
        case XKB_COMPOSE_COMPOSED: {
            const QString s = utf8Of(m_compose);
            xkb_compose_state_reset(m_compose);
            m_deadKeysym = 0;
            if (!s.isEmpty())
                return {1, s[0].unicode(), s[0].unicode()};
            break;
        }
        case XKB_COMPOSE_CANCELLED: {
            // No such combination: the dead key's own character, then this key's (two characters).
            const char16_t first = m_deadKeysym ? spacingOf(m_deadKeysym) : 0;
            xkb_compose_state_reset(m_compose);
            m_deadKeysym = 0;
            const char16_t c = plainChar(sym);
            if (first && c)
                return {2, c, first};
            if (first || c)
                return {1, char16_t(first | c), char16_t(first | c)};
            return {};
        }
        case XKB_COMPOSE_NOTHING:
            break;
        }
    }
    const char16_t c = plainChar(sym);
    return c ? Char{1, c, c} : Char{};
}

XkbKeyboard::Char XkbKeyboard::characterOf(int keycode, quint8 vk, bool ctrl, bool alt, bool shift)
{
    applyGroup();
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_state, keycode);
    if (ctrl && !alt) {
        const char16_t c = ctrlChar(vk, shift);
        return c ? Char{1, c, c} : Char{};
    }
    if (ctrl && alt) {
        // Ctrl+Alt is AltGr for Windows: the third-level character when the layout has one there.
        if (xkb_state_mod_name_is_active(m_state, "Mod5", XKB_STATE_MODS_EFFECTIVE) <= 0)
            return {};
    }
    return typed(sym);
}

std::optional<HookEvent> XkbKeyboard::event(int code, int value, qint64 timeUs, bool injected)
{
    if (code < 0 || code >= int(m_held.size()))
        return std::nullopt;
    const bool down = value != 0;
    std::optional<HookEvent> out;
    if (const std::optional<Keyboard::ScanCode> scan = Keyboard::evdevToScan(code)) {
        HookEvent e;
        e.timeUs = timeUs;
        quint32 flags = scan->code;
        if (scan->extended)
            flags |= KeyRecord::Extended;
        if (injected)
            flags |= KeyRecord::Injected;
        if (!down)
            flags |= KeyRecord::KeyUp;
        // The modifiers held before this event: a press of Shift has no Shift bit, its release has it.
        const bool shift = held(LeftShift) || held(RightShift), ctrl = held(LeftCtrl) || held(RightCtrl),
                   alt = held(LeftAlt) || held(RightAlt), win = held(LeftMeta) || held(RightMeta);
        if (shift)
            flags |= KeyRecord::Shift;
        if (ctrl)
            flags |= KeyRecord::Ctrl;
        if (alt)
            flags |= KeyRecord::Alt;
        if (win)
            flags |= KeyRecord::Win;
        const quint8 vk = Keyboard::scanToVk(*scan, numLock());
        Char c;
        if (down && m_state)
            c = characterOf(code + kKeycodeOffset, vk, ctrl, alt, shift);
        if (c.chars != 0) {
            flags |= KeyRecord::HasChar;
            e.ch = c.ch;
            e.firstCh = c.chars == 2 ? c.first : c.ch;
        } else {
            flags |= KeyRecord::NoChar;
        }
        if (c.chars < 0)
            flags |= KeyRecord::DeadKey;
        e.flags = flags | quint32(vk) << 16;
        e.chars = c.chars;
        out = e;
    }
    if (value != 2) { // an autorepeat changes no state
        m_held[code] = down;
        if (m_state)
            xkb_state_update_key(m_state, code + kKeycodeOffset, down ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
    return out;
}

int XkbKeyboard::toUnicode(Keyboard::ScanCode scan, bool shift, bool caps, char16_t out[2])
{
    out[0] = out[1] = 0;
    const int code = Keyboard::scanToEvdev(scan);
    if (!m_keymap || code < 0)
        return 0;
    // A state of its own: only Shift and Caps Lock, the desktop's layout.
    xkb_state *state = xkb_state_new(m_keymap);
    if (shift)
        xkb_state_update_key(state, LeftShift + kKeycodeOffset, XKB_KEY_DOWN);
    const xkb_mod_index_t lock = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_CAPS);
    const xkb_mod_mask_t locked = caps && lock != XKB_MOD_INVALID ? xkb_mod_mask_t(1) << lock : 0;
    xkb_state_update_mask(state, xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED), 0, locked, 0, 0,
                          m_group > 0 ? xkb_layout_index_t(m_group) : 0);
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(state, code + kKeycodeOffset);
    xkb_state_unref(state);
    const Char c = typed(sym);
    if (c.chars == 2) {
        out[0] = c.first;
        out[1] = c.ch;
    } else {
        out[0] = c.ch;
    }
    return c.chars;
}

void XkbKeyboard::clearDeadKey()
{
    if (m_compose)
        xkb_compose_state_reset(m_compose);
    m_deadKeysym = 0;
}

QString XkbKeyboard::keyName(quint8 scan, bool *dead)
{
    if (dead)
        *dead = false;
    const int code = Keyboard::scanToEvdev({scan, false});
    if (!m_keymap || code < 0)
        return {};
    xkb_state *state = xkb_state_new(m_keymap);
    xkb_state_update_mask(state, 0, 0, 0, 0, 0, m_group > 0 ? xkb_layout_index_t(m_group) : 0);
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(state, code + kKeycodeOffset);
    xkb_state_unref(state);
    const bool isDead = isDeadKeysym(sym);
    if (dead)
        *dead = isDead;
    const char16_t ch = isDead ? spacingOf(sym) : plainChar(sym);
    quint32 flags = scan | quint32(Keyboard::scanToVk({scan, false}, false)) << 16;
    if (!ch)
        flags |= KeyRecord::NoChar;
    return keyDisplayName(flags, ch);
}

int XkbKeyboard::groupOfName(const QString &description) const
{
    if (!m_keymap)
        return -1;
    for (xkb_layout_index_t i = 0; i < xkb_keymap_num_layouts(m_keymap); ++i)
        if (const char *name = xkb_keymap_layout_get_name(m_keymap, i); name && description == QString::fromUtf8(name))
            return int(i);
    return -1;
}
