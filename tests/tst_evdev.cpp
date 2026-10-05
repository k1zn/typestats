// The Linux hook (src/platform/linux): evdev events → hook events with the flags and characters of the
// Windows hook. Only synthetic events: keymaps from XKB names (xkb-data), devices as FIFOs with a
// fake sysfs; nothing is sent to the system.

#include "core/TsfFile.h"
#include "platform/linux/EvdevReader.h"
#include "platform/linux/XkbKeyboard.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>

namespace {

enum : int { Esc = 1, Key1 = 2, Key6 = 7, Minus = 12, Backspace = 14, Tab = 15, KeyQ = 16, KeyE = 18, LeftBrace = 26,
             Enter = 28, LeftCtrl = 29, KeyA = 30, Apostrophe = 40, LeftShift = 42, KeyX = 45, Space = 57,
             CapsLock = 58, NumLock = 69, Kp1 = 79, KpDot = 83, RightCtrl = 97, RightAlt = 100, Up = 103, Delete = 111,
             Pause = 119, LeftMeta = 125 };

Desktop::XkbNames names(const char *layout, const char *variant = "")
{
    Desktop::XkbNames n;
    n.layout = QLatin1String(layout);
    n.variant = QLatin1String(variant);
    return n;
}

struct Typist
{
    XkbKeyboard &k;
    qint64 time = 0;
    HookEvent press(int code) { return *k.event(code, 1, time += 1000, false); }
    HookEvent release(int code) { return *k.event(code, 0, time += 1000, false); }
    HookEvent tap(int code)
    {
        const HookEvent e = press(code);
        release(code);
        return e;
    }
};

quint32 vkOf(const HookEvent &e)
{
    return (e.flags & KeyRecord::VkMask) >> 16;
}

} // namespace

class TstEvdev : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!XkbKeyboard::sharedContext())
            QSKIP("no xkbcommon context");
        if (xkb_keymap *k = XkbKeyboard::compile(names("us")))
            xkb_keymap_unref(k);
        else
            QSKIP("no XKB data (xkb-data)");
    }

    void letters()
    {
        XkbKeyboard k;
        QVERIFY(k.setKeymap(names("us,ru")));
        k.setGroup(0);
        Typist t{k};
        HookEvent a = t.press(KeyA);
        QCOMPARE(a.flags, quint32(0x1E) | 'A' << 16 | KeyRecord::HasChar);
        QCOMPARE(a.ch, u'a');
        QCOMPARE(a.chars, 1);
        QCOMPARE(a.timeUs, qint64(1000));
        const HookEvent up = t.release(KeyA);
        QCOMPARE(up.flags, quint32(0x1E) | 'A' << 16 | KeyRecord::KeyUp | KeyRecord::NoChar);
        QCOMPARE(up.chars, 0);

        // Shift: its press has no Shift bit, its release has it (as the Windows hook, golden files).
        const HookEvent shift = t.press(LeftShift);
        QCOMPARE(shift.flags, quint32(0x2A) | 0xA0 << 16 | KeyRecord::NoChar);
        a = t.tap(KeyA);
        QCOMPARE(a.ch, u'A');
        QVERIFY(a.flags & KeyRecord::Shift);
        QCOMPARE(t.release(LeftShift).flags, quint32(0x2A) | 0xA0 << 16 | KeyRecord::KeyUp | KeyRecord::NoChar | KeyRecord::Shift);

        // The layout the desktop has on.
        k.setGroup(1);
        QCOMPARE(t.tap(KeyA).ch, u'ф');
        QCOMPARE(vkOf(t.tap(KeyA)), quint32('A'));
        k.setGroup(0);
        QCOMPARE(t.tap(KeyA).ch, u'a');

        // Caps Lock: toggled by its key, and as the LEDs say.
        t.tap(CapsLock);
        QVERIFY(k.capsLock());
        QCOMPARE(t.tap(KeyA).ch, u'A');
        t.tap(CapsLock);
        QCOMPARE(t.tap(KeyA).ch, u'a');
        k.setLocks(true, false);
        QCOMPARE(t.tap(KeyA).ch, u'A');
        k.setLocks(false, false);
    }

    void controlCharacters()
    {
        XkbKeyboard k;
        QVERIFY(k.setKeymap(names("us,ru")));
        Typist t{k};
        QCOMPARE(t.tap(Backspace).ch, char16_t(0x08));
        QCOMPARE(t.tap(Tab).ch, char16_t(0x09));
        QCOMPARE(t.tap(Enter).ch, char16_t(0x0D));
        QCOMPARE(t.tap(Esc).ch, char16_t(0x1B));
        QCOMPARE(t.tap(Space).ch, u' ');
        // Delete: no character (xkb has DEL), extended.
        const HookEvent del = t.tap(Delete);
        QCOMPARE(del.chars, 0);
        QCOMPARE(del.flags, quint32(0x53) | 0x2E << 16 | KeyRecord::Extended | KeyRecord::NoChar);

        // With Ctrl: what ToUnicodeEx gives (Ctrl+BS is DEL, Ctrl+Enter LF, letters 01-1A in any layout).
        t.press(LeftCtrl);
        HookEvent e = t.tap(Backspace);
        QCOMPARE(e.ch, char16_t(0x7F));
        QVERIFY(e.flags & KeyRecord::Ctrl);
        QCOMPARE(t.tap(Enter).ch, char16_t(0x0A));
        QCOMPARE(t.tap(KeyA).ch, char16_t(0x01));
        QCOMPARE(t.tap(LeftBrace).ch, char16_t(0x1B));
        QCOMPARE(t.tap(Key1).chars, 0);
        QCOMPARE(t.tap(Space).ch, u' ');
        k.setGroup(1);
        QCOMPARE(t.tap(KeyA).ch, char16_t(0x01));
        k.setGroup(0);
        t.press(LeftShift);
        QCOMPARE(t.tap(Key6).ch, char16_t(0x1E));
        QCOMPARE(t.tap(Minus).ch, char16_t(0x1F));
        t.release(LeftShift);
        // Ctrl+Alt is AltGr for Windows: the US layout has nothing there.
        t.press(56);
        QCOMPARE(t.tap(KeyA).chars, 0);
        t.release(56);
        t.release(LeftCtrl);
        // Alt alone does not change the character (Alt+Tab in the golden files: 09).
        t.press(56);
        e = t.tap(Tab);
        QCOMPARE(e.ch, char16_t(0x09));
        QVERIFY(e.flags & KeyRecord::Alt);
        t.release(56);
        // Win.
        t.press(LeftMeta);
        e = t.tap(KeyA);
        QVERIFY(e.flags & KeyRecord::Win);
        QCOMPARE(e.ch, u'a');
        t.release(LeftMeta);
    }

    void extendedAndKeypad()
    {
        XkbKeyboard k;
        QVERIFY(k.setKeymap(names("us")));
        Typist t{k};
        QCOMPARE(t.tap(RightCtrl).flags, quint32(0x1D) | 0xA3 << 16 | KeyRecord::Extended | KeyRecord::NoChar);
        QCOMPARE(t.tap(RightAlt).flags, quint32(0x38) | 0xA5 << 16 | KeyRecord::Extended | KeyRecord::NoChar);
        QCOMPARE(t.tap(Up).flags, quint32(0x48) | 0x26 << 16 | KeyRecord::Extended | KeyRecord::NoChar);
        QCOMPARE(t.tap(LeftMeta).flags, quint32(0x5B) | 0x5B << 16 | KeyRecord::Extended | KeyRecord::NoChar);
        QCOMPARE(t.tap(Pause).flags, quint32(0x45) | 0x13 << 16 | KeyRecord::NoChar);
        // The keypad without NumLock: End, no character, not extended.
        QCOMPARE(t.tap(Kp1).flags, quint32(0x4F) | 0x23 << 16 | KeyRecord::NoChar);
        QCOMPARE(t.tap(KpDot).flags, quint32(0x53) | 0x2E << 16 | KeyRecord::NoChar);
        // NumLock: extended 0x45, then digits.
        QCOMPARE(t.tap(NumLock).flags, quint32(0x45) | 0x90 << 16 | KeyRecord::Extended | KeyRecord::NoChar);
        QVERIFY(k.numLock());
        HookEvent one = t.tap(Kp1);
        QCOMPARE(one.flags, quint32(0x4F) | 0x61 << 16 | KeyRecord::HasChar);
        QCOMPARE(one.ch, u'1');
        QCOMPARE(t.tap(KpDot).ch, u'.');
        // Autorepeat: a press again, the state unchanged.
        t.press(KeyA);
        const HookEvent repeat = *k.event(KeyA, 2, 99, false);
        QCOMPARE(repeat.ch, u'a');
        QVERIFY(!(repeat.flags & KeyRecord::KeyUp));
        t.release(KeyA);
        // Injected (a virtual device) and a key the Windows hook has no code for.
        QVERIFY(k.event(KeyA, 1, 0, true)->flags & KeyRecord::Injected);
        k.event(KeyA, 0, 0, true);
        QVERIFY(!k.event(464, 1, 0, false)); // KEY_FN
        k.event(464, 0, 0, false);
    }

    void deadKeys()
    {
        // us(intl): the apostrophe is a dead acute.
        XkbKeyboard k;
        QVERIFY(k.setKeymap(names("us", "intl")));
        Typist t{k};
        HookEvent dead = t.tap(Apostrophe);
        if (dead.chars == 1 && dead.ch == u'\'')
            QSKIP("no Compose data for the locale (libx11-data)");
        QCOMPARE(dead.chars, -1);
        QVERIFY(dead.flags & KeyRecord::DeadKey);
        QVERIFY(dead.flags & KeyRecord::HasChar);
        QCOMPARE(dead.ch, u'\''); // what the layout types for it with a space
        const HookEvent e = t.tap(KeyE);
        QCOMPARE(e.chars, 1);
        QCOMPARE(e.ch, u'é');
        // No combination: the dead key's character, then the key's - two characters, as Windows.
        t.tap(Apostrophe);
        const HookEvent x = t.tap(KeyX);
        QCOMPARE(x.chars, 2);
        QCOMPARE(x.firstCh, u'\'');
        QCOMPARE(x.ch, u'x');
        QCOMPARE(t.tap(KeyX).chars, 1);
        // The helpers: a dead key waits for the next call, as ToUnicodeEx.
        char16_t out[2];
        QCOMPARE(k.toUnicode({0x28, false}, false, false, out), -1);
        QCOMPARE(k.toUnicode({0x12, false}, false, false, out), 1);
        QCOMPARE(out[0], u'é');
        bool isDead = false;
        k.keyName(0x28, &isDead);
        QVERIFY(isDead);
    }

    void helpers()
    {
        XkbKeyboard k;
        QVERIFY(k.setKeymap(names("us,ru")));
        k.setGroup(1);
        char16_t out[2] = {};
        QCOMPARE(k.toUnicode({0x1E, false}, false, false, out), 1);
        QCOMPARE(out[0], u'ф');
        QCOMPARE(k.toUnicode({0x1E, false}, true, false, out), 1);
        QCOMPARE(out[0], u'Ф');
        QCOMPARE(k.toUnicode({0x1E, false}, false, true, out), 1);
        QCOMPARE(out[0], u'Ф');
        QCOMPARE(k.keyName(0x1E, nullptr), QStringLiteral("ф"));
        QCOMPARE(k.keyName(0x0E, nullptr), QStringLiteral("[BackSpace]"));
        QCOMPARE(k.groupOfName(QStringLiteral("Russian")), 1);
        QCOMPARE(k.groupOfName(QStringLiteral("English (US)")), 0);
        QCOMPARE(k.groupOfName(QStringLiteral("Klingon")), -1);
    }

    void goldenRecordings()
    {
        // The recordings made by the original on Windows, fed back key by key: the same flags and characters.
        XkbKeyboard k, probe;
        QVERIFY(k.setKeymap(names("us,ru")));
        QVERIFY(probe.setKeymap(names("us,ru")));
        const quint32 compared = ~quint32(KeyRecord::SegmentStart | KeyRecord::Transient | KeyRecord::Marked | KeyRecord::SingleChar);
        int checked = 0, group = 0;
        bool capsHeld = false;
        for (const QString &file : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")})) {
            TsfDocument doc;
            QCOMPARE(Tsf::read(QStringLiteral(TS_GOLDEN_DIR "/") + file, doc), Tsf::ReadError::None);
            group = 0;
            for (const KeyRecord &r : doc.records) {
                if (r.flags & KeyRecord::Packet)
                    continue;
                const int code = Keyboard::scanToEvdev({r.scan(), bool(r.flags & KeyRecord::Extended)});
                QVERIFY2(code >= 0, qPrintable(QStringLiteral("%1: scan %2").arg(file).arg(r.scan(), 0, 16)));
                // The layout the key had this character in (the one before, when both have it).
                if (r.isDown() && r.hasChar()) {
                    for (int g : {group, 1 - group}) {
                        probe.setGroup(g);
                        char16_t out[2];
                        if (probe.toUnicode({r.scan(), bool(r.flags & KeyRecord::Extended)}, r.flags & KeyRecord::Shift, false, out) == 1
                            && QChar(out[0]).toLower() == QChar(r.ch).toLower()) {
                            group = g;
                            break;
                        }
                    }
                }
                k.setGroup(group);
                const auto e = k.event(code, r.isDown() ? 1 : 0, 0, r.flags & KeyRecord::Injected);
                QVERIFY(e);
                const QString where = QStringLiteral("%1, record %2").arg(file).arg(&r - doc.records.data());
                QVERIFY2((e->flags & compared) == (r.flags & compared),
                         qPrintable(QStringLiteral("%1: %2 instead of %3").arg(where).arg(e->flags, 8, 16).arg(r.flags, 8, 16)));
                // A key pressed while Caps Lock is held: XKB unlocks on its release, Windows has unlocked on the
                // press (the original wrote "о" in "Согласно" with Caps Lock still down; Linux programs get "О").
                if (r.vk() == 0x14)
                    capsHeld = r.isDown();
                if (r.hasChar() && capsHeld)
                    QVERIFY2(QChar(e->ch).toLower() == QChar(r.ch).toLower(), qPrintable(where));
                else if (r.hasChar())
                    QVERIFY2(e->ch == r.ch, qPrintable(where + QStringLiteral(": ") + QChar(e->ch) + QStringLiteral(" instead of ") + QChar(r.ch)));
                ++checked;
            }
        }
        QVERIFY(checked > 1000);
    }

    void reader()
    {
        // Keyboards by their sysfs capabilities; the device files are FIFOs the test writes events into.
        QTemporaryDir dir;
        const QString sys = dir.filePath(QStringLiteral("sys")), dev = dir.filePath(QStringLiteral("dev"));
        QDir().mkpath(dev);
        const QString keyboardKeys = QStringLiteral("1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe");
        auto device = [&](const QString &name, const QString &ev, const QString &keys, const QString &bus) {
            const QString caps = sys + u'/' + name + QStringLiteral("/device/capabilities");
            QDir().mkpath(caps);
            QDir().mkpath(sys + u'/' + name + QStringLiteral("/device/id"));
            auto write = [](const QString &path, const QString &text) {
                QFile f(path);
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(text.toLatin1() + '\n');
            };
            write(caps + QStringLiteral("/ev"), ev);
            write(caps + QStringLiteral("/key"), keys);
            write(sys + u'/' + name + QStringLiteral("/device/id/bustype"), bus);
            const QByteArray path = QFile::encodeName(dev + u'/' + name);
            if (mkfifo(path.constData(), 0600) != 0)
                return -1;
            return ::open(path.constData(), O_RDWR | O_NONBLOCK); // the writing end, kept open
        };
        auto send = [](int fd, int type, int code, int value, qint64 us) {
            input_event e = {};
            e.input_event_sec = us / 1000000;
            e.input_event_usec = us % 1000000;
            e.type = quint16(type);
            e.code = quint16(code);
            e.value = value;
            QCOMPARE(::write(fd, &e, sizeof(e)), ssize_t(sizeof(e)));
        };
        QVERIFY(EvdevReader::isKeyboard(QStringLiteral("120013"), keyboardKeys));
        QVERIFY(!EvdevReader::isKeyboard(QStringLiteral("17"), QStringLiteral("70000 0 0 0 0")));      // a mouse
        QVERIFY(!EvdevReader::isKeyboard(QStringLiteral("3"), QStringLiteral("10000000000000 0")));    // the power button

        const int keyboard = device(QStringLiteral("event3"), QStringLiteral("120013"), keyboardKeys, QStringLiteral("0011"));
        const int mouse = device(QStringLiteral("event5"), QStringLiteral("17"), QStringLiteral("70000 0 0 0 0"), QStringLiteral("0003"));
        QVERIFY(keyboard >= 0 && mouse >= 0);

        std::mutex mutex;
        QList<HookEvent> events;
        bool opened = false;
        EvdevReader reader([&](const HookEvent &e) { const std::lock_guard l(mutex); events << e; },
                           [&] { const std::lock_guard l(mutex); opened = true; }, sys, dev);
        xkb_keymap *us = XkbKeyboard::compile(names("us"));
        reader.setKeymap(us);
        xkb_keymap_unref(us);
        EvdevReader::Status status;
        QVERIFY(reader.start(&status));
        QCOMPARE(status.keyboards, 1);
        QCOMPARE(status.open, 1);

        send(keyboard, EV_MSC, 4, 30, 1000000);
        send(keyboard, EV_KEY, KeyA, 1, 1000000);
        send(keyboard, EV_SYN, SYN_REPORT, 0, 1000000);
        send(keyboard, EV_KEY, KeyA, 0, 1080000);
        send(keyboard, EV_SYN, SYN_REPORT, 0, 1080000);
        // Lost events: up to the next report they are not used.
        send(keyboard, EV_SYN, SYN_DROPPED, 0, 1100000);
        send(keyboard, EV_KEY, KeyQ, 1, 1100000);
        send(keyboard, EV_SYN, SYN_REPORT, 0, 1100000);
        send(keyboard, EV_LED, LED_CAPSL, 1, 1150000); // the desktop lit Caps Lock
        send(keyboard, EV_KEY, KeyA, 1, 1200000);
        QTRY_COMPARE(([&] { const std::lock_guard l(mutex); return events.size(); }()), 3);
        {
            const std::lock_guard l(mutex);
            QCOMPARE(events[0].ch, u'a');
            QCOMPARE(events[0].timeUs, qint64(1000000));
            QVERIFY(!(events[0].flags & KeyRecord::Injected));
            QVERIFY(events[1].flags & KeyRecord::KeyUp);
            QCOMPARE(events[1].timeUs, qint64(1080000));
            QCOMPARE(events[2].ch, u'A');
        }
        QVERIFY(reader.capsLock());

        // A virtual keyboard plugged in: its keys are injected.
        QVERIFY(!opened); // one was open from the start
        const int virtualKeyboard = device(QStringLiteral("event7"), QStringLiteral("120013"), keyboardKeys, QStringLiteral("0006"));
        QTest::qWait(200);
        send(virtualKeyboard, EV_KEY, KeyX, 1, 2000000);
        QTRY_COMPARE(([&] { const std::lock_guard l(mutex); return events.size(); }()), 4);
        {
            const std::lock_guard l(mutex);
            QVERIFY(events[3].flags & KeyRecord::Injected);
        }
        reader.stop();
        for (int fd : {keyboard, mouse, virtualKeyboard})
            ::close(fd);
    }

    void readerWaitsForKeyboards()
    {
        // No keyboard at the start: the reader waits and says when the first one opens.
        QTemporaryDir dir;
        const QString sys = dir.filePath(QStringLiteral("sys")), dev = dir.filePath(QStringLiteral("dev"));
        QDir().mkpath(dev);
        QDir().mkpath(sys);
        std::atomic<bool> opened = false;
        EvdevReader reader([](const HookEvent &) {}, [&] { opened = true; }, sys, dev);
        EvdevReader::Status status;
        QVERIFY(reader.start(&status));
        QCOMPARE(status.keyboards, 0);
        const QString caps = sys + QStringLiteral("/event1/device/capabilities");
        QDir().mkpath(caps);
        for (const auto &[file, text] : {std::pair{"ev", "120013"}, std::pair{"key", "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe"}}) {
            QFile f(caps + u'/' + QLatin1String(file));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        }
        const QByteArray path = QFile::encodeName(dev + QStringLiteral("/event1"));
        QCOMPARE(mkfifo(path.constData(), 0600), 0);
        const int writer = ::open(path.constData(), O_RDWR | O_NONBLOCK);
        QTRY_VERIFY(opened.load());
        reader.stop();
        ::close(writer);
    }
};

QTEST_GUILESS_MAIN(TstEvdev)
#include "tst_evdev.moc"
