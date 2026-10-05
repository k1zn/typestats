// The layout sources of the wlroots-style compositors that talk over Unix sockets: sway and Hyprland.

#include "LayoutSources_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#ifdef TS_HAVE_XKBREGISTRY
#include <xkbcommon/xkbregistry.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// --- Unix sockets ---

int UnixSocket::connect(const QByteArray &path)
{
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (path.isEmpty() || size_t(path.size()) >= sizeof(address.sun_path))
        return -1;
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool UnixSocket::writeAll(int fd, const QByteArray &data)
{
    for (qsizetype done = 0; done < data.size();) {
        const ssize_t n = ::send(fd, data.constData() + done, size_t(data.size() - done), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

std::optional<QByteArray> UnixSocket::readUntil(int fd, int timeoutMs, const std::function<bool(const QByteArray &)> &complete)
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    QByteArray data;
    char buffer[65536];
    for (;;) {
        const int left = int(duration_cast<milliseconds>(deadline - steady_clock::now()).count());
        pollfd p = {fd, POLLIN, 0};
        if (left <= 0 || poll(&p, 1, left) <= 0)
            return std::nullopt;
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) // closed: what came is all
            return data;
        data.append(buffer, n);
        if (complete && complete(data))
            return data;
    }
}

bool UnixSocket::readAvailable(int fd, QByteArray &into)
{
    char buffer[65536];
    for (;;) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n > 0) {
            into.append(buffer, n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
    }
}

// --- sway ---

namespace {

// "English (US)", "Russian (phonetic)" → us, ru(phonetic) by the XKB registry (the descriptions
// sway gives are the keymap's names of the layouts, which are the registry's descriptions).
Desktop::XkbNames namesFromDescriptions(const QStringList &descriptions)
{
#ifdef TS_HAVE_XKBREGISTRY
    rxkb_context *context = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
    if (!context)
        return {};
    QStringList ids;
    if (rxkb_context_parse_default_ruleset(context)) {
        for (const QString &description : descriptions) {
            QString id;
            for (rxkb_layout *l = rxkb_layout_first(context); l; l = rxkb_layout_next(l)) {
                const char *d = rxkb_layout_get_description(l);
                if (d && description == QString::fromUtf8(d)) {
                    id = QString::fromUtf8(rxkb_layout_get_name(l));
                    if (const char *variant = rxkb_layout_get_variant(l); variant && *variant)
                        id += u'+' + QString::fromUtf8(variant);
                    break;
                }
            }
            if (id.isEmpty()) // unknown: the layouts would not be in their places
                break;
            ids << id;
        }
    }
    rxkb_context_unref(context);
    if (ids.size() == descriptions.size())
        return Desktop::namesFromIds(ids);
#else
    Q_UNUSED(descriptions)
#endif
    return {};
}

} // namespace

SwaySource::~SwaySource()
{
    if (m_events >= 0)
        ::close(m_events);
}

std::optional<QByteArray> SwaySource::request(quint32 type, const QByteArray &payload)
{
    const int fd = UnixSocket::connect(m_path);
    if (fd < 0)
        return std::nullopt;
    std::optional<QByteArray> reply;
    if (UnixSocket::writeAll(fd, Desktop::I3::message(type, payload))) {
        if (std::optional<QByteArray> data = UnixSocket::readUntil(fd, 1000, [](const QByteArray &d) {
                QByteArray copy = d;
                return Desktop::I3::take(copy).has_value();
            })) {
            if (auto m = Desktop::I3::take(*data); m && m->first == type)
                reply = m->second;
        }
    }
    ::close(fd);
    return reply;
}

bool SwaySource::start()
{
    m_path = qgetenv("SWAYSOCK");
    const std::optional<QByteArray> inputs = request(Desktop::I3::GetInputs);
    if (!inputs)
        return false;
    const QList<Desktop::SwayKeyboard> keyboards = Desktop::parseSwayInputs(*inputs);
    if (keyboards.isEmpty())
        setNames(systemNames(), false);
    else
        useKeyboard(keyboards.first());
    if (const std::optional<QByteArray> tree = request(Desktop::I3::GetTree))
        if (auto w = Desktop::parseSwayTree(*tree))
            setWindow(*w);

    m_events = UnixSocket::connect(m_path);
    if (m_events >= 0
        && UnixSocket::writeAll(m_events, Desktop::I3::message(Desktop::I3::Subscribe, R"(["input","window"])"))) {
        m_notifier = new QSocketNotifier(m_events, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &SwaySource::readEvents);
    }
    return keymap() != nullptr;
}

void SwaySource::readEvents()
{
    const bool open = UnixSocket::readAvailable(m_events, m_buffer);
    while (auto m = Desktop::I3::take(m_buffer)) {
        if (m->first == Desktop::I3::InputEvent) {
            if (auto k = Desktop::parseSwayInputEvent(m->second))
                useKeyboard(*k);
        } else if (m->first == Desktop::I3::WindowEvent) {
            if (auto w = Desktop::parseSwayWindowEvent(m->second))
                setWindow(*w);
        }
    }
    if (!open) { // sway is gone
        m_notifier->setEnabled(false);
        ::close(m_events);
        m_events = -1;
    }
}

void SwaySource::useKeyboard(const Desktop::SwayKeyboard &k)
{
    if (k.layoutNames != m_layoutNames || !keymap()) {
        m_layoutNames = k.layoutNames;
        const Desktop::XkbNames names = namesFromDescriptions(k.layoutNames);
        setNames(names.isEmpty() ? systemNames() : names);
    }
    // The keymap's own names are the same descriptions: the index is checked against them.
    const int byName = k.active >= 0 && k.active < k.layoutNames.size() ? groupOfName(k.layoutNames[k.active]) : -1;
    setGroup(byName >= 0 ? byName : std::max(0, k.active));
}

// --- Hyprland ---

HyprlandSource::~HyprlandSource()
{
    if (m_events >= 0)
        ::close(m_events);
}

std::optional<QByteArray> HyprlandSource::request(const QByteArray &command)
{
    const int fd = UnixSocket::connect(QFile::encodeName(m_dir + QStringLiteral("/.socket.sock")));
    if (fd < 0)
        return std::nullopt;
    std::optional<QByteArray> reply;
    if (UnixSocket::writeAll(fd, command))
        reply = UnixSocket::readUntil(fd, 1000, {}); // the reply ends with the connection
    ::close(fd);
    return reply;
}

bool HyprlandSource::start()
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    m_dir = qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/hypr/") + signature;
    if (!QFileInfo::exists(m_dir + QStringLiteral("/.socket.sock")))
        m_dir = QStringLiteral("/tmp/hypr/") + signature; // before Hyprland 0.40
    const std::optional<QByteArray> devices = request("j/devices");
    if (!devices)
        return false;
    readDevices();
    if (const std::optional<QByteArray> active = request("j/activewindow"))
        if (auto w = Desktop::parseHyprActiveWindow(*active))
            setWindow(*w);

    m_events = UnixSocket::connect(QFile::encodeName(m_dir + QStringLiteral("/.socket2.sock")));
    if (m_events >= 0) {
        m_notifier = new QSocketNotifier(m_events, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &HyprlandSource::readEvents);
    }
    return keymap() != nullptr;
}

void HyprlandSource::readDevices()
{
    const std::optional<QByteArray> devices = request("j/devices");
    const QList<Desktop::HyprKeyboard> keyboards = devices ? Desktop::parseHyprDevices(*devices) : QList<Desktop::HyprKeyboard>{};
    if (keyboards.isEmpty()) {
        if (!keymap())
            setNames(systemNames(), false);
        return;
    }
    auto main = std::find_if(keyboards.begin(), keyboards.end(), [](const Desktop::HyprKeyboard &k) { return k.main; });
    const Desktop::HyprKeyboard &k = main != keyboards.end() ? *main : keyboards.first();
    m_keyboard = k.name;
    setNames(k.names.isEmpty() ? systemNames() : k.names);
    if (const int group = groupOfName(k.activeKeymap); group >= 0)
        setGroup(group);
}

void HyprlandSource::readEvents()
{
    const bool open = UnixSocket::readAvailable(m_events, m_buffer);
    for (qsizetype end; (end = m_buffer.indexOf('\n')) >= 0;) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        const auto event = Desktop::parseHyprEvent(line);
        if (!event)
            continue;
        if (event->first == QLatin1String("activelayout")) {
            const QString layout = Desktop::splitHyprPair(event->second).second;
            const int group = groupOfName(layout);
            if (group >= 0)
                setGroup(group);
            else
                readDevices(); // a layout this keymap does not have: the configuration changed
        } else if (event->first == QLatin1String("configreloaded")) {
            readDevices();
        } else if (event->first == QLatin1String("activewindow")) {
            setWindow({window().id, Desktop::splitHyprPair(event->second).second});
        } else if (event->first == QLatin1String("activewindowv2")) {
            bool ok = false;
            const quint64 id = event->second.toULongLong(&ok, 16);
            setWindow({ok ? id : 0, window().title});
        }
    }
    if (!open) {
        m_notifier->setEnabled(false);
        ::close(m_events);
        m_events = -1;
    }
}
