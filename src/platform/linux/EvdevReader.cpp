#include "EvdevReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <linux/input.h>

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include <xkbcommon/xkbcommon.h>

namespace {

QString readSmall(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.read(4096)).trimmed() : QString();
}

} // namespace

EvdevReader::EvdevReader(EventSink onEvent, std::function<void()> onOpened, const QString &sysRoot, const QString &devRoot)
    : m_onEvent(std::move(onEvent)), m_onOpened(std::move(onOpened)), m_sysRoot(sysRoot), m_devRoot(devRoot)
{
}

EvdevReader::~EvdevReader()
{
    stop();
    xkb_keymap_unref(m_pendingKeymap);
}

bool EvdevReader::testBit(const QString &bitmap, int bit)
{
    const QStringList words = bitmap.split(u' ', Qt::SkipEmptyParts);
    constexpr int wordBits = int(sizeof(long)) * CHAR_BIT; // the kernel's long
    const int index = bit / wordBits;
    if (index >= words.size())
        return false;
    bool ok = false;
    const quint64 word = words[words.size() - 1 - index].toULongLong(&ok, 16);
    return ok && (word >> (bit % wordBits)) & 1;
}

bool EvdevReader::isKeyboard(const QString &evBits, const QString &keyBits)
{
    return testBit(evBits, EV_KEY) && testBit(keyBits, KEY_A) && testBit(keyBits, KEY_Z) && testBit(keyBits, KEY_SPACE)
           && testBit(keyBits, KEY_ENTER);
}

EvdevReader::Open EvdevReader::open(const QString &name)
{
    if (m_devices.count(name))
        return Open::Ok;
    const QString sys = m_sysRoot + u'/' + name + QStringLiteral("/device");
    if (!isKeyboard(readSmall(sys + QStringLiteral("/capabilities/ev")), readSmall(sys + QStringLiteral("/capabilities/key"))))
        return Open::NotKeyboard;
    const QByteArray path = QFile::encodeName(m_devRoot + u'/' + name);
    const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return errno == EACCES || errno == EPERM ? Open::Denied : Open::Failed;
    // Times in the scale of std::chrono::steady_clock. Not a device (tests): whatever comes.
    int clock = CLOCK_MONOTONIC;
    ioctl(fd, EVIOCSCLOCKID, &clock);
    Device d;
    d.fd = fd;
    // uinput devices (ydotool, keyd, kanata...) are virtual: their keys are "injected", as SendInput's.
    d.injected = readSmall(sys + QStringLiteral("/id/bustype")).toUInt(nullptr, 16) == BUS_VIRTUAL
                 || QFileInfo(m_sysRoot + u'/' + name).canonicalFilePath().contains(QLatin1String("/devices/virtual/"));
    unsigned long leds[(LED_CNT + CHAR_BIT * sizeof(long) - 1) / (CHAR_BIT * sizeof(long))] = {};
    if (ioctl(fd, EVIOCGLED(sizeof(leds)), leds) >= 0) {
        auto on = [&leds](int led) { return (leds[led / (CHAR_BIT * sizeof(long))] >> (led % (CHAR_BIT * sizeof(long)))) & 1; };
        m_keyboard.setLocks(on(LED_CAPSL), on(LED_NUML));
        m_caps = m_keyboard.capsLock();
    }
    m_devices[name] = d;
    return Open::Ok;
}

void EvdevReader::close(const QString &name)
{
    auto it = m_devices.find(name);
    if (it == m_devices.end())
        return;
    ::close(it->second.fd);
    m_devices.erase(it);
}

bool EvdevReader::start(Status *status)
{
    if (m_thread.joinable())
        return true;
    m_stop = false;
    m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    m_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_inotify < 0 || m_wake < 0)
        return false;
    inotify_add_watch(m_inotify, QFile::encodeName(m_devRoot).constData(), IN_CREATE | IN_ATTRIB | IN_DELETE);
    applyPending();
    Status s;
    for (const QString &name : QDir(m_sysRoot).entryList({QStringLiteral("event*")}, QDir::AllEntries | QDir::System)) {
        switch (open(name)) {
        case Open::Ok:
            ++s.keyboards;
            ++s.open;
            break;
        case Open::Denied:
        case Open::Failed:
            ++s.keyboards;
            ++s.denied;
            break;
        case Open::NotKeyboard:
            break;
        }
    }
    m_openedOnce = s.open > 0;
    if (status)
        *status = s;
    m_thread = std::thread([this] { run(); });
    return true;
}

void EvdevReader::stop()
{
    if (m_thread.joinable()) {
        m_stop = true;
        const quint64 one = 1;
        [[maybe_unused]] const auto written = ::write(m_wake, &one, sizeof(one));
        m_thread.join();
    }
    while (!m_devices.empty())
        close(m_devices.begin()->first);
    for (int *fd : {&m_inotify, &m_wake})
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
}

void EvdevReader::setKeymap(xkb_keymap *keymap)
{
    if (!keymap)
        return;
    const std::lock_guard lock(m_mutex);
    xkb_keymap_ref(keymap);
    xkb_keymap_unref(m_pendingKeymap);
    m_pendingKeymap = keymap;
}

void EvdevReader::setGroup(int group)
{
    const std::lock_guard lock(m_mutex);
    m_pendingGroup = group;
    m_groupPending = true;
}

void EvdevReader::applyPending()
{
    const std::lock_guard lock(m_mutex);
    if (m_pendingKeymap) {
        m_keyboard.setKeymap(m_pendingKeymap);
        xkb_keymap_unref(m_pendingKeymap);
        m_pendingKeymap = nullptr;
        m_caps = m_keyboard.capsLock();
    }
    if (m_groupPending) {
        m_keyboard.setGroup(m_pendingGroup);
        m_groupPending = false;
    }
}

void EvdevReader::run()
{
    std::vector<pollfd> fds;
    std::vector<QString> names;
    while (!m_stop) {
        fds.assign({{m_wake, POLLIN, 0}, {m_inotify, POLLIN, 0}});
        names.clear();
        for (const auto &[name, d] : m_devices) {
            fds.push_back({d.fd, POLLIN, 0});
            names.push_back(name);
        }
        if (poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents) {
            quint64 value;
            [[maybe_unused]] const auto n = ::read(m_wake, &value, sizeof(value));
            if (m_stop)
                break;
        }
        if (fds[1].revents)
            readInotify();
        for (size_t i = 2; i < fds.size(); ++i) {
            auto it = m_devices.find(names[i - 2]);
            if (it == m_devices.end() || it->second.fd != fds[i].fd)
                continue;
            if (fds[i].revents & POLLIN)
                readDevice(it->first, it->second);
            else if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                close(names[i - 2]);
        }
    }
}

void EvdevReader::readDevice(const QString &name, Device &d)
{
    input_event events[64];
    for (;;) {
        const ssize_t n = ::read(d.fd, events, sizeof(events));
        if (n < 0 && errno == EAGAIN)
            return;
        if (n <= 0) { // unplugged (ENODEV), or the end of a pipe
            close(name);
            return;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i) {
            const input_event &e = events[i];
            if (e.type == EV_SYN) {
                if (e.code == SYN_DROPPED)
                    d.dropping = true;
                else if (e.code == SYN_REPORT)
                    d.dropping = false;
                continue;
            }
            if (d.dropping)
                continue;
            if (e.type == EV_LED && (e.code == LED_CAPSL || e.code == LED_NUML)) {
                // The desktop lit the LED: the lock is as it says (another keyboard, a lock set elsewhere).
                const bool caps = e.code == LED_CAPSL ? e.value != 0 : m_keyboard.capsLock();
                const bool num = e.code == LED_NUML ? e.value != 0 : m_keyboard.numLock();
                m_keyboard.setLocks(caps, num);
                m_caps = caps;
            } else if (e.type == EV_KEY) {
                applyPending();
                const qint64 timeUs = qint64(e.input_event_sec) * 1000000 + e.input_event_usec;
                const std::optional<HookEvent> event = m_keyboard.event(e.code, e.value, timeUs, d.injected);
                m_caps = m_keyboard.capsLock();
                if (event && m_onEvent)
                    m_onEvent(*event);
            }
        }
    }
}

void EvdevReader::readInotify()
{
    alignas(inotify_event) char buffer[4096];
    for (;;) {
        const ssize_t n = ::read(m_inotify, buffer, sizeof(buffer));
        if (n <= 0)
            return;
        for (ssize_t at = 0; at < n;) {
            const auto *e = reinterpret_cast<const inotify_event *>(buffer + at);
            at += ssize_t(sizeof(inotify_event) + e->len);
            const QString name = e->len ? QFile::decodeName(e->name) : QString();
            if (!name.startsWith(QLatin1String("event")))
                continue;
            if (e->mask & IN_DELETE) {
                close(name);
            } else if (m_devices.count(name)) {
                // The rights changed: a keyboard that is no longer ours (the user left the seat) is let go.
                if (e->mask & IN_ATTRIB && access(QFile::encodeName(m_devRoot + u'/' + name).constData(), R_OK) != 0)
                    close(name);
            } else if (open(name) == Open::Ok && !m_openedOnce) {
                m_openedOnce = true;
                if (m_onOpened)
                    m_onOpened();
            }
        }
    }
}
