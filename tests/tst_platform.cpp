// Key code tables of the hooks (core/Keyboard), what the Linux desktops say (platform/DesktopParsers) and the
// association of .tsf (platform/FileAssociation: in a registry key / directories of the test's own).
// Runs on every system: nothing here talks to a desktop.

#include "core/Keyboard.h"
#include "platform/DesktopParsers.h"
#include "platform/FileAssociation.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using Keyboard::ScanCode;

class TstPlatform : public QObject
{
    Q_OBJECT
private slots:
    void scanToVk()
    {
        // As the Windows hook reports them (re/tsf_format.md and the golden recordings).
        QCOMPARE(Keyboard::scanToVk({0x1E, false}, false), quint8('A'));
        QCOMPARE(Keyboard::scanToVk({0x02, false}, false), quint8('1'));
        QCOMPARE(Keyboard::scanToVk({0x39, false}, false), quint8(0x20));
        QCOMPARE(Keyboard::scanToVk({0x2A, false}, false), quint8(0xA0)); // LShift
        QCOMPARE(Keyboard::scanToVk({0x36, false}, false), quint8(0xA1)); // RShift
        QCOMPARE(Keyboard::scanToVk({0x1D, false}, false), quint8(0xA2)); // LCtrl
        QCOMPARE(Keyboard::scanToVk({0x1D, true}, false), quint8(0xA3));  // RCtrl
        QCOMPARE(Keyboard::scanToVk({0x38, false}, false), quint8(0xA4)); // LAlt
        QCOMPARE(Keyboard::scanToVk({0x38, true}, false), quint8(0xA5));  // RAlt
        QCOMPARE(Keyboard::scanToVk({0x5B, true}, false), quint8(0x5B));  // LWin
        QCOMPARE(Keyboard::scanToVk({0x4D, true}, false), quint8(0x27));  // Right
        QCOMPARE(Keyboard::scanToVk({0x53, true}, false), quint8(0x2E));  // Delete
        QCOMPARE(Keyboard::scanToVk({0x1C, true}, false), quint8(0x0D));  // keypad Enter
        QCOMPARE(Keyboard::scanToVk({0x35, true}, false), quint8(0x6F));  // keypad /
        // The keypad by NumLock; its non-digit keys either way.
        QCOMPARE(Keyboard::scanToVk({0x4F, false}, true), quint8(0x61));
        QCOMPARE(Keyboard::scanToVk({0x4F, false}, false), quint8(0x23)); // End
        QCOMPARE(Keyboard::scanToVk({0x53, false}, true), quint8(0x6E));
        QCOMPARE(Keyboard::scanToVk({0x53, false}, false), quint8(0x2E));
        QCOMPARE(Keyboard::scanToVk({0x4C, false}, false), quint8(0x0C)); // Clear
        QCOMPARE(Keyboard::scanToVk({0x4A, false}, false), quint8(0x6D));
        QCOMPARE(Keyboard::scanToVk({0x4E, false}, true), quint8(0x6B));
        // Pause and NumLock share 0x45.
        QCOMPARE(Keyboard::scanToVk({0x45, false}, false), quint8(0x13));
        QCOMPARE(Keyboard::scanToVk({0x45, true}, false), quint8(0x90));
        QCOMPARE(Keyboard::scanToVk({0x64, false}, false), quint8(0x7C)); // F13
        QCOMPARE(Keyboard::scanToVk({0x7F, false}, false), quint8(0));
        // Agrees with the US table the reader of version-0 files uses.
        for (int code = 1; code < 0x59; ++code) {
            const quint8 vk = Keyboard::scanToVk({quint8(code), false}, true);
            if (vk && code != 0x45 && code != 0x37 && code != 0x4C)
                QCOMPARE(Keyboard::vkToScan(vk), quint8(code));
        }
    }

    void evdevCodes()
    {
        QCOMPARE(Keyboard::evdevToScan(30), (ScanCode{0x1E, false}));  // KEY_A
        QCOMPARE(Keyboard::evdevToScan(1), (ScanCode{0x01, false}));   // KEY_ESC
        QCOMPARE(Keyboard::evdevToScan(83), (ScanCode{0x53, false}));  // KEY_KPDOT
        QCOMPARE(Keyboard::evdevToScan(69), (ScanCode{0x45, true}));   // KEY_NUMLOCK
        QCOMPARE(Keyboard::evdevToScan(119), (ScanCode{0x45, false})); // KEY_PAUSE
        QCOMPARE(Keyboard::evdevToScan(96), (ScanCode{0x1C, true}));   // KEY_KPENTER
        QCOMPARE(Keyboard::evdevToScan(97), (ScanCode{0x1D, true}));   // KEY_RIGHTCTRL
        QCOMPARE(Keyboard::evdevToScan(100), (ScanCode{0x38, true}));  // KEY_RIGHTALT
        QCOMPARE(Keyboard::evdevToScan(111), (ScanCode{0x53, true}));  // KEY_DELETE
        QCOMPARE(Keyboard::evdevToScan(125), (ScanCode{0x5B, true}));  // KEY_LEFTMETA
        QCOMPARE(Keyboard::evdevToScan(86), (ScanCode{0x56, false}));  // KEY_102ND
        QCOMPARE(Keyboard::evdevToScan(183), (ScanCode{0x64, false})); // KEY_F13
        QVERIFY(!Keyboard::evdevToScan(464));                          // KEY_FN
        QVERIFY(!Keyboard::evdevToScan(0));
        // Both ways.
        for (int code = 0; code < 256; ++code)
            if (const auto scan = Keyboard::evdevToScan(code))
                QCOMPARE(Keyboard::scanToEvdev(*scan), code);
        QCOMPARE(Keyboard::scanToEvdev({0x7F, true}), -1);
    }

    void macCodes()
    {
        QCOMPARE(Keyboard::macToScan(0x00), (ScanCode{0x1E, false}));  // kVK_ANSI_A
        QCOMPARE(Keyboard::macToScan(0x37), (ScanCode{0x5B, true}));   // kVK_Command: LWin
        QCOMPARE(Keyboard::macToScan(0x36), (ScanCode{0x5C, true}));   // kVK_RightCommand
        QCOMPARE(Keyboard::macToScan(0x3A), (ScanCode{0x38, false}));  // kVK_Option: LAlt
        QCOMPARE(Keyboard::macToScan(0x3D), (ScanCode{0x38, true}));   // kVK_RightOption
        QCOMPARE(Keyboard::macToScan(0x3B), (ScanCode{0x1D, false}));  // kVK_Control
        QCOMPARE(Keyboard::macToScan(0x33), (ScanCode{0x0E, false}));  // kVK_Delete: BackSpace
        QCOMPARE(Keyboard::macToScan(0x75), (ScanCode{0x53, true}));   // kVK_ForwardDelete
        QCOMPARE(Keyboard::macToScan(0x24), (ScanCode{0x1C, false}));  // kVK_Return
        QCOMPARE(Keyboard::macToScan(0x4C), (ScanCode{0x1C, true}));   // kVK_ANSI_KeypadEnter
        QCOMPARE(Keyboard::macToScan(0x32), (ScanCode{0x29, false}));  // kVK_ANSI_Grave
        QCOMPARE(Keyboard::macToScan(0x0A), (ScanCode{0x56, false}));  // kVK_ISO_Section
        QCOMPARE(Keyboard::macToScan(0x7E), (ScanCode{0x48, true}));   // kVK_UpArrow
        QVERIFY(!Keyboard::macToScan(0x3F));                           // kVK_Function
        // Both ways, and every key of the US main block has its Mac key.
        for (int code = 0; code < 128; ++code)
            if (const auto scan = Keyboard::macToScan(code))
                QCOMPARE(Keyboard::scanToMac(*scan), code);
        for (int scan = 0x01; scan <= 0x39; ++scan)
            if (scan != 0x37) // keypad *, elsewhere on a Mac
                QVERIFY2(Keyboard::scanToMac({quint8(scan), false}) >= 0, qPrintable(QString::number(scan, 16)));
    }

    void names()
    {
        const Desktop::XkbNames n = Desktop::namesFromIds({QStringLiteral("us"), QStringLiteral("ru+phonetic")}, QStringLiteral("grp:alt_shift_toggle"));
        QCOMPARE(n.layout, QStringLiteral("us,ru"));
        QCOMPARE(n.variant, QStringLiteral(",phonetic"));
        QCOMPARE(n.layoutCount(), 2);
        QCOMPARE(Desktop::namesFromIds({QStringLiteral("us"), QStringLiteral("ru")}).variant, QString());
        QCOMPARE(Desktop::withoutGroupSwitching(QStringLiteral("grp:alt_shift_toggle,compose:ralt,grp_led:scroll")),
                 QStringLiteral("compose:ralt,grp_led:scroll"));
    }

    void i3Messages()
    {
        QByteArray stream = Desktop::I3::message(Desktop::I3::GetInputs) + Desktop::I3::message(Desktop::I3::InputEvent, "{\"a\":1}");
        stream.append("i3-i"); // the start of a third one
        auto first = Desktop::I3::take(stream);
        QVERIFY(first);
        QCOMPARE(first->first, quint32(Desktop::I3::GetInputs));
        QVERIFY(first->second.isEmpty());
        auto second = Desktop::I3::take(stream);
        QVERIFY(second);
        QCOMPARE(second->first, quint32(Desktop::I3::InputEvent));
        QCOMPARE(second->second, QByteArray("{\"a\":1}"));
        QVERIFY(!Desktop::I3::take(stream));
        QCOMPARE(stream, QByteArray("i3-i"));
    }

    void sway()
    {
        const QByteArray inputs = R"j([
            {"identifier": "1267:12377:ELAN_Touchpad", "type": "touchpad"},
            {"identifier": "1:1:AT_Translated_Set_2_keyboard", "type": "keyboard",
             "xkb_layout_names": ["English (US)", "Russian"], "xkb_active_layout_index": 1},
            {"identifier": "0:1:Power_Button", "type": "keyboard", "xkb_layout_names": [], "xkb_active_layout_index": 0}])j";
        const auto keyboards = Desktop::parseSwayInputs(inputs);
        QCOMPARE(keyboards.size(), 1);
        QCOMPARE(keyboards[0].identifier, QStringLiteral("1:1:AT_Translated_Set_2_keyboard"));
        QCOMPARE(keyboards[0].layoutNames, (QStringList{QStringLiteral("English (US)"), QStringLiteral("Russian")}));
        QCOMPARE(keyboards[0].active, 1);

        const auto event = Desktop::parseSwayInputEvent(R"j({"change": "xkb_layout", "input": {"identifier": "k", "type": "keyboard",
            "xkb_layout_names": ["English (US)", "Russian"], "xkb_active_layout_index": 0}})j");
        QVERIFY(event);
        QCOMPARE(event->active, 0);
        QVERIFY(!Desktop::parseSwayInputEvent(R"j({"change": "libinput_config", "input": {"type": "keyboard"}})j"));

        const auto focus = Desktop::parseSwayWindowEvent(R"j({"change": "focus", "container": {"id": 12, "name": "Terminal", "focused": true}})j");
        QCOMPARE(focus, (Desktop::ActiveWindow{12, QStringLiteral("Terminal")}));
        QVERIFY(Desktop::parseSwayWindowEvent(R"j({"change": "title", "container": {"id": 12, "name": "vim", "focused": true}})j"));
        QVERIFY(!Desktop::parseSwayWindowEvent(R"j({"change": "title", "container": {"id": 13, "name": "x", "focused": false}})j"));
        QVERIFY(!Desktop::parseSwayWindowEvent(R"j({"change": "new", "container": {"id": 14}})j"));

        const QByteArray tree = R"j({"id": 1, "name": "root", "focused": false, "nodes": [
            {"id": 2, "name": "eDP-1", "focused": false, "nodes": [
                {"id": 3, "name": "1", "focused": false, "nodes": [{"id": 7, "name": "Editor", "focused": false, "nodes": []}],
                 "floating_nodes": [{"id": 9, "name": "Firefox", "focused": true, "nodes": []}]}]}]})j";
        QCOMPARE(Desktop::parseSwayTree(tree), (Desktop::ActiveWindow{9, QStringLiteral("Firefox")}));
    }

    void hyprland()
    {
        const QByteArray devices = R"j({"mice": [], "keyboards": [
            {"address": "0x1", "name": "power-button", "rules": "", "model": "", "layout": "us", "variant": "", "options": "",
             "active_keymap": "English (US)", "main": false},
            {"address": "0x2", "name": "at-translated-set-2-keyboard", "rules": "", "model": "pc105", "layout": "us,ru",
             "variant": ",phonetic", "options": "grp:alt_shift_toggle", "active_keymap": "Russian (phonetic)", "main": true}]})j";
        const auto keyboards = Desktop::parseHyprDevices(devices);
        QCOMPARE(keyboards.size(), 2);
        QVERIFY(keyboards[1].main);
        QCOMPARE(keyboards[1].names.layout, QStringLiteral("us,ru"));
        QCOMPARE(keyboards[1].names.variant, QStringLiteral(",phonetic"));
        QCOMPARE(keyboards[1].names.options, QStringLiteral("grp:alt_shift_toggle"));
        QCOMPARE(keyboards[1].names.model, QStringLiteral("pc105"));
        QCOMPARE(keyboards[1].activeKeymap, QStringLiteral("Russian (phonetic)"));

        const auto event = Desktop::parseHyprEvent("activelayout>>at-translated-set-2-keyboard,Russian (phonetic)");
        QVERIFY(event);
        QCOMPARE(event->first, QStringLiteral("activelayout"));
        QCOMPARE(Desktop::splitHyprPair(event->second).second, QStringLiteral("Russian (phonetic)"));
        QCOMPARE(Desktop::splitHyprPair(QStringLiteral("kitty,~/src, the title")).second, QStringLiteral("~/src, the title"));
        QVERIFY(!Desktop::parseHyprEvent("garbage"));
        QCOMPARE(Desktop::parseHyprActiveWindow(R"j({"address": "0x55e0f8a0b2c0", "class": "kitty", "title": "~"})j"),
                 (Desktop::ActiveWindow{0x55e0f8a0b2c0, QStringLiteral("~")}));
        QVERIFY(!Desktop::parseHyprActiveWindow("{}"));
    }

    void gnome()
    {
        const auto sources = Desktop::parseGnomeSources(QStringLiteral("[('xkb', 'us'), ('xkb', 'ru+phonetic'), ('ibus', 'mozc-jp')]"));
        QCOMPARE(sources.size(), 3);
        QCOMPARE(sources[1], (std::pair{QStringLiteral("xkb"), QStringLiteral("ru+phonetic")}));
        QVERIFY(Desktop::parseGnomeSources(QStringLiteral("@a(ss) []")).isEmpty());
        QCOMPARE(Desktop::parseGVariantStrings(QStringLiteral("['grp:alt_shift_toggle', 'compose:ralt']")),
                 (QStringList{QStringLiteral("grp:alt_shift_toggle"), QStringLiteral("compose:ralt")}));
        QVERIFY(Desktop::parseGVariantStrings(QStringLiteral("@as []")).isEmpty());
        const auto line = Desktop::parseGsettingsMonitorLine(QStringLiteral("mru-sources: [('xkb', 'ru'), ('xkb', 'us')]\n"));
        QVERIFY(line);
        QCOMPARE(line->first, QStringLiteral("mru-sources"));
        QCOMPARE(Desktop::parseGnomeSources(line->second).first().second, QStringLiteral("ru"));
    }

    void kdeAndSystem()
    {
        const Desktop::XkbNames kde = Desktop::parseKxkbrc(QStringLiteral(
            "[$Version]\nupdate_info=kxkb.upd:remove-empty-lists\n\n[Layout]\nDisplayNames=,\nLayoutList=us,ru\n"
            "Model=pc104\nOptions=grp:caps_toggle\nResetOldOptions=true\nUse=true\nVariantList=,phonetic\n"));
        QCOMPARE(kde.layout, QStringLiteral("us,ru"));
        QCOMPARE(kde.variant, QStringLiteral(",phonetic"));
        QCOMPARE(kde.options, QStringLiteral("grp:caps_toggle"));
        QCOMPARE(kde.model, QStringLiteral("pc104"));
        QVERIFY(Desktop::parseKxkbrc(QStringLiteral("[Layout]\nLayoutList=us,ru\nUse=false\n")).isEmpty());

        const Desktop::XkbNames debian = Desktop::parseDefaultKeyboard(QStringLiteral(
            "# KEYBOARD CONFIGURATION FILE\n\nXKBMODEL=\"pc105\"\nXKBLAYOUT=\"us,ru\"\nXKBVARIANT=\",\"\n"
            "XKBOPTIONS=\"grp:alt_shift_toggle,grp_led:scroll\"\n\nBACKSPACE=\"guess\"\n"));
        QCOMPARE(debian.layout, QStringLiteral("us,ru"));
        QCOMPARE(debian.variant, QStringLiteral(","));
        QCOMPARE(debian.options, QStringLiteral("grp:alt_shift_toggle,grp_led:scroll"));
        QCOMPARE(debian.model, QStringLiteral("pc105"));
        QCOMPARE(Desktop::parseDefaultKeyboard(QStringLiteral("KEYMAP=ru\nXKBLAYOUT=de\n")).layout, QStringLiteral("de"));
    }

    // The MIME type the program writes for an AppImage is the one the package installs.
    void mimeDefinition()
    {
        QFile f(QStringLiteral(TS_GOLDEN_DIR "/../../resources/linux/org.typingstatistics.TypingStatistics.xml"));
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text)); // a Windows checkout has CRLF
        QCOMPARE(QString::fromUtf8(f.readAll()), FileAssociation::mimeDefinition());
    }

    void association()
    {
        using FileAssociation::State;
        QTemporaryDir dir;
        // Programs: two that exist, one that does not.
        auto program = [&dir](const QString &name) {
            const QString path = dir.filePath(name);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write("#!/bin/sh\n");
            f.close();
            f.setPermissions(f.permissions() | QFile::ExeOwner);
            return path;
        };
        const QString exe = program(QStringLiteral("TypingStatistics.exe")), other = program(QStringLiteral("TypeStats.exe"));
        const QString gone = dir.filePath(QStringLiteral("old/TypingStatistics.exe"));
#if defined(Q_OS_WIN)
        FileAssociation::Places p;
        const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\TypingStatisticsTest");
        p.classes = root + QStringLiteral("\\Classes");
        p.userChoice = root + QStringLiteral("\\UserChoice");
        QSettings(root, QSettings::NativeFormat).remove(QString());
        QCOMPARE(FileAssociation::tsfState(exe, p), State::None);
        QVERIFY(FileAssociation::associateTsf(exe, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Ours);
        QCOMPARE(QSettings(p.classes, QSettings::NativeFormat).value(QStringLiteral("TypingStatistics.tsf/shell/open/command/.")).toString(),
                 QLatin1Char('"') + QDir::toNativeSeparators(exe) + QStringLiteral("\" \"%1\""));
        QCOMPARE(FileAssociation::tsfState(other, p), State::Other); // another copy of the program is registered
        QVERIFY(FileAssociation::associateTsf(gone, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Moved);
        QSettings(p.classes, QSettings::NativeFormat).setValue(QStringLiteral(".tsf/."), QStringLiteral("tsFile")); // the original's
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Other);
        QSettings(p.userChoice, QSettings::NativeFormat).setValue(QStringLiteral("ProgId"), QStringLiteral("Applications\\TypingStatistics.exe"));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Ours); // chosen by the user in "Open with"
        QSettings(p.userChoice, QSettings::NativeFormat).setValue(QStringLiteral("ProgId"), QStringLiteral("tsFile"));
        QVERIFY(FileAssociation::associateTsf(exe, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Overridden);
        QSettings(root, QSettings::NativeFormat).remove(QString());
#elif defined(Q_OS_MACOS)
        Q_UNUSED(exe);
        Q_UNUSED(other);
        Q_UNUSED(gone);
        QCOMPARE(FileAssociation::tsfState(), State::Unsupported); // a test is not an .app
#else
        FileAssociation::Places p;
        p.dataHome = dir.filePath(QStringLiteral("home/data"));
        p.configHome = dir.filePath(QStringLiteral("home/config"));
        p.dataDirs = {dir.filePath(QStringLiteral("usr/share"))};
        p.configDirs = {dir.filePath(QStringLiteral("etc/xdg"))};
        p.desktops = {QStringLiteral("KDE")};
        p.runTools = false;
        const QString list = p.configHome + QStringLiteral("/mimeapps.list");
        auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        auto read = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        write(list, "[Default Applications]\ntext/plain=org.kde.kate.desktop\n\n[Removed Associations]\nx=y\n");

        QCOMPARE(FileAssociation::tsfState(exe, p), State::None);
        QVERIFY(FileAssociation::associateTsf(exe, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Ours);
        // The user's type, the user's desktop file (there is none in the system), the default; the rest is kept.
        QCOMPARE(read(p.dataHome + QStringLiteral("/mime/packages/org.typingstatistics.TypingStatistics.xml")),
                 FileAssociation::mimeDefinition());
        const QString desktop = read(p.dataHome + QStringLiteral("/applications/org.typingstatistics.TypingStatistics.desktop"));
        QVERIFY(desktop.contains(QStringLiteral("Exec=\"") + exe + QStringLiteral("\" %f\n")));
        QVERIFY(desktop.contains(QStringLiteral("MimeType=application/x-typing-statistics;\n")));
        QCOMPARE(read(list), QStringLiteral("[Default Applications]\ntext/plain=org.kde.kate.desktop\n"
                                            "application/x-typing-statistics=org.typingstatistics.TypingStatistics.desktop\n\n"
                                            "[Removed Associations]\nx=y\n\n"
                                            "[Added Associations]\n"
                                            "application/x-typing-statistics=org.typingstatistics.TypingStatistics.desktop;\n"));
        QCOMPARE(FileAssociation::tsfState(other, p), State::Other);
        // The AppImage was moved: its old place is gone.
        QVERIFY(FileAssociation::associateTsf(gone, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Moved);
        QVERIFY(FileAssociation::associateTsf(exe, p));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Ours);
        // A desktop-specific list goes first.
        write(p.configHome + QStringLiteral("/kde-mimeapps.list"),
              "[Default Applications]\napplication/x-typing-statistics=other.desktop\n");
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Other);
        QFile::remove(p.configHome + QStringLiteral("/kde-mimeapps.list"));

        // Installed by a package that starts this program: no desktop file of the user's own.
        QDir(p.dataHome).removeRecursively();
        write(p.dataDirs[0] + QStringLiteral("/applications/org.typingstatistics.TypingStatistics.desktop"),
              (QStringLiteral("[Desktop Entry]\nExec=") + exe + QStringLiteral(" %f\nMimeType=application/x-typing-statistics;\n")).toUtf8());
        write(p.dataDirs[0] + QStringLiteral("/mime/packages/org.typingstatistics.TypingStatistics.xml"), FileAssociation::mimeDefinition().toUtf8());
        QVERIFY(FileAssociation::associateTsf(exe, p));
        QVERIFY(!QFileInfo::exists(p.dataHome + QStringLiteral("/applications/org.typingstatistics.TypingStatistics.desktop")));
        QVERIFY(!QFileInfo::exists(p.dataHome + QStringLiteral("/mime")));
        QCOMPARE(FileAssociation::tsfState(exe, p), State::Ours);
#endif
    }
};

QTEST_APPLESS_MAIN(TstPlatform)
#include "tst_platform.moc"
