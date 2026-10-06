#include "TimeStamp.h"

#include "Der.h"
#include "Rsa.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMutex>
#include <QSet>

namespace TimeStamp {

namespace {

const QByteArray &oidSignedData() { static const QByteArray o = Der::oid("1.2.840.113549.1.7.2"); return o; }
const QByteArray &oidTstInfo() { static const QByteArray o = Der::oid("1.2.840.113549.1.9.16.1.4"); return o; }
const QByteArray &oidContentType() { static const QByteArray o = Der::oid("1.2.840.113549.1.9.3"); return o; }
const QByteArray &oidMessageDigest() { static const QByteArray o = Der::oid("1.2.840.113549.1.9.4"); return o; }
const QByteArray &oidSha256() { static const QByteArray o = Der::oid("2.16.840.1.101.3.4.2.1"); return o; }
const QByteArray &oidCommonName() { static const QByteArray o = Der::oid("2.5.4.3"); return o; }

std::optional<QCryptographicHash::Algorithm> digestOf(QByteArrayView oid)
{
    static const QByteArray sha256 = Der::oid("2.16.840.1.101.3.4.2.1"), sha384 = Der::oid("2.16.840.1.101.3.4.2.2"),
                            sha512 = Der::oid("2.16.840.1.101.3.4.2.3"), rsa256 = Der::oid("1.2.840.113549.1.1.11"),
                            rsa384 = Der::oid("1.2.840.113549.1.1.12"), rsa512 = Der::oid("1.2.840.113549.1.1.13");
    if (oid == sha256 || oid == rsa256)
        return QCryptographicHash::Sha256;
    if (oid == sha384 || oid == rsa384)
        return QCryptographicHash::Sha384;
    if (oid == sha512 || oid == rsa512)
        return QCryptographicHash::Sha512;
    return std::nullopt; // SHA-1 and the rest: not accepted
}

QByteArrayView algorithmOid(const Der::Node &algId)
{
    Der::Reader r(algId.content);
    const std::optional<Der::Node> id = r.next(Der::Oid);
    return id ? id->content : QByteArrayView();
}

// The pieces of a certificate a chain needs.
struct Certificate
{
    QByteArray der;
    QByteArrayView tbs, issuer, subject, serial, spki, subjectKeyId, signatureAlgorithm, signature;
    qint64 notBefore = 0, notAfter = 0;
    bool ca = false, timeStamping = false;
    QString commonName;
};

QString commonNameOf(QByteArrayView name)
{
    Der::Reader rdns(name);
    while (const std::optional<Der::Node> rdn = rdns.next(Der::Set)) {
        Der::Reader atvs(rdn->content);
        while (const std::optional<Der::Node> atv = atvs.next(Der::Sequence)) {
            Der::Reader a(atv->content);
            const std::optional<Der::Node> type = a.next(Der::Oid), value = a.next();
            if (type && value && type->content == oidCommonName())
                return QString::fromUtf8(value->content.toByteArray());
        }
    }
    return {};
}

std::optional<Certificate> parseCertificate(const QByteArray &der)
{
    Certificate c;
    c.der = der;
    const std::optional<Der::Node> top = Der::parse(c.der);
    if (!top || top->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader r(top->content);
    const std::optional<Der::Node> tbs = r.next(Der::Sequence), alg = r.next(Der::Sequence),
                                   sig = r.next(Der::BitString);
    if (!tbs || !alg || !sig || sig->content.isEmpty() || sig->content[0] != 0)
        return std::nullopt;
    c.tbs = tbs->whole;
    c.signatureAlgorithm = algorithmOid(*alg);
    c.signature = sig->content.sliced(1);
    Der::Reader t(tbs->content);
    t.next(Der::context(0)); // version
    const std::optional<Der::Node> serial = t.next(Der::Integer);
    t.next(Der::Sequence); // signature algorithm, again
    const std::optional<Der::Node> issuer = t.next(Der::Sequence), validity = t.next(Der::Sequence),
                                   subject = t.next(Der::Sequence), spki = t.next(Der::Sequence);
    if (!serial || !issuer || !validity || !subject || !spki)
        return std::nullopt;
    c.serial = Der::unsignedInteger(*serial);
    c.issuer = issuer->content;
    c.subject = subject->content;
    c.spki = spki->whole;
    c.commonName = commonNameOf(c.subject);
    Der::Reader v(validity->content);
    const std::optional<Der::Node> from = v.next(), to = v.next();
    const std::optional<qint64> fromMs = from ? Der::time(*from) : std::nullopt, toMs = to ? Der::time(*to) : std::nullopt;
    if (!fromMs || !toMs)
        return std::nullopt;
    c.notBefore = *fromMs;
    c.notAfter = *toMs;
    t.next(Der::context(1, false));
    t.next(Der::context(2, false));
    if (const std::optional<Der::Node> ext = t.next(Der::context(3))) {
        const std::optional<Der::Node> list = Der::parse(ext->content);
        if (!list || list->tag != Der::Sequence)
            return std::nullopt;
        static const QByteArray eku = Der::oid("2.5.29.37"), basic = Der::oid("2.5.29.19"), ski = Der::oid("2.5.29.14"),
                                timeStamping = Der::oid("1.3.6.1.5.5.7.3.8");
        Der::Reader exts(list->content);
        while (const std::optional<Der::Node> e = exts.next(Der::Sequence)) {
            Der::Reader x(e->content);
            const std::optional<Der::Node> id = x.next(Der::Oid);
            x.next(Der::Boolean);
            const std::optional<Der::Node> value = x.next(Der::OctetString);
            if (!id || !value)
                return std::nullopt;
            const std::optional<Der::Node> inner = Der::parse(value->content);
            if (id->content == eku && inner) {
                Der::Reader purposes(inner->content);
                while (const std::optional<Der::Node> p = purposes.next(Der::Oid))
                    c.timeStamping |= p->content == timeStamping;
            } else if (id->content == basic && inner) {
                Der::Reader b(inner->content);
                const std::optional<Der::Node> ca = b.next(Der::Boolean);
                c.ca = ca && !ca->content.isEmpty() && ca->content[0] != 0;
            } else if (id->content == ski && inner && inner->tag == Der::OctetString) {
                c.subjectKeyId = inner->content;
            }
        }
    }
    return c;
}

// The pinned roots, SubjectPublicKeyInfo in DER.
const QList<QByteArray> &anchors()
{
    static const QList<QByteArray> keys = [] {
        static const char *const base64[] = {
#include "StampRoots.inc"
        };
        QList<QByteArray> out;
        for (const char *b : base64)
            out.append(QByteArray::fromBase64(b));
        return out;
    }();
    return keys;
}

QMutex g_cacheMutex;
QHash<QByteArray, bool> g_signatures; // sha256(signed bytes, signature, key) -> valid
QSet<QByteArray> g_goodTokens;       // sha256(token, certificate pool) of tokens that passed

QByteArray sha256(QByteArrayView a) { return QCryptographicHash::hash(a, QCryptographicHash::Sha256); }

bool signatureValid(QByteArrayView spki, QCryptographicHash::Algorithm alg, QByteArrayView signedBytes,
                    QByteArrayView signature)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(sha256(signedBytes));
    h.addData(sha256(signature));
    h.addData(sha256(spki));
    h.addData(QByteArray::number(int(alg)));
    const QByteArray key = h.result();
    {
        const QMutexLocker lock(&g_cacheMutex);
        if (const auto it = g_signatures.constFind(key); it != g_signatures.cend())
            return *it;
    }
    const std::optional<Rsa::PublicKey> pub = Rsa::fromSpki(spki);
    const bool ok = pub && Rsa::verify(*pub, alg, QCryptographicHash::hash(signedBytes, alg), signature);
    const QMutexLocker lock(&g_cacheMutex);
    g_signatures.insert(key, ok);
    return ok;
}

bool signedBy(const Certificate &c, QByteArrayView issuerSpki)
{
    const std::optional<QCryptographicHash::Algorithm> alg = digestOf(c.signatureAlgorithm);
    return alg && signatureValid(issuerSpki, *alg, c.tbs, c.signature);
}

// From the signer's certificate up to a pinned root, every certificate valid at `atMs`.
bool trusted(const Certificate &leaf, const QList<Certificate> &pool, qint64 atMs)
{
    const QList<QByteArray> &roots = anchors();
    const Certificate *c = &leaf;
    for (int depth = 0; depth < 6; ++depth) {
        if (roots.contains(c->spki.toByteArray()))
            return depth > 0; // a root does not stamp by itself
        if (atMs < c->notBefore || atMs > c->notAfter)
            return false;
        if (depth > 0 && !c->ca)
            return false;
        for (const QByteArray &root : roots)
            if (signedBy(*c, root))
                return true;
        const Certificate *issuer = nullptr;
        for (const Certificate &candidate : pool)
            if (candidate.subject == c->issuer && candidate.der != c->der && signedBy(*c, candidate.spki)) {
                issuer = &candidate;
                break;
            }
        if (!issuer)
            return false;
        c = issuer;
    }
    return false;
}

struct SignedData
{
    QByteArrayView tstInfo;      // eContent: the bytes the signature covers through messageDigest
    std::optional<Der::Node> certificates;
    std::optional<Der::Node> signerInfo;
};

std::optional<SignedData> signedData(QByteArrayView token)
{
    const std::optional<Der::Node> top = Der::parse(token);
    if (!top || top->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader ci(top->content);
    const std::optional<Der::Node> type = ci.next(Der::Oid), explicitContent = ci.next(Der::context(0));
    if (!type || type->content != oidSignedData() || !explicitContent)
        return std::nullopt;
    const std::optional<Der::Node> sd = Der::parse(explicitContent->content);
    if (!sd || sd->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader r(sd->content);
    const std::optional<Der::Node> version = r.next(Der::Integer), digests = r.next(Der::Set),
                                   encap = r.next(Der::Sequence);
    if (!version || !digests || !encap)
        return std::nullopt;
    SignedData out;
    Der::Reader e(encap->content);
    const std::optional<Der::Node> eType = e.next(Der::Oid), eContent = e.next(Der::context(0));
    if (!eType || eType->content != oidTstInfo() || !eContent)
        return std::nullopt;
    const std::optional<Der::Node> octets = Der::parse(eContent->content);
    if (!octets || octets->tag != Der::OctetString)
        return std::nullopt;
    out.tstInfo = octets->content;
    out.certificates = r.next(Der::context(0));
    r.next(Der::context(1)); // CRLs
    const std::optional<Der::Node> signers = r.next(Der::Set);
    if (!signers)
        return std::nullopt;
    Der::Reader s(signers->content);
    out.signerInfo = s.next(Der::Sequence);
    if (!out.signerInfo || !s.atEnd()) // exactly one signer
        return std::nullopt;
    return out;
}

std::optional<Info> parseTstInfo(QByteArrayView der)
{
    const std::optional<Der::Node> top = Der::parse(der);
    if (!top || top->tag != Der::Sequence)
        return std::nullopt;
    Der::Reader r(top->content);
    const std::optional<Der::Node> version = r.next(Der::Integer), policy = r.next(Der::Oid),
                                   imprint = r.next(Der::Sequence), serial = r.next(Der::Integer),
                                   genTime = r.next(Der::GeneralizedTime);
    if (!version || !policy || !imprint || !serial || !genTime)
        return std::nullopt;
    Info info;
    Der::Reader m(imprint->content);
    const std::optional<Der::Node> alg = m.next(Der::Sequence), hash = m.next(Der::OctetString);
    if (!alg || !hash)
        return std::nullopt;
    info.sha256 = algorithmOid(*alg) == oidSha256();
    info.imprint = hash->content.toByteArray();
    const std::optional<qint64> t = Der::time(*genTime);
    if (!t)
        return std::nullopt;
    info.timeMs = *t;
    r.next(Der::Sequence); // accuracy
    r.next(Der::Boolean);  // ordering
    if (const std::optional<Der::Node> nonce = r.next(Der::Integer))
        info.nonce = Der::unsignedInteger(*nonce).toByteArray();
    return info;
}

} // namespace

QByteArray request(QByteArrayView hash, quint64 nonce, bool withCertificates)
{
    const QByteArray algId =
        Der::tlv(Der::Sequence, Der::tlv(Der::Oid, oidSha256()) + Der::tlv(Der::Null, {}));
    QByteArray body = Der::integer(1) + Der::tlv(Der::Sequence, algId + Der::tlv(Der::OctetString, hash))
                      + Der::integer(nonce);
    if (withCertificates)
        body += Der::tlv(Der::Boolean, QByteArray(1, char(0xFF)));
    return Der::tlv(Der::Sequence, body);
}

Response parseResponse(QByteArrayView der)
{
    Response out;
    const std::optional<Der::Node> top = Der::parse(der);
    if (!top || top->tag != Der::Sequence) {
        out.error = QStringLiteral("not a time stamp response");
        return out;
    }
    Der::Reader r(top->content);
    const std::optional<Der::Node> status = r.next(Der::Sequence);
    if (!status) {
        out.error = QStringLiteral("no status");
        return out;
    }
    Der::Reader s(status->content);
    const std::optional<Der::Node> code = s.next(Der::Integer);
    const QByteArrayView value = code ? Der::unsignedInteger(*code) : QByteArrayView();
    if (value.size() != 1 || value[0] > 1) { // granted (0) or grantedWithMods (1)
        out.error = QStringLiteral("refused (status %1)").arg(value.size() == 1 ? int(value[0]) : -1);
        if (const std::optional<Der::Node> text = s.next(Der::Sequence)) {
            Der::Reader texts(text->content);
            if (const std::optional<Der::Node> t = texts.next())
                out.error += QStringLiteral(": ") + QString::fromUtf8(t->content.toByteArray());
        }
        return out;
    }
    const std::optional<Der::Node> token = r.next(Der::Sequence);
    if (!token || !signedData(token->whole)) {
        out.error = QStringLiteral("no token");
        return out;
    }
    out.granted = true;
    out.token = token->whole.toByteArray();
    return out;
}

std::optional<Info> info(QByteArrayView token)
{
    const std::optional<SignedData> sd = signedData(token);
    return sd ? parseTstInfo(sd->tstInfo) : std::nullopt;
}

QList<QByteArray> certificates(QByteArrayView token)
{
    QList<QByteArray> out;
    const std::optional<SignedData> sd = signedData(token);
    if (!sd || !sd->certificates)
        return out;
    Der::Reader r(sd->certificates->content);
    while (const std::optional<Der::Node> c = r.next())
        if (c->tag == Der::Sequence)
            out.append(c->whole.toByteArray());
    return out;
}

QByteArray withoutCertificates(QByteArrayView token)
{
    const std::optional<SignedData> sd = signedData(token);
    if (!sd || !sd->certificates)
        return token.toByteArray();
    // Rebuild SignedData without the [0] element, then the [0] EXPLICIT and ContentInfo around it.
    const std::optional<Der::Node> top = Der::parse(token);
    Der::Reader ci(top->content);
    const std::optional<Der::Node> type = ci.next(Der::Oid), explicitContent = ci.next(Der::context(0));
    const std::optional<Der::Node> signed_ = Der::parse(explicitContent->content);
    QByteArray body;
    Der::Reader r(signed_->content);
    while (const std::optional<Der::Node> n = r.next())
        if (n->whole.data() != sd->certificates->whole.data())
            body += n->whole;
    const QByteArray content = Der::tlv(Der::Sequence, body);
    return Der::tlv(Der::Sequence, type->whole.toByteArray() + Der::tlv(Der::context(0), content));
}

Check verify(QByteArrayView token, const QList<QByteArray> &pool, Info *out)
{
    const std::optional<SignedData> sd = signedData(token);
    if (!sd)
        return Check::Malformed;
    std::optional<Info> tst = parseTstInfo(sd->tstInfo);
    if (!tst)
        return Check::Malformed;

    QList<Certificate> certs;
    QSet<QByteArray> seen;
    const auto add = [&](const QByteArray &der) {
        if (seen.contains(der))
            return;
        seen.insert(der);
        if (std::optional<Certificate> c = parseCertificate(der))
            certs.append(std::move(*c));
    };
    for (const QByteArray &der : certificates(token))
        add(der);
    for (const QByteArray &der : pool)
        add(der);

    // SignerInfo: sid, digest algorithm, signed attributes, signature.
    Der::Reader si(sd->signerInfo->content);
    const std::optional<Der::Node> version = si.next(Der::Integer);
    std::optional<Der::Node> sid = si.next(Der::Sequence);   // IssuerAndSerialNumber
    std::optional<Der::Node> keyId;
    if (!sid)
        keyId = si.next(Der::context(0, false));            // [0] SubjectKeyIdentifier
    const std::optional<Der::Node> digestAlg = si.next(Der::Sequence), attrs = si.next(Der::context(0)),
                                   sigAlg = si.next(Der::Sequence), signature = si.next(Der::OctetString);
    if (!version || (!sid && !keyId) || !digestAlg || !attrs || !sigAlg || !signature)
        return Check::Malformed;
    const std::optional<QCryptographicHash::Algorithm> digest = digestOf(algorithmOid(*digestAlg));
    if (!digest)
        return Check::BadSignature;
    static const QByteArray rsa = Der::oid("1.2.840.113549.1.1.1");
    const QByteArrayView sigOid = algorithmOid(*sigAlg);
    std::optional<QCryptographicHash::Algorithm> sigDigest = sigOid == rsa ? digest : digestOf(sigOid);
    if (!sigDigest)
        return Check::BadSignature;

    // contentType = TSTInfo and messageDigest = digest(TSTInfo) among the signed attributes.
    bool typeOk = false, digestOk = false;
    Der::Reader a(attrs->content);
    while (const std::optional<Der::Node> attr = a.next(Der::Sequence)) {
        Der::Reader x(attr->content);
        const std::optional<Der::Node> type = x.next(Der::Oid), values = x.next(Der::Set);
        if (!type || !values)
            return Check::Malformed;
        const std::optional<Der::Node> value = Der::parse(values->content);
        if (type->content == oidContentType())
            typeOk = value && value->tag == Der::Oid && value->content == oidTstInfo();
        else if (type->content == oidMessageDigest())
            digestOk = value && value->tag == Der::OctetString
                       && value->content == QCryptographicHash::hash(sd->tstInfo, *digest);
    }
    if (!typeOk || !digestOk)
        return Check::BadSignature;

    const Certificate *signer = nullptr;
    QByteArrayView issuerName, serial;
    if (sid) {
        Der::Reader s(sid->content);
        const std::optional<Der::Node> issuer = s.next(Der::Sequence), number = s.next(Der::Integer);
        if (!issuer || !number)
            return Check::Malformed;
        issuerName = issuer->content;
        serial = Der::unsignedInteger(*number);
    }
    for (const Certificate &c : certs)
        if (sid ? c.issuer == issuerName && c.serial == serial : c.subjectKeyId == keyId->content) {
            signer = &c;
            break;
        }
    if (!signer)
        return Check::UnknownSigner;

    // The signature is over the DER of the attributes as a SET (the [0] tag replaced).
    QByteArray signedAttrs = attrs->whole.toByteArray();
    signedAttrs[0] = char(Der::Set);
    QCryptographicHash cacheKey(QCryptographicHash::Sha256);
    cacheKey.addData(token);
    cacheKey.addData(signer->der);
    const QByteArray key = cacheKey.result();
    bool known;
    {
        const QMutexLocker lock(&g_cacheMutex);
        known = g_goodTokens.contains(key);
    }
    if (!known) {
        if (!signatureValid(signer->spki, *sigDigest, signedAttrs, signature->content))
            return Check::BadSignature;
        if (!signer->timeStamping || !trusted(*signer, certs, tst->timeMs))
            return Check::Untrusted;
        const QMutexLocker lock(&g_cacheMutex);
        g_goodTokens.insert(key);
    }
    tst->authority = signer->commonName;
    if (out)
        *out = *tst;
    return Check::Ok;
}

void clearCache()
{
    const QMutexLocker lock(&g_cacheMutex);
    g_signatures.clear();
    g_goodTokens.clear();
}

} // namespace TimeStamp
