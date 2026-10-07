#include "Z85.h"

#include <array>

namespace {

constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

constexpr std::array<qint8, 128> decodeTable()
{
    std::array<qint8, 128> t{};
    for (auto &v : t)
        v = -1;
    for (int i = 0; i < 85; ++i)
        t[size_t(kAlphabet[i])] = qint8(i);
    return t;
}

constexpr auto kDecode = decodeTable();

} // namespace

namespace Z85 {

QString encode(QByteArrayView data)
{
    const qsizetype words = (data.size() + 3) / 4;
    QString out(words * 5, Qt::Uninitialized);
    char16_t *p = reinterpret_cast<char16_t *>(out.data());
    for (qsizetype w = 0; w < words; ++w) {
        quint32 v = 0;
        for (int i = 0; i < 4; ++i) {
            const qsizetype k = w * 4 + i;
            v = v << 8 | (k < data.size() ? quint8(data[k]) : 0);
        }
        for (int i = 4; i >= 0; --i) {
            p[w * 5 + i] = char16_t(kAlphabet[v % 85]);
            v /= 85;
        }
    }
    return out;
}

QByteArray decode(QStringView text, bool *ok)
{
    if (ok)
        *ok = false;
    if (text.size() % 5)
        return {};
    QByteArray out(text.size() / 5 * 4, Qt::Uninitialized);
    char *p = out.data();
    for (qsizetype w = 0; w < text.size() / 5; ++w) {
        quint64 v = 0;
        for (int i = 0; i < 5; ++i) {
            const char16_t c = text[w * 5 + i].unicode();
            if (c >= 128 || kDecode[c] < 0)
                return {};
            v = v * 85 + quint64(kDecode[c]);
        }
        if (v > 0xFFFFFFFFu)
            return {};
        for (int i = 3; i >= 0; --i) {
            p[w * 4 + i] = char(v & 0xFF);
            v >>= 8;
        }
    }
    if (ok)
        *ok = true;
    return out;
}

} // namespace Z85
