#pragma once

#include <QString>

#include <optional>

// The system a recording was made on. Its keys are recorded with Windows' codes (VK_LWIN for Super and
// Command, VK_MENU for Option), the names are the system's own: Windows (the original's, and files
// without the mark) [LWin], Linux [LSuper], macOS [LCmd], [LOption], "Option+".
enum class KeyPlatform : quint8 { Windows, Linux, MacOS };
KeyPlatform currentKeyPlatform();
// "Platform" of the .tsf header: "Linux", "macOS"; empty for Windows.
QString keyPlatformName(KeyPlatform p);
KeyPlatform keyPlatformFromName(const QString &name);

// Name of a key press as shown in the text and key lists (KeyDisplayName, 0x405fe0).
// A printable key gives its character, Enter gives "\r", anything else is bracketed:
// "[BackSpace]", "[LShift]", "[Ctrl+BackSpace]", "[Alt+x]".
QString keyDisplayName(quint32 flags, char16_t ch, KeyPlatform platform = KeyPlatform::Windows);
// The name's only character when keyDisplayName() gives a name of one character; the name is not built.
std::optional<char16_t> keyDisplayChar(quint32 flags, char16_t ch);
