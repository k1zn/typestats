#include "KeyName.h"

#include "KeyRecord.h"

static const char *noCharName(quint8 vk)
{
    switch (vk) {
    case 0x13: return "Pause";
    case 0x14: return "Caps Lock";
    case 0x21: return "Page Up";
    case 0x22: return "Page Down";
    case 0x23: return "End";
    case 0x24: return "Home";
    case 0x25: return "Left";
    case 0x26: return "Up";
    case 0x27: return "Right";
    case 0x28: return "Down";
    case 0x2C: return "Print Screen";
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x5D: return "SysMenu";
    case 0x90: return "Num Lock";
    case 0x91: return "Scroll Lock";
    case 0xA0: return "LShift";
    case 0xA1: return "RShift";
    default: return "Unrecognized key";
    }
}

QString keyDisplayName(quint32 flags, char16_t ch)
{
    const quint8 vk = (flags >> 16) & 0xFF;
    QString s;
    if (!(flags & KeyRecord::NoChar)) {
        switch (vk) {
        case 0x08: s = QStringLiteral("BackSpace"); break;
        case 0x09: s = QStringLiteral("Tab"); break;
        case 0x0D: s = QStringLiteral("\r"); break;
        case 0x1B: s = QStringLiteral("Esc"); break;
        }
    } else {
        // These come back already bracketed and without Alt/Ctrl prefixes.
        switch (vk) {
        case 0x5B: return QStringLiteral("[LWin]");
        case 0x5C: return QStringLiteral("[RWin]");
        case 0xA2: return QStringLiteral("[LCtrl]");
        case 0xA3: return QStringLiteral("[RCtrl]");
        case 0xA4: return QStringLiteral("[LAlt]");
        case 0xA5: return QStringLiteral("[RAlt]");
        }
        if (vk >= 0x70 && vk <= 0x7B)
            s = QLatin1Char('F') + QString::number(vk - 0x6F);
        else
            s = QLatin1String(noCharName(vk));
    }
    if (s.isEmpty())
        s = QChar(ch);
    if (flags & KeyRecord::Alt)
        s.prepend(QLatin1String("Alt+"));
    if (flags & KeyRecord::Ctrl)
        s.prepend(QLatin1String("Ctrl+"));
    if (s.size() > 1)
        s = QLatin1Char('[') + s + QLatin1Char(']');
    return s;
}
