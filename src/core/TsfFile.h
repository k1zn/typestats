#pragma once

#include "KeyName.h"
#include "KeyRecord.h"

#include <QStringList>

// A part of the chunk of a stamp of the chain v2 that is not simply "the next records / packets of the document"
// (re/stamps.md, "Скрытые части"): a block keeps the stamps of the recording it was cut from.
struct StampPart
{
    enum Kind : quint8 {
        Records,      // "R<k>[:<z85>]": the next k records of the document; v3 without a key - their salts
        Unstamped,    // "U<k>": the next k records of the document are not under this stamp (it was voided)
        Hidden,       // "H<z85>": v1 - records not in the document, 12 bytes each
        Leaf,         // "L<count>:<duration>:<hash>": a whole group of records not in the document
        Commitments,  // "C<count>:<duration>:<z85>": v3 - records of a group cut by a block's edge, as hashes
        Packets,      // "V<k>": the next k packets of the document's video
        HiddenPacket, // "W<hash>": a packet not in the document
    };
    Kind kind = Records;
    int count = 0;           // Records, Unstamped, Packets; Leaf, Commitments: records
    quint64 durationUs = 0;  // Leaf, Commitments: the sum of their dt
    // Records: their salts (16 bytes each) or none; Hidden: the records; Leaf: the hash of the group; Commitments:
    // the hashes of the records (32 bytes each); HiddenPacket: SHA-256 of the packet.
    QByteArray data;
    bool operator==(const StampPart &) const = default;
};

// A time stamp of a recording (re/stamps.md): it covers the normalized records up to `end`, requested `delayMs` after
// the last of them; the token is the RFC 3161 one, without its certificates (they are kept once per file).
struct Stamp
{
    int end = 0;
    quint32 delayMs = 0;
    bool voided = false; // its records were edited here afterwards: it only links the chain
    QByteArray token;
    // The chain: 1 - records hashed in a row (1.1.0); 3 - salted records in leaves, and video packets. Others (2: of
    // the days before 1.2, never released; newer ones) are not checked.
    int version = 1;
    int mediaEnd = 0;    // v3: the packets of the document's clip up to this one
    QByteArray key;      // v3: the key of the records' salts (16 bytes); none in a block that hides some of them
    QList<StampPart> parts;  // v3 (and v1 in a block): empty - the chunk is all in the document
    QByteArray previous;     // the imprint of the stamp before when it is not in the file (the first stamp of a block)
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
