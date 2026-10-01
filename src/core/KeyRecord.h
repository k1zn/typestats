#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

// One keyboard event, exactly as the original Typing statistics stores it
// (see re/tsf_format.md for the reverse-engineered layout).
struct KeyRecord
{
    enum Flag : quint32 {
        ScanMask     = 0x000000FF,
        Transient    = 0x00000100, // internal, cleared on save
        Marked       = 0x00000200, // "Пометить (Ins)"
        Alt          = 0x00000400,
        Ctrl         = 0x00000800,
        Injected     = 0x00001000,
        KeyUp        = 0x00002000,
        Extended     = 0x00004000,
        NoChar       = 0x00008000,
        VkMask       = 0x00FF0000,
        HasChar      = 0x01000000,
        Shift        = 0x04000000,
        SingleChar   = 0x08000000,
        Packet       = 0x10000000,
        DeadKey      = 0x20000000,
        SegmentStart = 0x40000000, // first press of a fragment: the original's Recalculate sets it and saves it; ignored on reading
        Win          = 0x80000000,
    };

    quint32 dtUs = 0;    // microseconds since the previous event
    quint32 flags = 0;
    char16_t ch = 0;     // produced UTF-16 character (valid when HasChar)
    QString comment;     // optional text mark

    bool isDown() const { return !(flags & KeyUp); }
    bool isUp() const { return flags & KeyUp; }
    quint8 vk() const { return (flags & VkMask) >> 16; }
    quint8 scan() const { return flags & ScanMask; }
    bool hasChar() const { return flags & HasChar; }
    bool has(quint32 f) const { return (flags & f) == f; }
};

// Movable as bytes (QString is): a growing vector of records is reallocated, not copied one by one.
Q_DECLARE_TYPEINFO(KeyRecord, Q_RELOCATABLE_TYPE);

using KeyRecords = QVector<KeyRecord>;

namespace Vk {
enum : quint8 {
    Back = 0x08, Tab = 0x09, Return = 0x0D, Shift = 0x10, Control = 0x11, Menu = 0x12,
    Capital = 0x14, Escape = 0x1B, Space = 0x20, Delete = 0x2E, LWin = 0x5B, RWin = 0x5C,
    F2 = 0x71, F4 = 0x73, F8 = 0x77, F9 = 0x78,
    LShift = 0xA0, RShift = 0xA1, LControl = 0xA2, RControl = 0xA3, LMenu = 0xA4, RMenu = 0xA5,
    Packet = 0xE7,
};
}
