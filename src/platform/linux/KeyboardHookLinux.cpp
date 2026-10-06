#include "platform/KeyboardHook.h"

#include "EvdevReader.h"
#include "LayoutSource.h"
#include "XkbKeyboard.h"

#include <QCoreApplication>

namespace {

// The desktop's side, in the GUI thread: where the layouts and the active window come from, and a
// keyboard of the current layout for the static helpers (key names, ToUnicode). Made when the hook
// first starts; before that the helpers use the US layout.
class LinuxDesktop : public QObject
{
public:
    static LinuxDesktop *instance() { return s_instance; }
    static LinuxDesktop *create()
    {
        if (!s_instance)
            s_instance = new LinuxDesktop(QCoreApplication::instance());
        return s_instance;
    }

    LayoutSource *source = nullptr;
    XkbKeyboard keyboard;
    EvdevReader *reader = nullptr; // the running hook's, for Caps Lock

private:
    explicit LinuxDesktop(QObject *parent) : QObject(parent)
    {
        source = LayoutSource::create(this);
        if (source) {
            qInfo("Typing statistics: keyboard layouts from \"%s\"", qPrintable(source->name()));
            keyboard.setKeymap(source->keymap());
            keyboard.setGroup(source->group());
            connect(source, &LayoutSource::keymapChanged, this, [this] { keyboard.setKeymap(source->keymap()); });
            connect(source, &LayoutSource::groupChanged, this, [this](int group) { keyboard.setGroup(group); });
        }
    }
    ~LinuxDesktop() override { s_instance = nullptr; }

    static inline LinuxDesktop *s_instance = nullptr;
};

} // namespace

struct KeyboardHook::Impl
{
    std::unique_ptr<EvdevReader> reader;
    std::unique_ptr<QObject> connections; // to the layout source, while the reader lives
};

KeyboardHook::KeyboardHook(QObject *parent) : QObject(parent), m_impl(std::make_unique<Impl>())
{
    qRegisterMetaType<HookEvent>();
}

KeyboardHook::~KeyboardHook()
{
    stop();
}

bool KeyboardHook::start()
{
    if (m_impl->reader)
        return m_running;
    LinuxDesktop *desktop = LinuxDesktop::create();
    // Events are made in the reader's thread and delivered here in order.
    m_impl->reader = std::make_unique<EvdevReader>(
        [this](const HookEvent &e) { QMetaObject::invokeMethod(this, [this, e] { emit key(e); }, Qt::QueuedConnection); },
        [this] {
            QMetaObject::invokeMethod(this, [this] {
                if (!m_running) {
                    m_running = true;
                    emit started();
                }
            }, Qt::QueuedConnection);
        });
    EvdevReader *reader = m_impl->reader.get();
    if (LayoutSource *source = desktop->source) {
        reader->setKeymap(source->keymap());
        reader->setGroup(source->group());
        m_impl->connections = std::make_unique<QObject>();
        connect(source, &LayoutSource::keymapChanged, m_impl->connections.get(), [reader, source] { reader->setKeymap(source->keymap()); });
        connect(source, &LayoutSource::groupChanged, m_impl->connections.get(), [reader](int group) { reader->setGroup(group); });
    }
    desktop->reader = reader;

    EvdevReader::Status status;
    if (!reader->start(&status)) {
        stop();
        emit failed(tr("Не удалось начать запись нажатий. Перезапустите программу."));
        return false;
    }
    if (status.open > 0) {
        m_running = true;
        emit started();
        return true;
    }
    // The reader waits: a keyboard may come, or the access may be granted (then started()).
    if (status.keyboards == 0)
        emit failed(tr("Клавиатура не найдена. Запись начнётся, как только вы её подключите."));
    else
        emit failed(tr("Программе нужно разрешение видеть нажатия клавиш.\n\n"
                       "Откройте терминал, вставьте туда эти две строки и введите свой пароль:\n\n"
                       "echo 'SUBSYSTEM==\"input\", KERNEL==\"event*\", ENV{ID_INPUT_KEYBOARD}==\"1\", TAG+=\"uaccess\"' "
                       "| sudo tee /etc/udev/rules.d/70-typingstatistics.rules\n"
                       "sudo udevadm control --reload && sudo udevadm trigger\n\n"
                       "Это нужно один раз. Запись начнётся сама."));
    return false;
}

void KeyboardHook::stop()
{
    if (LinuxDesktop *desktop = LinuxDesktop::instance(); desktop && desktop->reader == m_impl->reader.get())
        desktop->reader = nullptr;
    m_impl->connections.reset();
    m_impl->reader.reset(); // joins the thread
    m_running = false;
}

quint64 KeyboardHook::foregroundWindow()
{
    const LinuxDesktop *desktop = LinuxDesktop::instance();
    return desktop && desktop->source ? desktop->source->window().id : 0;
}

QString KeyboardHook::windowTitle(quint64)
{
    return foregroundTitle();
}

QString KeyboardHook::foregroundTitle()
{
    LinuxDesktop *desktop = LinuxDesktop::instance();
    return desktop && desktop->source ? desktop->source->windowTitle() : QString();
}

QString KeyboardHook::layoutKeyName(quint8 scan, bool *dead)
{
    if (LinuxDesktop *desktop = LinuxDesktop::instance(); desktop && desktop->keyboard.keymap())
        return desktop->keyboard.keyName(scan, dead);
    if (dead)
        *dead = false;
    return UsLayout::keyName(scan);
}

int KeyboardHook::toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2])
{
    if (LinuxDesktop *desktop = LinuxDesktop::instance(); desktop && desktop->keyboard.keymap())
        return desktop->keyboard.toUnicode({scan, false}, shift, caps, out);
    return UsLayout::toUnicode(scan, shift, caps, out);
}

void KeyboardHook::clearDeadKey()
{
    if (LinuxDesktop *desktop = LinuxDesktop::instance())
        desktop->keyboard.clearDeadKey();
}

bool KeyboardHook::capsLock()
{
    const LinuxDesktop *desktop = LinuxDesktop::instance();
    return desktop && desktop->reader && desktop->reader->capsLock();
}
