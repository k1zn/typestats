#include "Rsa.h"

#include "Der.h"

#include <vector>

namespace Rsa {

namespace {

using Limbs = std::vector<quint32>; // little-endian 32-bit words

Limbs fromBytes(QByteArrayView be, size_t words)
{
    Limbs out(words, 0);
    for (qsizetype i = 0; i < be.size(); ++i) {
        const size_t bit = size_t(be.size() - 1 - i) * 8;
        if (bit / 32 < words)
            out[bit / 32] |= quint32(quint8(be[i])) << (bit % 32);
    }
    return out;
}

QByteArray toBytes(const Limbs &v, qsizetype bytes)
{
    QByteArray out(bytes, '\0');
    for (qsizetype i = 0; i < bytes; ++i) {
        const size_t bit = size_t(bytes - 1 - i) * 8;
        if (bit / 32 < v.size())
            out[i] = char((v[bit / 32] >> (bit % 32)) & 0xFF);
    }
    return out;
}

// a >= b, same length
bool notLess(const Limbs &a, const Limbs &b)
{
    for (size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i])
            return a[i] > b[i];
    return true;
}

void subtract(Limbs &a, const Limbs &b)
{
    quint64 borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const quint64 d = quint64(a[i]) - b[i] - borrow;
        a[i] = quint32(d);
        borrow = (d >> 32) & 1;
    }
}

// Montgomery arithmetic modulo an odd n of k words: mul(a, b) = a·b·R⁻¹ mod n, R = 2^(32k).
class Montgomery
{
public:
    explicit Montgomery(Limbs n) : m_n(std::move(n))
    {
        // -n⁻¹ mod 2^32 by Newton's iteration.
        quint32 inv = 1;
        for (int i = 0; i < 5; ++i)
            inv *= 2 - m_n[0] * inv;
        m_ninv = quint32(0) - inv;
        // R² mod n by doubling 1 2·32k times.
        Limbs r(m_n.size(), 0);
        r[0] = 1;
        for (size_t i = 0; i < 64 * m_n.size(); ++i) {
            quint32 carry = 0;
            for (quint32 &w : r) {
                const quint32 next = w >> 31;
                w = (w << 1) | carry;
                carry = next;
            }
            if (carry || notLess(r, m_n))
                subtract(r, m_n);
        }
        m_r2 = std::move(r);
    }

    Limbs mul(const Limbs &a, const Limbs &b) const
    {
        const size_t k = m_n.size();
        std::vector<quint64> t(k + 2, 0);
        for (size_t i = 0; i < k; ++i) {
            quint64 carry = 0;
            for (size_t j = 0; j < k; ++j) {
                const quint64 v = t[j] + quint64(a[j]) * b[i] + carry;
                t[j] = quint32(v);
                carry = v >> 32;
            }
            quint64 v = t[k] + carry;
            t[k] = quint32(v);
            t[k + 1] = v >> 32;
            const quint32 m = quint32(t[0]) * m_ninv;
            v = t[0] + quint64(m) * m_n[0];
            carry = v >> 32;
            for (size_t j = 1; j < k; ++j) {
                v = t[j] + quint64(m) * m_n[j] + carry;
                t[j - 1] = quint32(v);
                carry = v >> 32;
            }
            v = t[k] + carry;
            t[k - 1] = quint32(v);
            t[k] = t[k + 1] + (v >> 32);
            t[k + 1] = 0;
        }
        Limbs out(k);
        for (size_t j = 0; j < k; ++j)
            out[j] = quint32(t[j]);
        if (t[k] || notLess(out, m_n))
            subtract(out, m_n);
        return out;
    }

    Limbs power(const Limbs &base, QByteArrayView exponent) const
    {
        const Limbs b = mul(base, m_r2); // to the Montgomery form
        Limbs one(m_n.size(), 0);
        one[0] = 1;
        Limbs acc = mul(one, m_r2);
        for (const char byte : exponent)
            for (int bit = 7; bit >= 0; --bit) {
                acc = mul(acc, acc);
                if ((quint8(byte) >> bit) & 1)
                    acc = mul(acc, b);
            }
        return mul(acc, one); // back from the Montgomery form
    }

private:
    Limbs m_n, m_r2;
    quint32 m_ninv = 0;
};

// DigestInfo prefixes (RFC 8017, 9.2 note 1): with the NULL parameters, and the same without them.
QByteArray digestInfo(QCryptographicHash::Algorithm alg, QByteArrayView digest, bool withNull)
{
    const char *oid = nullptr;
    switch (alg) {
    case QCryptographicHash::Sha1: oid = "1.3.14.3.2.26"; break;
    case QCryptographicHash::Sha256: oid = "2.16.840.1.101.3.4.2.1"; break;
    case QCryptographicHash::Sha384: oid = "2.16.840.1.101.3.4.2.2"; break;
    case QCryptographicHash::Sha512: oid = "2.16.840.1.101.3.4.2.3"; break;
    default: return {};
    }
    QByteArray algId = Der::tlv(Der::Oid, Der::oid(oid));
    if (withNull)
        algId += Der::tlv(Der::Null, {});
    return Der::tlv(Der::Sequence, Der::tlv(Der::Sequence, algId) + Der::tlv(Der::OctetString, digest));
}

} // namespace

std::optional<PublicKey> fromSpki(QByteArrayView spki)
{
    const std::optional<Der::Node> top = Der::parse(spki);
    if (!top || top->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader r(top->content);
    const std::optional<Der::Node> alg = r.next(Der::Sequence), bits = r.next(Der::BitString);
    if (!alg || !bits || bits->content.isEmpty() || bits->content[0] != 0)
        return std::nullopt;
    Der::Reader a(alg->content);
    const std::optional<Der::Node> id = a.next(Der::Oid);
    if (!id || id->content != Der::oid("1.2.840.113549.1.1.1"))
        return std::nullopt;
    const std::optional<Der::Node> key = Der::parse(bits->content.sliced(1));
    if (!key || key->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader k(key->content);
    const std::optional<Der::Node> n = k.next(Der::Integer), e = k.next(Der::Integer);
    if (!n || !e)
        return std::nullopt;
    PublicKey out{Der::unsignedInteger(*n).toByteArray(), Der::unsignedInteger(*e).toByteArray()};
    // An odd modulus of a sensible size, a small odd exponent.
    if (out.modulus.size() < 128 || out.modulus.size() > 1024 || !(quint8(out.modulus.back()) & 1)
        || out.exponent.size() > 8 || !(quint8(out.exponent.back()) & 1))
        return std::nullopt;
    return out;
}

std::optional<QByteArray> publicOperation(const PublicKey &key, QByteArrayView signature)
{
    const qsizetype bytes = key.modulus.size();
    if (signature.size() != bytes)
        return std::nullopt;
    const size_t words = size_t(bytes + 3) / 4;
    const Limbs n = fromBytes(key.modulus, words), s = fromBytes(signature, words);
    if (notLess(s, n))
        return std::nullopt;
    const Montgomery m(n);
    return toBytes(m.power(s, key.exponent), bytes);
}

bool verify(const PublicKey &key, QCryptographicHash::Algorithm alg, QByteArrayView digest, QByteArrayView signature)
{
    const std::optional<QByteArray> em = publicOperation(key, signature);
    if (!em)
        return false;
    for (const bool withNull : {true, false}) {
        const QByteArray t = digestInfo(alg, digest, withNull);
        if (t.isEmpty() || em->size() < t.size() + 11)
            return false;
        QByteArray expected(em->size(), char(0xFF));
        expected[0] = 0x00;
        expected[1] = 0x01;
        expected[em->size() - t.size() - 1] = 0x00;
        expected.replace(em->size() - t.size(), t.size(), t);
        if (*em == expected)
            return true;
    }
    return false;
}

} // namespace Rsa
