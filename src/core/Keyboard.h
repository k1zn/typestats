#pragma once

#include <QString>

#include <optional>

// Layout-independent keyboard helpers (US reference layout, scan code set 1).
namespace Keyboard {

quint8 vkToScan(quint8 vk);

// A set-1 scan code as the Windows hook gives it: the code and the E0 prefix (LLKHF_EXTENDED).
struct ScanCode
{
    quint8 code = 0;
    bool extended = false;
    bool operator==(const ScanCode &) const = default;
};

// The virtual-key code the Windows hook gives for a scan code with the US layout (vkCode of
// KBDLLHOOKSTRUCT): left and right modifiers apart, the numeric keypad by NumLock (without it
// VK_HOME and so on, not extended), Pause = scan 0x45 not extended, NumLock = 0x45 extended.
// 0 for a code without one.
quint8 scanToVk(ScanCode scan, bool numLock);

// Linux input event codes (KEY_*, linux/input-event-codes.h) and the scan codes of the same keys.
// The main block is set 1 itself (KEY_ESC = 1 ... KEY_KPDOT = 83); the rest is a table. A key
// Windows has no scan code for (KEY_FN, KEY_BRIGHTNESSUP...) has none here either.
std::optional<ScanCode> evdevToScan(int code);
int scanToEvdev(ScanCode scan); // -1 for none

}
