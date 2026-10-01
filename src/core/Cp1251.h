#pragma once

#include <QByteArray>
#include <QString>

// Windows-1251 codec. Qt 6 only ships UTF/Latin-1 converters on every platform,
// and the original program stores all text files in the Russian ANSI code page.
namespace Cp1251 {
QString decode(const QByteArray &bytes);
QByteArray encode(const QString &text);  // unmappable characters become '?'
char fromUnicode(char16_t u);            // one character of encode()
char16_t toUnicode(quint8 c);
}
