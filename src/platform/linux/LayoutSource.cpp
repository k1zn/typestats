#include "LayoutSource.h"

#include "LayoutSources_p.h"
#include "XkbKeyboard.h"

#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#ifdef TS_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#endif

#include <xkbcommon/xkbcommon.h>

LayoutSource::LayoutSource(QObject *parent) : QObject(parent) {}

LayoutSource::~LayoutSource()
{
    xkb_keymap_unref(m_keymap);
}

LayoutSource *LayoutSource::create(QObject *parent)
{
    const QByteArray session = qgetenv("XDG_SESSION_TYPE");
    const bool wayland = session == "wayland" || (session != "x11" && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"));
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    std::vector<LayoutSource *> candidates;
#ifdef TS_HAVE_X11
    if (!wayland && !qEnvironmentVariableIsEmpty("DISPLAY"))
        candidates.push_back(new X11Source(parent));
#endif
    if (!qEnvironmentVariableIsEmpty("SWAYSOCK"))
        candidates.push_back(new SwaySource(parent));
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        candidates.push_back(new HyprlandSource(parent));
#ifdef TS_HAVE_DBUS
    if (desktop.contains(QLatin1String("kde")))
        candidates.push_back(new KdeSource(parent));
#endif
    if (desktop.contains(QLatin1String("gnome")) || desktop.contains(QLatin1String("unity")))
        candidates.push_back(new GnomeSource(parent));
    candidates.push_back(new SystemSource(parent));
    LayoutSource *chosen = nullptr;
    for (LayoutSource *s : candidates) {
        if (!chosen && s->start())
            chosen = s;
        else
            delete s;
    }
    return chosen;
}

Desktop::XkbNames LayoutSource::systemNames()
{
#ifdef TS_HAVE_DBUS
    // systemd-localed: the keyboard of the system (localectl set-x11-keymap). Asked with a short timeout.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.locale1"), QStringLiteral("/org/freedesktop/locale1"),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    call << QStringLiteral("org.freedesktop.locale1");
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 500);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const QVariantMap p = qdbus_cast<QVariantMap>(reply.arguments().first());
        Desktop::XkbNames n;
        n.layout = p.value(QStringLiteral("X11Layout")).toString();
        n.model = p.value(QStringLiteral("X11Model")).toString();
        n.variant = p.value(QStringLiteral("X11Variant")).toString();
        n.options = p.value(QStringLiteral("X11Options")).toString();
        if (!n.isEmpty())
            return n;
    }
#endif
    for (const char *path : {"/etc/default/keyboard", "/etc/vconsole.conf"}) {
        QFile f{QString::fromLatin1(path)};
        if (f.open(QIODevice::ReadOnly)) {
            const Desktop::XkbNames n = Desktop::parseDefaultKeyboard(QString::fromUtf8(f.readAll()));
            if (!n.isEmpty())
                return n;
        }
    }
    Desktop::XkbNames n;
    n.layout = qEnvironmentVariable("XKB_DEFAULT_LAYOUT", QStringLiteral("us"));
    n.variant = qEnvironmentVariable("XKB_DEFAULT_VARIANT");
    n.model = qEnvironmentVariable("XKB_DEFAULT_MODEL");
    n.options = qEnvironmentVariable("XKB_DEFAULT_OPTIONS");
    return n;
}

void LayoutSource::setNames(const Desktop::XkbNames &names, bool desktopSwitches)
{
    Desktop::XkbNames n = names;
    if (desktopSwitches)
        n.options = Desktop::withoutGroupSwitching(n.options);
    if (m_keymap && n == m_names)
        return;
    xkb_keymap *keymap = n.isEmpty() ? nullptr : XkbKeyboard::compile(n);
    if (!keymap && !m_keymap) {
        n = systemNames();
        keymap = XkbKeyboard::compile(n);
    }
    if (!keymap) {
        Desktop::XkbNames us;
        us.layout = QStringLiteral("us");
        keymap = XkbKeyboard::compile(us);
    }
    if (!keymap)
        return;
    m_names = n;
    setKeymap(keymap);
    xkb_keymap_unref(keymap);
}

void LayoutSource::setKeymap(xkb_keymap *keymap)
{
    if (!keymap || keymap == m_keymap)
        return;
    xkb_keymap_ref(keymap);
    xkb_keymap_unref(m_keymap);
    m_keymap = keymap;
    emit keymapChanged();
}

void LayoutSource::setGroup(int group)
{
    if (group == m_group)
        return;
    m_group = group;
    emit groupChanged(group);
}

int LayoutSource::groupOfName(const QString &description) const
{
    if (!m_keymap)
        return -1;
    for (xkb_layout_index_t i = 0; i < xkb_keymap_num_layouts(m_keymap); ++i)
        if (const char *name = xkb_keymap_layout_get_name(m_keymap, i); name && description == QString::fromUtf8(name))
            return int(i);
    return -1;
}

// --- the system's configuration ---

bool SystemSource::start()
{
    setNames(systemNames(), false);
    return keymap() != nullptr;
}

// --- KDE ---

#ifdef TS_HAVE_DBUS

namespace {
const QString kKdeService = QStringLiteral("org.kde.keyboard");
const QString kKdePath = QStringLiteral("/Layouts");
const QString kKdeInterface = QStringLiteral("org.kde.KeyboardLayouts");
}

bool KdeSource::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QDBusMessage call = QDBusMessage::createMethodCall(kKdeService, kKdePath, kKdeInterface, QStringLiteral("getLayout"));
    const QDBusReply<uint> reply = bus.call(call, QDBus::Block, 1000);
    if (!reply.isValid())
        return false;
    readList();
    setGroup(int(reply.value()));
    bus.connect(kKdeService, kKdePath, kKdeInterface, QStringLiteral("layoutChanged"), this, SLOT(layoutChanged(uint)));
    bus.connect(kKdeService, kKdePath, kKdeInterface, QStringLiteral("layoutListChanged"), this, SLOT(layoutListChanged()));
    return keymap() != nullptr;
}

void KdeSource::readList()
{
    // The layouts of the KDE settings; without them KDE uses the system's.
    Desktop::XkbNames names;
    const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kxkbrc"));
    QFile f(path);
    if (!path.isEmpty() && f.open(QIODevice::ReadOnly))
        names = Desktop::parseKxkbrc(QString::fromUtf8(f.readAll()));
    setNames(names.isEmpty() ? systemNames() : names);
}

void KdeSource::layoutChanged(uint index)
{
    setGroup(int(index));
}

void KdeSource::layoutListChanged()
{
    readList();
    const QDBusMessage call = QDBusMessage::createMethodCall(kKdeService, kKdePath, kKdeInterface, QStringLiteral("getLayout"));
    const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
    if (reply.isValid())
        setGroup(int(reply.value()));
}

#endif

// --- GNOME ---

GnomeSource::~GnomeSource()
{
    if (m_monitor) {
        m_monitor->kill();
        m_monitor->waitForFinished(1000);
    }
}

QString GnomeSource::get(const char *key)
{
    QProcess p;
    p.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.input-sources"), QLatin1String(key)});
    if (!p.waitForFinished(2000) || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool GnomeSource::start()
{
    const QString sources = get("sources");
    if (sources.isEmpty())
        return false;
    for (const auto &s : Desktop::parseGnomeSources(sources))
        if (s.first == QLatin1String("xkb"))
            m_sources << s;
    m_options = Desktop::parseGVariantStrings(get("xkb-options")).join(u',');
    const auto mru = Desktop::parseGnomeSources(get("mru-sources"));
    m_current = mru.isEmpty() ? (m_sources.isEmpty() ? std::pair<QString, QString>{} : m_sources.first()) : mru.first();
    apply();

    m_monitor = new QProcess(this);
    connect(m_monitor, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_monitor->canReadLine()) {
            const auto line = Desktop::parseGsettingsMonitorLine(QString::fromUtf8(m_monitor->readLine()));
            if (!line)
                continue;
            if (line->first == QLatin1String("sources")) {
                m_sources.clear();
                for (const auto &s : Desktop::parseGnomeSources(line->second))
                    if (s.first == QLatin1String("xkb"))
                        m_sources << s;
            } else if (line->first == QLatin1String("xkb-options")) {
                m_options = Desktop::parseGVariantStrings(line->second).join(u',');
            } else if (line->first == QLatin1String("mru-sources")) {
                const auto mru = Desktop::parseGnomeSources(line->second);
                if (!mru.isEmpty())
                    m_current = mru.first();
            } else {
                continue;
            }
            apply();
        }
    });
    m_monitor->start(QStringLiteral("gsettings"), {QStringLiteral("monitor"), QStringLiteral("org.gnome.desktop.input-sources")});
    return keymap() != nullptr;
}

void GnomeSource::apply()
{
    QStringList ids;
    for (const auto &s : m_sources)
        ids << s.second;
    setNames(ids.isEmpty() ? systemNames() : Desktop::namesFromIds(ids, m_options));
    // An input method (ibus) on: its characters do not come from the keymap; the first layout is used.
    setGroup(std::max<int>(0, int(m_sources.indexOf(m_current))));
}
