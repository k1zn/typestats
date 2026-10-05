#pragma once

// The layout sources (LayoutSource.h), one per desktop.

#include "LayoutSource.h"

#include <QByteArray>

#include <functional>
#include <optional>

class QProcess;
class QSocketNotifier;

// The system's configuration; the keymap switches layouts by itself (its grp: options).
class SystemSource : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    QString name() const override { return QStringLiteral("system"); }
    bool start() override;
};

#ifdef TS_HAVE_DBUS
// KDE Plasma (Wayland): org.kde.keyboard /Layouts on the session bus says which layout is on;
// the list is in kxkbrc.
class KdeSource : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    QString name() const override { return QStringLiteral("kde"); }
    bool start() override;

private slots:
    void layoutChanged(uint index);
    void layoutListChanged();

private:
    void readList();
};
#endif

// GNOME (Wayland): gsettings org.gnome.desktop.input-sources: `sources` and `xkb-options`; the layout
// on is the first of `mru-sources`. Followed with `gsettings monitor`.
class GnomeSource : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    ~GnomeSource() override;
    QString name() const override { return QStringLiteral("gnome"); }
    bool start() override;

private:
    void apply();
    QString get(const char *key);

    QList<std::pair<QString, QString>> m_sources; // xkb sources only: their index is the group
    QString m_options;
    std::pair<QString, QString> m_current;
    QProcess *m_monitor = nullptr;
};

// sway: the i3 IPC socket ($SWAYSOCK). Layout names are descriptions ("Russian"): the XKB names come
// from the XKB registry. Also the focused window.
class SwaySource : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    ~SwaySource() override;
    QString name() const override { return QStringLiteral("sway"); }
    bool start() override;

private:
    std::optional<QByteArray> request(quint32 type, const QByteArray &payload = {});
    void readEvents();
    void useKeyboard(const Desktop::SwayKeyboard &k);

    QByteArray m_path;
    int m_events = -1;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
    QStringList m_layoutNames;
};

// Hyprland: hyprctl's socket (j/devices, j/activewindow) and the event socket (.socket2.sock).
class HyprlandSource : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    ~HyprlandSource() override;
    QString name() const override { return QStringLiteral("hyprland"); }
    bool start() override;

private:
    std::optional<QByteArray> request(const QByteArray &command);
    void readDevices();
    void readEvents();

    QString m_dir;
    int m_events = -1;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
    QString m_keyboard; // the main keyboard
    Desktop::XkbNames m_names;
};

#ifdef TS_HAVE_X11
// An X11 session: the keymap and the layout on from the X server (xkbcommon-x11), the active window
// from _NET_ACTIVE_WINDOW.
class X11Source : public LayoutSource
{
    Q_OBJECT
public:
    using LayoutSource::LayoutSource;
    ~X11Source() override;
    QString name() const override { return QStringLiteral("x11"); }
    bool start() override;
    QString windowTitle() override;

private:
    void readKeymap();
    void readActiveWindow();
    void processEvents();
    quint32 atom(const char *name);

    struct Connection;
    Connection *m_x = nullptr;
    QSocketNotifier *m_notifier = nullptr;
};
#endif

// Unix sockets of the Wayland compositors.
namespace UnixSocket {
int connect(const QByteArray &path); // a blocking socket, -1 on failure
bool writeAll(int fd, const QByteArray &data);
// Reads until `complete` says the data is whole or the peer closes; nothing on a timeout.
std::optional<QByteArray> readUntil(int fd, int timeoutMs, const std::function<bool(const QByteArray &)> &complete);
// What is there to read now (non-blocking); false when the peer has closed.
bool readAvailable(int fd, QByteArray &into);
}
