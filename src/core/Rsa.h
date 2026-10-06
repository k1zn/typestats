#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>

#include <optional>

// Verification of RSA signatures (RSASSA-PKCS1-v1_5, RFC 8017) - only checking, no private keys, so no secrets
// and no timing concerns. Enough for the time stamps of a recording (Stamps, TimeStamp).
namespace Rsa {

struct PublicKey
{
    QByteArray modulus;  // big-endian, no leading zeros
    QByteArray exponent;
};

// SubjectPublicKeyInfo (X.509) with an rsaEncryption key.
std::optional<PublicKey> fromSpki(QByteArrayView spki);

// signature^e mod n, as many bytes as the modulus; nothing if the signature is not below the modulus.
std::optional<QByteArray> publicOperation(const PublicKey &key, QByteArrayView signature);

// The signature is EMSA-PKCS1-v1_5 of this digest (DigestInfo with or without the NULL parameters).
bool verify(const PublicKey &key, QCryptographicHash::Algorithm alg, QByteArrayView digest, QByteArrayView signature);

} // namespace Rsa
