#pragma once

#include "MediaClip.h"
#include "Recalc.h"
#include "TimeStamp.h"
#include "TsfFile.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

#include <optional>

// Time stamps of a recording (re/stamps.md): while recording, a chain of hashes of the normalized records goes to
// RFC 3161 time stamping authorities; the tokens show that the records existed at that time and were not changed
// afterwards, and the time between them shows that they were typed in real time. The chain v3 hashes the records,
// each with a salt of its own, in leaves of groups, and the packets of the webcam's video too: a block keeps its
// stamps with the records around it as hashes no one can reverse, and the video is stamped with the keys.
namespace Stamps {

constexpr int kIntervalMs = 10000;     // a stamp at the first key this long after the last stamped one
constexpr int kIdleMs = 2000;          // and this long after the last key
constexpr qint64 kToleranceMs = 2500;  // how far the time of the stamps may wander from the time of the records
constexpr int kGroup = 8;              // records per leaf (v3)
constexpr int kVersion = 3;            // of the chain the stamps are taken with
constexpr int kKeySize = 16, kSaltSize = 16;

// A normalized record as it is hashed: dt, flags (without the marks that may be set later), character - 3 × u32 LE.
QByteArray encode(const KeyRecord &normalized);
// The imprint of a stamp v1: the previous stamp's imprint (empty for the first), the number of records it covers, the
// delay of the request, the records.
QByteArray chunkHash(QByteArrayView previousImprint, int length, quint32 delayMs, QByteArrayView encodedRecords);

// v3: the salt of record j of a chunk from its stamp's key; the hash of a salted record.
QByteArray salt(QByteArrayView key, quint32 j);
QByteArray commitment(QByteArrayView salt, QByteArrayView encodedRecord);
// A leaf - a group of records (count, the sum of their dt, the hash of their commitments).
QByteArray leaf(int count, quint64 durationUs, QByteArrayView groupHash);
// The leaves of the encoded records of a chunk (groups of kGroup from the first), hashed together: H_rec.
QByteArray recordsHash(QByteArrayView encodedRecords, QByteArrayView key);
// SHA-256 of a packet as the clip stores it (MediaClip::packetBytes).
QByteArray packetHash(const MediaPacket &p);
// The imprint of a stamp v3.
QByteArray chunkHashV3(QByteArrayView previousImprint, int records, quint32 delayMs, QByteArrayView recordsHash,
                       int packets, QByteArrayView packetHashes);

// The normalized records of a recording as they come, encoded for hashing.
class Chain
{
public:
    void reset();
    // A raw record of the document; true if the normalization keeps it.
    bool push(const KeyRecord &raw);
    int size() const { return int(m_encoded.size() / 12); }
    // Time of a record since the first one, by the records' dt.
    qint64 timeUs(int i) const { return m_timeUs[i]; }
    QByteArray hash(QByteArrayView previousImprint, int from, int to, quint32 delayMs) const;
    // v3: with the stamp's key and the hashes of the packets of the chunk.
    QByteArray hashV3(QByteArrayView previousImprint, int from, int to, quint32 delayMs, QByteArrayView key,
                      QByteArrayView packetHashes, int packets) const;

private:
    Recalc::Normalizer m_normalizer;
    QByteArray m_encoded;
    QVector<qint64> m_timeUs;
};

struct Report
{
    enum class Status { None, Confirmed, Partial, Broken };
    Status status = Status::None;
    int stamps = 0;
    int bad = 0;             // stamps that do not match the records or whose token does not check
    int voided = 0;          // stamps whose records were edited in the program afterwards
    int unknown = 0;         // stamps of a version this program does not check (newer, or the unreleased v2)
    int records = 0;
    int stamped = 0;         // records under good stamps
    int confirmed = 0;       // records whose time the stamps confirm
    qint64 firstMs = 0, lastMs = 0; // UTC time of the first and the last good stamp
    qint64 driftMs = 0;      // the largest spread of the stamps' time against the records' time
    int injected = 0;        // presses the system marked as synthetic (SendInput, uinput...)
    QStringList authorities;
    // The webcam's video (chain v3).
    int packets = 0;         // of the clip
    int packetsStamped = 0;  // under good stamps
    int packetsLate = 0;     // timed after the stamp that holds them (such stamps are bad)
    // After the last stamp: while recording they wait for the next one (not missing yet).
    int pendingRecords = 0, pendingPackets = 0;
    // A block: time of the recording outside it, as its stamps tell.
    qint64 hiddenBeforeUs = 0, hiddenAfterUs = 0;
};

// The checks of the stamps verify() has done (a stamp v3 hashes each record twice - 1 µs a record - and its token
// takes an RSA operation or more). Valid while the document only grows: the caller clears it when its records or video
// are replaced (an edit, another document).
struct Cache
{
    struct Entry
    {
        QByteArray token, previous;
        int end = 0, mediaEnd = 0, recordCursor = 0, packetCursor = 0;
        qsizetype parts = 0;
        bool clip = false;
        bool tokenOk = false, matches = false;
        TimeStamp::Info info;
    };
    QList<Entry> entries;
    void clear() { entries.clear(); }
};

// The document's records (normalized), its stamps and the authorities' certificates; the clip of its webcam and the
// model time of its packets (pts − originUs − modelOffsetUs, DocTime::modelOffset of the raw records); the raw dt of
// the first record (a block: the chain had it, not the normalization's 60 s).
Report verify(const KeyRecords &normalized, const QList<Stamp> &stamps, const QList<QByteArray> &certificates,
              const MediaClip *clip = nullptr, qint64 clipModelOffsetUs = 0, std::optional<quint32> firstDtUs = {},
              Cache *cache = nullptr);

// After an edit of the normalized records whose `tag` was set to index + 1 before it: the stamps follow their records
// (an edit only drops records and keeps the order), and those whose records changed are voided. A voided stamp v3
// keeps its records as leaves (from `before`): its token still stamps the video.
void follow(QList<Stamp> &stamps, const KeyRecords &before, const KeyRecords &edited);

// The stamps of a block: the normalized records [from, to) and the packets of the clip that the cut kept (their indexes
// in `clip`, in order). Records and packets outside the block become hidden parts (v3: hashes of salted records).
// `revealed` - records outside the block that go into the file as they are (v1 stamps hash their chunk in a row).
QList<Stamp> extract(const QList<Stamp> &stamps, const KeyRecords &normalized, const MediaClip *clip, int from, int to,
                     const QList<int> &keptPackets, int *revealed = nullptr, std::optional<quint32> firstDtUs = {});

// The certificates of a token that the pool does not have yet.
void addCertificates(QList<QByteArray> &pool, const QList<QByteArray> &certificates);

} // namespace Stamps
