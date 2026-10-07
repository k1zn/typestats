#pragma once

#include "KeyName.h"
#include "KeyRecord.h"

#include <QStringList>

// A time stamp of a recording (re/stamps.md): it covers the normalized records up to `end`, requested `delayMs` after
// the last of them; the token is the RFC 3161 one, without its certificates (they are kept once per file).
struct Stamp
{
    int end = 0;
    quint32 delayMs = 0;
    bool voided = false; // its records were edited here afterwards: it only links the chain
    QByteArray token;
};

// Contents of a .tsf file (see re/tsf_format.md).
struct TsfDocument
{
    KeyRecords records;
    int version = 1;
    QString author;          // "autor"
    QString comment;
    QString date;
    QString fingerZonesName; // empty = not stored
    QStringList fingers;     // Finger0..Finger7
    QString attachedVideo;
    int videoTimeShiftMs = 0;
    KeyPlatform platform = KeyPlatform::Windows; // "Platform": where it was recorded; none - Windows
    QList<Stamp> stamps;                    // "Stamp1", "Stamp2"...: the port's own, not signed
    QList<QByteArray> stampCertificates;    // "StampCert1"...: the certificates of the authorities, DER
    QByteArray webcam;                      // "Webcam", "WebcamData1"...: the webcam recording (MediaClip, re/webcam.md)
    bool webcamDamaged = false;             // its lines were there but did not add up (it is dropped)
    bool signed_ = false;     // file had a signature line
    bool signatureValid = true;
};

namespace Tsf {
constexpr int kCurrentVersion = 1;

enum class ReadError { None, CannotOpen, NewerVersion };

// Parses a .tsf file. NewerVersion is reported but the data is still loaded,
// as the original does after its warning.
ReadError read(const QString &path, TsfDocument &doc);
bool write(const QString &path, const TsfDocument &doc, bool sign);

// Text-level helpers, exposed for tests.
TsfDocument parse(const QStringList &lines, ReadError *err = nullptr);
QStringList serialize(const TsfDocument &doc, bool sign);
QString dataLine(const KeyRecord &r);
}
