#pragma once

#include <QByteArray>
#include <QString>

// Z85 (ZeroMQ RFC 32): 4 bytes as 5 printable characters, without spaces, quotes or backslashes - binary data in
// lines of a .tsf at +25 % instead of base64's +33 % (re/webcam.md). The data is padded with zero bytes to a
// multiple of 4: the caller keeps its true length.
namespace Z85 {
QString encode(QByteArrayView data);
// Empty and false on a character outside the alphabet or a length not a multiple of 5.
QByteArray decode(QStringView text, bool *ok = nullptr);
}
