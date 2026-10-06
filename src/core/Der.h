#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

// The little of DER (X.690) that time stamps, CMS and X.509 need: reading nested TLVs in place, writing them.
namespace Der {

enum Tag : quint8 {
    Boolean = 0x01,
    Integer = 0x02,
    BitString = 0x03,
    OctetString = 0x04,
    Null = 0x05,
    Oid = 0x06,
    Utf8String = 0x0C,
    PrintableString = 0x13,
    UtcTime = 0x17,
    GeneralizedTime = 0x18,
    Sequence = 0x30,
    Set = 0x31,
};

// Context-specific tag [n]: constructed (EXPLICIT, or IMPLICIT of a SEQUENCE/SET) or primitive.
constexpr quint8 context(int n, bool constructed = true) { return quint8(0x80 | (constructed ? 0x20 : 0) | n); }

struct Node
{
    quint8 tag = 0;
    QByteArrayView whole;   // tag, length and content
    QByteArrayView content;
};

// The elements of a DER string, one after another. Any malformed element ends the reading (next() gives nothing
// from then on, failed() tells it).
class Reader
{
public:
    explicit Reader(QByteArrayView data) : m_data(data) {}
    bool atEnd() const { return m_pos >= m_data.size() || m_failed; }
    bool failed() const { return m_failed; }
    std::optional<Node> peek() const;
    std::optional<Node> next();
    // The next element if it has this tag (otherwise nothing, and nothing is consumed).
    std::optional<Node> next(quint8 tag);

private:
    QByteArrayView m_data;
    qsizetype m_pos = 0;
    mutable bool m_failed = false;
};

// The single element of the string; nothing if it is malformed or followed by more bytes.
std::optional<Node> parse(QByteArrayView data);

// Content of an OID as its DER bytes, e.g. oid("2.16.840.1.101.3.4.2.1") - for comparisons.
QByteArray oid(const char *dotted);
// UTCTime / GeneralizedTime (with fractions of a second) as milliseconds since 1970 UTC.
std::optional<qint64> time(const Node &n);
// INTEGER as an unsigned big-endian number without the leading zero bytes.
QByteArrayView unsignedInteger(const Node &n);

QByteArray tlv(quint8 tag, QByteArrayView content);
QByteArray integer(quint64 v);

} // namespace Der
