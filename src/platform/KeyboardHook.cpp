#include "KeyboardHook.h"

#include <QByteArray>

QString UsLayout::keyName(quint8 scan)
{
    static const struct { quint8 first; const char *chars; } rows[] = {
        {0x02, "1234567890-="}, {0x10, "qwertyuiop[]"}, {0x1E, "asdfghjkl;'"}, {0x2C, "zxcvbnm,./"}};
    for (const auto &row : rows)
        if (scan >= row.first && scan < row.first + qstrlen(row.chars))
            return QString(QLatin1Char(row.chars[scan - row.first]));
    switch (scan) {
    case 0x29: return QStringLiteral("`");
    case 0x2B: return QStringLiteral("\\");
    case 0x0E: return QStringLiteral("[BackSpace]");
    case 0x0F: return QStringLiteral("[Tab]");
    case 0x3A: return QStringLiteral("[CapsLock]");
    case 0x2A: return QStringLiteral("[LShift]");
    case 0x36: return QStringLiteral("[RShift]");
    }
    return {};
}

int UsLayout::toUnicode(quint8 scan, bool shift, bool caps, char16_t out[2])
{
    static const struct { quint8 first; const char *plain, *shifted; } rows[] = {
        {0x02, "1234567890-=", "!@#$%^&*()_+"}, {0x10, "qwertyuiop[]", "QWERTYUIOP{}"},
        {0x1E, "asdfghjkl;'`", "ASDFGHJKL:\"~"}, {0x2B, "\\zxcvbnm,./", "|ZXCVBNM<>?"}, {0x39, " ", " "}};
    for (const auto &row : rows)
        if (scan >= row.first && scan < row.first + qstrlen(row.plain)) {
            const char c = row.plain[scan - row.first];
            const bool letter = c >= 'a' && c <= 'z';
            out[0] = char16_t((shift != (caps && letter) ? row.shifted : row.plain)[scan - row.first]);
            return 1;
        }
    return 0;
}
