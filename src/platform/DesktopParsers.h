#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// What the Linux desktops say about the keyboard layouts and the active window, parsed. Only QtCore:
// the parsers are tested on every system (tests/tst_platform.cpp); the sources that talk to the
// desktops are in src/platform/linux/.
namespace Desktop {

// A keyboard configuration in XKB names (RMLVO), as xkb_keymap_new_from_names takes it; the lists
// are comma-separated, one entry per layout (group).
struct XkbNames
{
    QString model, layout, variant, options;
    bool operator==(const XkbNames &) const = default;
    bool isEmpty() const { return layout.isEmpty(); }
    int layoutCount() const { return layout.isEmpty() ? 0 : int(layout.count(u',')) + 1; }
};

// Builds names from "us", "ru+phonetic"-like ids (GNOME) or layout/variant pairs.
XkbNames namesFromIds(const QStringList &ids, const QString &options = {});
// Without the "grp:" options (switching the layout): the desktop switches, and says so.
QString withoutGroupSwitching(const QString &options);

struct ActiveWindow
{
    quint64 id = 0;
    QString title;
    bool operator==(const ActiveWindow &) const = default;
};

// --- sway: the i3 IPC protocol ("i3-ipc", length, type, JSON) ---
namespace I3 {
enum : quint32 { Subscribe = 2, GetTree = 4, GetInputs = 100, EventBit = 0x80000000u, WindowEvent = EventBit | 3,
                 InputEvent = EventBit | 21 };
QByteArray message(quint32 type, const QByteArray &payload = {});
// Takes a complete message off the front of `buffer`: {type, payload}.
std::optional<std::pair<quint32, QByteArray>> take(QByteArray &buffer);
}

struct SwayKeyboard
{
    QString identifier;
    QStringList layoutNames; // descriptions: "English (US)", "Russian (phonetic)"
    int active = -1;
};
// GET_INPUTS reply: the keyboards that have layouts.
QList<SwayKeyboard> parseSwayInputs(const QByteArray &json);
// "input" event: the keyboard whose layout or keymap changed.
std::optional<SwayKeyboard> parseSwayInputEvent(const QByteArray &json);
// "window" event: the focused window (focus or title change of the focused one).
std::optional<ActiveWindow> parseSwayWindowEvent(const QByteArray &json);
// GET_TREE reply: the focused window.
std::optional<ActiveWindow> parseSwayTree(const QByteArray &json);

// --- Hyprland: hyprctl's socket (j/devices, j/activewindow) and the event socket (socket2) ---
struct HyprKeyboard
{
    QString name;
    XkbNames names;
    QString activeKeymap; // the description of the active layout
    bool main = false;
};
QList<HyprKeyboard> parseHyprDevices(const QByteArray &json);
std::optional<ActiveWindow> parseHyprActiveWindow(const QByteArray &json);
// One line of socket2: "activelayout>>keyboard,Russian" → {"activelayout", "keyboard,Russian"}.
std::optional<std::pair<QString, QString>> parseHyprEvent(const QByteArray &line);
// "keyboard,Russian" → {"keyboard", "Russian"}.
std::pair<QString, QString> splitHyprPair(const QString &data);

// --- GNOME: gsettings org.gnome.desktop.input-sources ---
// "[('xkb', 'us'), ('xkb', 'ru+phonetic'), ('ibus', 'mozc')]" → {{"xkb","us"}, ...}; "@a(ss) []" → {}.
QList<std::pair<QString, QString>> parseGnomeSources(const QString &text);
// "['grp:alt_shift_toggle', 'compose:ralt']" → list; "@as []" → {}.
QStringList parseGVariantStrings(const QString &text);
// A line of `gsettings monitor`: "mru-sources: [('xkb', 'ru')]" → {"mru-sources", "[('xkb', 'ru')]"}.
std::optional<std::pair<QString, QString>> parseGsettingsMonitorLine(const QString &line);

// --- KDE: ~/.config/kxkbrc, [Layout] LayoutList, VariantList, Options, Model; empty when Use=false ---
XkbNames parseKxkbrc(const QString &text);

// --- /etc/default/keyboard (Debian) or /etc/vconsole.conf-like KEY="value" lines ---
XkbNames parseDefaultKeyboard(const QString &text);

}
