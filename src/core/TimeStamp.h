#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

// RFC 3161 time stamps: the request, the response, and the check of a token (CMS SignedData over TSTInfo, signed by
// a time stamping authority whose certificate leads to one of the roots pinned here). Only RSA signatures and
// SHA-2 - the services the program uses (DigiCert, Sectigo, GlobalSign, Certum, SwissSign, Microsoft) sign so.
namespace TimeStamp {

// TimeStampReq for a SHA-256 imprint.
QByteArray request(QByteArrayView sha256, quint64 nonce, bool withCertificates);

struct Response
{
    bool granted = false;
    QByteArray token; // ContentInfo, as the authority sent it
    QString error;
};
Response parseResponse(QByteArrayView der);

struct Info
{
    QByteArray imprint;      // the hash that was stamped
    bool sha256 = false;     // of that algorithm
    qint64 timeMs = 0;       // genTime, ms since 1970 UTC
    QByteArray nonce;        // as an unsigned big-endian number
    QString authority;       // the common name of the signer's certificate (empty if it is not known)
};

// TSTInfo of a token, not checked.
std::optional<Info> info(QByteArrayView token);
// The certificates the token carries.
QList<QByteArray> certificates(QByteArrayView token);
// The same token without its certificates (they are not signed; kept once per file instead).
QByteArray withoutCertificates(QByteArrayView token);

enum class Check {
    Ok,
    Malformed,      // not a time stamp token
    BadSignature,   // the content does not match the signature
    UnknownSigner,  // the signer's certificate is not among those given
    Untrusted,      // its chain does not lead to a pinned root, or a certificate in it is not valid at that time
};
// Checks the token with the given certificates (from the token itself and from the file); fills `out` on Ok.
Check verify(QByteArrayView token, const QList<QByteArray> &certificates, Info *out = nullptr);

// Checks of certificates and signatures are cached: the stamps of a recording share them.
void clearCache();

} // namespace TimeStamp
