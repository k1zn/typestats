#include "Stamps.h"

#include "TimeStamp.h"

#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>

namespace Stamps {

namespace {

// The marks that may be put on records after they were typed: they do not break the stamps.
constexpr quint32 kLaterMarks = KeyRecord::Transient | KeyRecord::Marked | KeyRecord::SegmentStart;

QByteArray imprintOf(const QByteArray &token)
{
    const std::optional<TimeStamp::Info> info = TimeStamp::info(token);
    return info ? info->imprint : QByteArray();
}

QString shortName(const QString &commonName)
{
    for (const char *name : {"DigiCert", "Sectigo", "GlobalSign"})
        if (commonName.contains(QLatin1String(name), Qt::CaseInsensitive))
            return QLatin1String(name);
    return commonName;
}

} // namespace

QByteArray encode(const KeyRecord &r)
{
    QByteArray out(12, Qt::Uninitialized);
    uchar *p = reinterpret_cast<uchar *>(out.data());
    qToLittleEndian<quint32>(r.dtUs, p);
    qToLittleEndian<quint32>(r.flags & ~kLaterMarks, p + 4);
    qToLittleEndian<quint32>(r.ch, p + 8);
    return out;
}

QByteArray chunkHash(QByteArrayView previousImprint, int length, quint32 delayMs, QByteArrayView encodedRecords)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QByteArrayView("TypingStatistics stamps 1"));
    const char size = char(previousImprint.size());
    h.addData(QByteArrayView(&size, 1));
    h.addData(previousImprint);
    uchar counts[8];
    qToLittleEndian<quint32>(quint32(length), counts);
    qToLittleEndian<quint32>(delayMs, counts + 4);
    h.addData(QByteArrayView(reinterpret_cast<const char *>(counts), sizeof counts));
    h.addData(encodedRecords);
    return h.result();
}

void Chain::reset()
{
    m_normalizer = {};
    m_encoded.clear();
}

bool Chain::push(const KeyRecord &raw)
{
    const std::optional<KeyRecord> r = m_normalizer.push(raw);
    if (!r)
        return false;
    m_encoded += encode(*r);
    return true;
}

QByteArray Chain::hash(QByteArrayView previousImprint, int from, int to, quint32 delayMs) const
{
    return chunkHash(previousImprint, to - from, delayMs, QByteArrayView(m_encoded).sliced(from * 12, (to - from) * 12));
}

Report verify(const KeyRecords &records, const QList<Stamp> &stamps, const QList<QByteArray> &certificates)
{
    Report rep;
    rep.records = int(records.size());
    rep.stamps = int(stamps.size());
    for (const KeyRecord &r : records)
        if (r.isDown() && (r.flags & KeyRecord::Injected))
            ++rep.injected;
    if (stamps.isEmpty())
        return rep;

    const qsizetype n = records.size();
    QByteArray encoded;
    encoded.reserve(n * 12);
    QVector<qint64> timeUs(n); // of each record since the first
    for (qsizetype i = 0; i < n; ++i) {
        encoded += encode(records[i]);
        timeUs[i] = i == 0 ? 0 : timeUs[i - 1] + records[i].dtUs;
    }

    QVector<char> confirmed(n, 0);
    QSet<QString> authorities;
    QByteArray previous;
    int prevEnd = 0;
    bool structure = true;  // the ends go up and stay within the records
    int lastGood = -1;      // the stamp the time is followed from, in the current band
    qint64 bandLo = 0, bandHi = 0;
    for (const Stamp &s : stamps) {
        bool good = false;
        if (structure && (s.voided ? s.end >= prevEnd : s.end > prevEnd) && s.end <= n) {
            TimeStamp::Info info;
            const bool tokenOk = TimeStamp::verify(s.token, certificates, &info) == TimeStamp::Check::Ok && info.sha256;
            if (s.voided) {
                ++rep.voided;
                if (!tokenOk)
                    ++rep.bad;
            } else if (tokenOk
                       && info.imprint
                              == chunkHash(previous, s.end - prevEnd, s.delayMs,
                                           QByteArrayView(encoded).sliced(prevEnd * 12, (s.end - prevEnd) * 12))) {
                good = true;
                rep.stamped += s.end - prevEnd;
                authorities.insert(shortName(info.authority));
                if (rep.firstMs == 0)
                    rep.firstMs = info.timeMs;
                rep.lastMs = info.timeMs;
                // The time of the stamp against the time of its last record: in one band they differ by about
                // the same (the delay of the network), whatever the pauses.
                const qint64 offset = info.timeMs - timeUs[s.end - 1] / 1000 - qint64(s.delayMs);
                const qint64 lo = std::min(bandLo, offset), hi = std::max(bandHi, offset);
                if (lastGood >= 0 && hi - lo <= kToleranceMs) {
                    std::fill(confirmed.begin() + (lastGood - 1), confirmed.begin() + s.end, 1);
                    bandLo = lo;
                    bandHi = hi;
                } else {
                    bandLo = bandHi = offset;
                }
                rep.driftMs = std::max(rep.driftMs, bandHi - bandLo);
                lastGood = s.end;
            } else {
                ++rep.bad;
            }
            prevEnd = s.end;
        } else {
            structure = false;
            ++rep.bad;
        }
        if (!good)
            lastGood = -1; // the time is not followed across a stamp that does not check
        previous = imprintOf(s.token);
    }
    rep.confirmed = int(std::count(confirmed.begin(), confirmed.end(), 1));
    rep.authorities = QStringList(authorities.begin(), authorities.end());
    rep.authorities.sort();
    if (rep.bad > 0)
        rep.status = Report::Status::Broken;
    else if (rep.confirmed == rep.records)
        rep.status = Report::Status::Confirmed;
    else
        rep.status = Report::Status::Partial;
    return rep;
}

void follow(QList<Stamp> &stamps, const KeyRecords &edited)
{
    if (stamps.isEmpty())
        return;
    // The new end of a stamp: the records left of those it covered (tags are old indices + 1, in order).
    QVector<quint32> tags;
    tags.reserve(edited.size());
    for (const KeyRecord &r : edited)
        tags.append(r.tag);
    for (Stamp &s : stamps)
        s.end = int(std::upper_bound(tags.begin(), tags.end(), quint32(s.end)) - tags.begin());
    // A stamp whose records changed is voided; it stays as the link of the chain.
    QByteArray encoded;
    encoded.reserve(edited.size() * 12);
    for (const KeyRecord &r : edited)
        encoded += encode(r);
    QByteArray previous;
    int prevEnd = 0;
    for (Stamp &s : stamps) {
        if (!s.voided
            && (s.end <= prevEnd
                || imprintOf(s.token)
                       != chunkHash(previous, s.end - prevEnd, s.delayMs,
                                    QByteArrayView(encoded).sliced(prevEnd * 12, (s.end - prevEnd) * 12))))
            s.voided = true;
        s.end = std::max(s.end, prevEnd);
        prevEnd = s.end;
        previous = imprintOf(s.token);
    }
}

void addCertificates(QList<QByteArray> &pool, const QList<QByteArray> &certificates)
{
    for (const QByteArray &c : certificates)
        if (!pool.contains(c))
            pool.append(c);
}

} // namespace Stamps
