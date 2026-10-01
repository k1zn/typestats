#pragma once

#include <QString>

#include <optional>

// Name of a key press as shown in the text and key lists (KeyDisplayName, 0x405fe0).
// A printable key gives its character, Enter gives "\r", anything else is bracketed:
// "[BackSpace]", "[LShift]", "[Ctrl+BackSpace]", "[Alt+x]".
QString keyDisplayName(quint32 flags, char16_t ch);
// The name's only character when keyDisplayName() gives a name of one character; the name is not built.
std::optional<char16_t> keyDisplayChar(quint32 flags, char16_t ch);
