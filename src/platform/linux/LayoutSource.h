#pragma once

#include "platform/DesktopParsers.h"

#include <QObject>

struct xkb_keymap;

// Where the keyboard layouts and the active window come from on Linux. Under Wayland only the
// compositor knows the layout that is on, so each desktop is asked in its own way; the keymap is
// compiled from its names (XKB RMLVO), the layout on is a group of it. Every source keeps a keymap
// (the system's configuration until the desktop answers).
//
// Adding a desktop: a subclass with start() that calls setNames()/setGroup() (and setWindow() if it
// knows the focused window), and a line in create().
class LayoutSource : public QObject
{
    Q_OBJECT
public:
    explicit LayoutSource(QObject *parent = nullptr);
    ~LayoutSource() override;

    // The source of this session: X11, sway, Hyprland, KDE (Wayland), GNOME (Wayland), else the system's
    // configuration (systemd-localed, /etc/default/keyboard, XKB_DEFAULT_*).
    static LayoutSource *create(QObject *parent = nullptr);

    virtual QString name() const = 0;
    // Connects; false when the desktop does not answer (the next source is tried).
    virtual bool start() = 0;

    xkb_keymap *keymap() const { return m_keymap; }
    // The layout on, a group of keymap(); -1 when the keymap switches by itself (its grp: options).
    int group() const { return m_group; }
    // The focused window, when the desktop tells it (X11, sway, Hyprland); id 0 otherwise.
    Desktop::ActiveWindow window() const { return m_window; }
    virtual QString windowTitle() { return m_window.title; }

    // The configuration of the system: what the desktop would use without its own settings.
    static Desktop::XkbNames systemNames();

signals:
    void keymapChanged();
    void groupChanged(int group);

protected:
    // Compiles the names; the desktop switches layouts (`desktopSwitches`), so its grp: options are
    // dropped. Names that do not compile give the system's configuration.
    void setNames(const Desktop::XkbNames &names, bool desktopSwitches = true);
    void setKeymap(xkb_keymap *keymap); // takes a reference
    void setGroup(int group);
    void setWindow(const Desktop::ActiveWindow &window) { m_window = window; }
    int groupOfName(const QString &description) const;

private:
    xkb_keymap *m_keymap = nullptr;
    Desktop::XkbNames m_names;
    int m_group = -1;
    Desktop::ActiveWindow m_window;
};
