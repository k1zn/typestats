#include "Der.h"

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>

namespace Der {

namespace {

// One TLV at pos (definite lengths only, as DER requires); its size, or 0 when malformed.
qsizetype element(QByteArrayView d, qsizetype pos, Node &out)
{
    const qsizetype n = d.size();
    if (pos + 2 > n)
        return 0;
    const quint8 tag = quint8(d[pos]);
    if ((tag & 0x1F) == 0x1F) // high tag numbers: not used by anything read here
        return 0;
    qsizetype p = pos + 1;
    quint8 first = quint8(d[p++]);
    qsizetype len = 0;
    if (first < 0x80) {
        len = first;
    } else {
        const int bytes = first & 0x7F;
        if (bytes == 0 || bytes > 4 || p + bytes > n)
            return 0;
        for (int i = 0; i < bytes; ++i)
            len = (len << 8) | quint8(d[p++]);
    }
    if (len < 0 || len > n - p)
        return 0;
    out.tag = tag;
    out.content = d.sliced(p, len);
    out.whole = d.sliced(pos, p + len - pos);
    return p + len - pos;
}

} // namespace

std::optional<Node> Reader::peek() const
{
    if (atEnd())
        return std::nullopt;
    Node node;
    if (element(m_data, m_pos, node) == 0) {
        m_failed = true;
        return std::nullopt;
    }
    return node;
}

std::optional<Node> Reader::next()
{
    std::optional<Node> node = peek();
    if (node)
        m_pos += node->whole.size();
    return node;
}

std::optional<Node> Reader::next(quint8 tag)
{
    const std::optional<Node> node = peek();
    if (!node || node->tag != tag)
        return std::nullopt;
    m_pos += node->whole.size();
    return node;
}

std::optional<Node> parse(QByteArrayView data)
{
    Node node;
    if (element(data, 0, node) != data.size())
        return std::nullopt;
    return node;
}

QByteArray oid(const char *dotted)
{
    const QList<QByteArray> parts = QByteArray(dotted).split('.');
    QByteArray out;
    if (parts.size() < 2)
        return out;
    QList<quint64> arcs;
    for (const QByteArray &p : parts)
        arcs.append(p.toULongLong());
    arcs[1] += arcs[0] * 40;
    for (qsizetype i = 1; i < arcs.size(); ++i) {
        quint64 v = arcs[i];
        QByteArray bytes(1, char(v & 0x7F));
        while (v >>= 7)
            bytes.prepend(char(0x80 | (v & 0x7F)));
        out += bytes;
    }
    return out;
}

std::optional<qint64> time(const Node &n)
{
    const QByteArray s = n.content.toByteArray();
    int year = 0;
    qsizetype p = 0;
    const auto digits = [&](int count) -> std::optional<int> {
        int v = 0;
        for (int i = 0; i < count; ++i, ++p) {
            if (p >= s.size() || s[p] < '0' || s[p] > '9')
                return std::nullopt;
            v = v * 10 + (s[p] - '0');
        }
        return v;
    };
    if (n.tag == UtcTime) {
        const std::optional<int> y = digits(2);
        if (!y)
            return std::nullopt;
        year = *y < 50 ? 2000 + *y : 1900 + *y; // RFC 5280
    } else if (n.tag == GeneralizedTime) {
        const std::optional<int> y = digits(4);
        if (!y)
            return std::nullopt;
        year = *y;
    } else {
        return std::nullopt;
    }
    const std::optional<int> mon = digits(2), day = digits(2), h = digits(2), m = digits(2), sec = digits(2);
    if (!mon || !day || !h || !m || !sec)
        return std::nullopt;
    int ms = 0;
    if (p < s.size() && s[p] == '.') { // fractions of a second (GeneralizedTime only)
        ++p;
        int scale = 100;
        bool any = false;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
            ms += (s[p] - '0') * scale;
            scale /= 10;
            any = true;
            ++p;
        }
        if (!any)
            return std::nullopt;
    }
    if (p != s.size() - 1 || s[p] != 'Z')
        return std::nullopt;
    const QDate date(year, *mon, *day);
    const QTime t(*h, *m, *sec);
    if (!date.isValid() || !t.isValid())
        return std::nullopt;
    return QDateTime(date, t, QTimeZone::utc()).toMSecsSinceEpoch() + ms;
}

QByteArrayView unsignedInteger(const Node &n)
{
    QByteArrayView v = n.content;
    while (v.size() > 1 && v[0] == 0)
        v = v.sliced(1);
    return v;
}

QByteArray tlv(quint8 tag, QByteArrayView content)
{
    QByteArray out(1, char(tag));
    const qsizetype len = content.size();
    if (len < 0x80) {
        out += char(len);
    } else {
        QByteArray bytes;
        for (qsizetype l = len; l; l >>= 8)
            bytes.prepend(char(l & 0xFF));
        out += char(0x80 | bytes.size());
        out += bytes;
    }
    out += content;
    return out;
}

QByteArray integer(quint64 v)
{
    QByteArray bytes;
    do {
        bytes.prepend(char(v & 0xFF));
        v >>= 8;
    } while (v);
    if (quint8(bytes[0]) & 0x80)
        bytes.prepend('\0'); // positive
    return tlv(Integer, bytes);
}

} // namespace Der
