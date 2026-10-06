#pragma once

#include "Recalc.h"
#include "TsfFile.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

// Time stamps of a recording (re/stamps.md): while recording, a chain of hashes of the normalized records goes to
// RFC 3161 time stamping authorities; the tokens show that the records existed at that time and were not changed
// afterwards, and the time between them shows that they were typed in real time.
namespace Stamps {

constexpr int kIntervalMs = 10000;     // a stamp at the first key this long after the last stamped one
constexpr int kIdleMs = 2000;          // and this long after the last key
constexpr qint64 kToleranceMs = 2500;  // how far the time of the stamps may wander from the time of the records

// A normalized record as it is hashed: dt, flags (without the marks that may be set later), character - 3 × u32 LE.
QByteArray encode(const KeyRecord &normalized);
// The imprint of a stamp: the previous stamp's imprint (empty for the first), the number of records it covers, the
// delay of the request, the records.
QByteArray chunkHash(QByteArrayView previousImprint, int length, quint32 delayMs, QByteArrayView encodedRecords);

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
    int records = 0;
    int stamped = 0;         // records under good stamps
    int confirmed = 0;       // records whose time the stamps confirm
    qint64 firstMs = 0, lastMs = 0; // UTC time of the first and the last good stamp
    qint64 driftMs = 0;      // the largest spread of the stamps' time against the records' time
    int injected = 0;        // presses the system marked as synthetic (SendInput, uinput...)
    QStringList authorities;
};

Report verify(const KeyRecords &normalized, const QList<Stamp> &stamps, const QList<QByteArray> &certificates);

// After an edit of the normalized records whose `tag` was set to index + 1 before it: the stamps follow their records
// (an edit only drops records and keeps the order), and those whose records changed are voided.
void follow(QList<Stamp> &stamps, const KeyRecords &edited);

// The certificates of a token that the pool does not have yet.
void addCertificates(QList<QByteArray> &pool, const QList<QByteArray> &certificates);

} // namespace Stamps
