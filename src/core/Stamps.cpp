#include "Stamps.h"

#include "TimeStamp.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <functional>
#include <limits>

namespace Stamps {

namespace {

// The marks that may be put on records after they were typed: they do not break the stamps.
constexpr quint32 kLaterMarks = KeyRecord::Transient | KeyRecord::Marked | KeyRecord::SegmentStart;

QByteArray sha256(QByteArrayView data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

// SHA-256 of two pieces into 32 bytes at `out`, with nothing allocated (the salts: two hashes for each record).
void sha256Into(char *out, QByteArrayView a, QByteArrayView b)
{
    const QByteArrayView parts[] = {a, b};
    QCryptographicHash::hashInto(QSpan<char>(out, 32), QSpan<const QByteArrayView>(parts), QCryptographicHash::Sha256);
}

QByteArray imprintOf(const QByteArray &token)
{
    const std::optional<TimeStamp::Info> info = TimeStamp::info(token);
    return info ? info->imprint : QByteArray();
}

QString shortName(const QString &commonName)
{
    for (const char *name : {"DigiCert", "Sectigo", "GlobalSign", "Certum", "SwissSign", "Microsoft"})
        if (commonName.contains(QLatin1String(name), Qt::CaseInsensitive))
            return QLatin1String(name);
    return commonName;
}

quint32 dtOf(QByteArrayView encoded)
{
    return qFromLittleEndian<quint32>(encoded.data());
}

// A record of the chunk of a stamp as the chain had it.
struct RecItem
{
    enum Kind { Present, Unstamped, Hidden, Leaf, Commitment } kind = Present;
    int doc = -1;           // Present, Unstamped: the record of the document
    QByteArray encoded;     // Present, Hidden: 12 bytes; Leaf: the hash of its group; Commitment: the record's hash
    int count = 1;          // Leaf: records of the group
    quint64 durationUs = 0; // Leaf; Commitment: the dt of the hidden records up to this one (on the last of a part)
    QByteArray salt;        // Present, v3
};

bool isVersionChecked(int version)
{
    return version == 1 || version == kVersion;
}

quint64 durationOf(const RecItem &r);

struct PacketItem
{
    int doc = -1;      // a packet of the document's clip, or -1
    QByteArray hash;   // its SHA-256 (given for a hidden one)
};

// A stamp laid out over the document: what its chunk is made of.
struct Layout
{
    bool ok = true;
    QList<RecItem> records;
    QList<PacketItem> packets;
    int recordsEnd = 0, packetsEnd = 0;  // the document's cursors after it
    int present = 0;                     // its records in the document (not unstamped)
};

// The encodings of the document's records for the chain. Record 0 is the chain's first (its dt is the 60 s of the
// normalization) unless records not in the document come before it: then it is a block's, with the dt the chain had.
class Records
{
public:
    Records(const KeyRecords &normalized, const QList<Stamp> &stamps, std::optional<quint32> firstDtUs)
    {
        m_encoded.reserve(normalized.size() * 12);
        for (const KeyRecord &r : normalized)
            m_encoded += encode(r);
        const auto hiddenFirst = [&stamps] {
            for (const Stamp &s : stamps) {
                if (s.parts.isEmpty()) {
                    if (s.end > 0)
                        return false;
                    continue;
                }
                for (const StampPart &p : s.parts) {
                    if (p.kind == StampPart::Hidden || p.kind == StampPart::Leaf || p.kind == StampPart::Commitments)
                        return true;
                    if (p.kind == StampPart::Records || p.kind == StampPart::Unstamped)
                        return false;
                }
            }
            return false;
        };
        if (firstDtUs && !m_encoded.isEmpty() && hiddenFirst())
            qToLittleEndian<quint32>(*firstDtUs, m_encoded.data());
    }
    int size() const { return int(m_encoded.size() / 12); }
    QByteArrayView at(int i) const { return QByteArrayView(m_encoded).sliced(qsizetype(i) * 12, 12); }

private:
    QByteArray m_encoded;
};

Layout layOut(const Stamp &s, int recordCursor, int packetCursor, const Records &records, const MediaClip *clip)
{
    Layout l;
    const int n = records.size();
    const int clipPackets = clip ? int(clip->packets.size()) : 0;
    int j = 0; // the number of the next record of the chunk (v3 salts)
    auto present = [&](int count, RecItem::Kind kind, const QByteArray &salts = {}) {
        for (int i = 0; i < count; ++i, ++recordCursor) {
            if (recordCursor >= n) {
                l.ok = false;
                return;
            }
            RecItem item;
            item.kind = kind;
            item.doc = recordCursor;
            if (kind == RecItem::Present) {
                item.encoded = records.at(recordCursor).toByteArray();
                if (s.version >= 2)
                    item.salt = !salts.isEmpty() ? salts.mid(qsizetype(i) * kSaltSize, kSaltSize)
                                : !s.key.isEmpty() ? salt(s.key, quint32(j)) : QByteArray();
                ++l.present;
                ++j;
            }
            l.records.append(item);
        }
    };
    auto packets = [&](int count) {
        for (int i = 0; i < count; ++i, ++packetCursor) {
            if (!clip) { // only counted (follow: an edit of the records does not touch the video)
                l.packets.append(PacketItem{packetCursor, QByteArray()});
                continue;
            }
            if (packetCursor >= clipPackets) {
                l.ok = false;
                return;
            }
            l.packets.append({packetCursor, packetHash(clip->packets[packetCursor])});
        }
    };
    if (s.parts.isEmpty()) {
        if (s.end < recordCursor)
            l.ok = false;
        else
            present(s.end - recordCursor, RecItem::Present);
        if (s.version >= 2) {
            if (s.mediaEnd < packetCursor)
                l.ok = false;
            else
                packets(s.mediaEnd - packetCursor);
        }
    } else {
        for (const StampPart &p : s.parts) {
            switch (p.kind) {
            case StampPart::Records:
                present(p.count, RecItem::Present, p.data);
                break;
            case StampPart::Unstamped:
                present(p.count, RecItem::Unstamped);
                break;
            case StampPart::Hidden:
                if (p.data.isEmpty() || p.data.size() % 12) {
                    l.ok = false;
                    break;
                }
                for (qsizetype i = 0; i < p.data.size(); i += 12)
                    l.records.append({RecItem::Hidden, -1, p.data.mid(i, 12), 1, 0, {}});
                break;
            case StampPart::Leaf:
                if (s.version < 2 || p.count < 1 || p.count > kGroup || p.data.size() != 32) {
                    l.ok = false;
                    break;
                }
                l.records.append({RecItem::Leaf, -1, p.data, p.count, p.durationUs, {}});
                j += p.count;
                break;
            case StampPart::Commitments:
                if (s.version < 2 || p.count < 1 || p.count > kGroup || p.data.size() != qsizetype(p.count) * 32) {
                    l.ok = false;
                    break;
                }
                for (int i = 0; i < p.count; ++i)
                    l.records.append({RecItem::Commitment, -1, p.data.mid(qsizetype(i) * 32, 32), 1,
                                      i == p.count - 1 ? p.durationUs : 0, {}});
                j += p.count;
                break;
            case StampPart::Packets:
                if (s.version < 2)
                    l.ok = false;
                else
                    packets(p.count);
                break;
            case StampPart::HiddenPacket:
                if (s.version < 2 || p.data.size() != 32)
                    l.ok = false;
                else
                    l.packets.append({-1, p.data});
                break;
            }
        }
        if (recordCursor != s.end || (s.version >= 2 && packetCursor != s.mediaEnd))
            l.ok = false;
    }
    l.recordsEnd = recordCursor;
    l.packetsEnd = packetCursor;
    return l;
}

// The imprint the chunk laid out gives (empty when it cannot be built: a leaf not at the start of a group).
QByteArray imprint(const Stamp &s, const Layout &l, QByteArrayView previous)
{
    if (s.version < 2) {
        QByteArray encoded;
        int n = 0;
        for (const RecItem &r : l.records)
            if (r.kind == RecItem::Present || r.kind == RecItem::Hidden) {
                encoded += r.encoded;
                ++n;
            }
        return chunkHash(previous, n, s.delayMs, encoded);
    }
    QByteArray leaves, group;
    int inGroup = 0, n = 0;
    quint64 duration = 0;
    auto flush = [&] {
        if (inGroup) {
            leaves += leaf(inGroup, duration, sha256(group));
            group.clear();
            inGroup = 0;
            duration = 0;
        }
    };
    for (const RecItem &r : l.records) {
        if (r.kind == RecItem::Unstamped)
            continue;
        if (r.kind == RecItem::Leaf) {
            if (inGroup)
                return {};
            leaves += leaf(r.count, r.durationUs, r.encoded);
            n += r.count;
            continue;
        }
        if (r.kind == RecItem::Hidden || (r.kind == RecItem::Present && r.salt.size() != kSaltSize))
            return {}; // a record of v1 or one with no salt: not of this chain
        group += r.kind == RecItem::Commitment ? r.encoded : commitment(r.salt, r.encoded);
        duration += durationOf(r);
        ++n;
        if (++inGroup == kGroup)
            flush();
    }
    flush();
    QByteArray packets;
    for (const PacketItem &p : l.packets)
        packets += p.hash;
    return chunkHashV3(previous, n, s.delayMs, sha256(leaves), int(l.packets.size()), packets);
}

quint64 durationOf(const RecItem &r)
{
    switch (r.kind) {
    case RecItem::Leaf:
    case RecItem::Commitment: return r.durationUs;
    case RecItem::Unstamped: return 0;
    default: return dtOf(r.encoded);
    }
}

// The hash of a record of a chunk v3 as its group takes it.
QByteArray commitmentOf(const RecItem &r)
{
    return r.kind == RecItem::Commitment ? r.encoded : commitment(r.salt, r.encoded);
}

// The parts of a chunk whose records `keep` says are in the new document (their new indexes follow in order) and
// whose packets `keepPacket` says are. v3: whole hidden groups become leaves, the hidden records of a group cut by the
// edge their hashes; `withSalts` - the records kept carry their salts (the new stamp has no key). v1: the records
// not kept go as they are.
QList<StampPart> partsOf(const Stamp &s, const Layout &l, const std::function<bool(const RecItem &)> &keep,
                         const std::function<bool(const PacketItem &)> &keepPacket, bool withSalts, int *revealed)
{
    QList<StampPart> parts;
    auto add = [&parts](StampPart::Kind kind, int count, const QByteArray &data = {}) {
        if (!parts.isEmpty() && parts.last().kind == kind
            && (kind == StampPart::Records || kind == StampPart::Hidden || kind == StampPart::Packets)) {
            parts.last().count += count;
            parts.last().data += data;
        } else {
            parts.append({kind, count, 0, data});
        }
    };
    // The records in groups (v3), as the hash takes them.
    QList<RecItem> stamped;
    for (const RecItem &r : l.records)
        if (r.kind != RecItem::Unstamped)
            stamped.append(r);
    qsizetype i = 0;
    while (i < stamped.size()) {
        qsizetype end = i + 1;
        if (s.version >= 2 && stamped[i].kind != RecItem::Leaf) {
            int count = 1;
            while (end < stamped.size() && count < kGroup && stamped[end].kind != RecItem::Leaf) {
                ++end;
                ++count;
            }
        }
        bool anyKept = false;
        for (qsizetype k = i; k < end; ++k)
            anyKept |= stamped[k].kind != RecItem::Leaf && keep(stamped[k]);
        if (s.version >= 2 && !anyKept) {
            if (stamped[i].kind == RecItem::Leaf) {
                parts.append({StampPart::Leaf, stamped[i].count, stamped[i].durationUs, stamped[i].encoded});
            } else {
                QByteArray group;
                quint64 duration = 0;
                for (qsizetype k = i; k < end; ++k) {
                    group += commitmentOf(stamped[k]);
                    duration += durationOf(stamped[k]);
                }
                parts.append({StampPart::Leaf, int(end - i), duration, sha256(group)});
            }
        } else if (s.version >= 2) {
            // A group cut by an edge: the records kept as they are, each run of the others as their hashes.
            QByteArray hidden;
            int count = 0;
            quint64 duration = 0;
            auto flushHidden = [&] {
                if (count)
                    parts.append({StampPart::Commitments, count, duration, hidden});
                hidden.clear();
                count = 0;
                duration = 0;
            };
            for (qsizetype k = i; k < end; ++k) {
                const RecItem &r = stamped[k];
                if (r.kind == RecItem::Present && keep(r)) {
                    flushHidden();
                    add(StampPart::Records, 1, withSalts ? r.salt : QByteArray());
                } else {
                    hidden += commitmentOf(r);
                    ++count;
                    duration += durationOf(r);
                }
            }
            flushHidden();
        } else {
            for (qsizetype k = i; k < end; ++k) {
                const RecItem &r = stamped[k];
                if (r.kind != RecItem::Leaf && keep(r)) {
                    add(StampPart::Records, 1);
                } else {
                    add(StampPart::Hidden, 0, r.encoded);
                    if (revealed)
                        ++*revealed;
                }
            }
        }
        i = end;
    }
    int unstamped = 0; // records of a voided stamp left in the document
    for (const RecItem &r : l.records)
        if (r.kind == RecItem::Unstamped && keep(RecItem{RecItem::Present, r.doc, {}, 1, 0, {}}))
            ++unstamped;
    if (unstamped)
        parts.append({StampPart::Unstamped, unstamped, 0, {}});
    for (const PacketItem &p : l.packets) {
        if (p.doc >= 0 && keepPacket(p))
            add(StampPart::Packets, 1);
        else
            parts.append({StampPart::HiddenPacket, 0, 0, p.hash});
    }
    return parts;
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

QByteArray salt(QByteArrayView key, quint32 j)
{
    // A hash with the key in front is enough here: the other salts are not extensions of a known input but inputs of
    // the same length with another j, and the output is cut.
    char index[4], out[32];
    qToLittleEndian<quint32>(j, index);
    sha256Into(out, key, QByteArrayView(index, 4));
    return QByteArray(out, kSaltSize);
}

QByteArray commitment(QByteArrayView salt, QByteArrayView encodedRecord)
{
    QByteArray out(32, Qt::Uninitialized);
    sha256Into(out.data(), salt, encodedRecord);
    return out;
}

QByteArray leaf(int count, quint64 durationUs, QByteArrayView groupHash)
{
    QByteArray in(9, Qt::Uninitialized);
    in[0] = char(count);
    qToLittleEndian<quint64>(durationUs, in.data() + 1);
    return sha256(in + groupHash.toByteArray());
}

QByteArray recordsHash(QByteArrayView encoded, QByteArrayView key)
{
    QByteArray leaves;
    for (qsizetype i = 0; i < encoded.size(); i += 12 * kGroup) {
        const QByteArrayView group = encoded.sliced(i, std::min<qsizetype>(12 * kGroup, encoded.size() - i));
        quint64 duration = 0;
        QByteArray commitments(group.size() / 12 * 32, Qt::Uninitialized);
        for (qsizetype k = 0; k < group.size(); k += 12) {
            duration += dtOf(group.sliced(k, 12));
            char index[4], salted[32];
            qToLittleEndian<quint32>(quint32((i + k) / 12), index);
            sha256Into(salted, key, QByteArrayView(index, 4));
            sha256Into(commitments.data() + k / 12 * 32, QByteArrayView(salted, kSaltSize), group.sliced(k, 12));
        }
        leaves += leaf(int(group.size() / 12), duration, sha256(commitments));
    }
    return sha256(leaves);
}

QByteArray packetHash(const MediaPacket &p)
{
    return sha256(MediaClip::packetBytes(p));
}

QByteArray chunkHashV3(QByteArrayView previousImprint, int records, quint32 delayMs, QByteArrayView recordsHash,
                       int packets, QByteArrayView packetHashes)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QByteArrayView("TypingStatistics stamps 3"));
    const char size = char(previousImprint.size());
    h.addData(QByteArrayView(&size, 1));
    h.addData(previousImprint);
    uchar b[4];
    qToLittleEndian<quint32>(quint32(records), b);
    h.addData(QByteArrayView(reinterpret_cast<const char *>(b), 4));
    qToLittleEndian<quint32>(delayMs, b);
    h.addData(QByteArrayView(reinterpret_cast<const char *>(b), 4));
    h.addData(recordsHash);
    qToLittleEndian<quint32>(quint32(packets), b);
    h.addData(QByteArrayView(reinterpret_cast<const char *>(b), 4));
    h.addData(sha256(packetHashes));
    return h.result();
}

void Chain::reset()
{
    m_normalizer = {};
    m_encoded.clear();
    m_timeUs.clear();
}

bool Chain::push(const KeyRecord &raw)
{
    const std::optional<KeyRecord> r = m_normalizer.push(raw);
    if (!r)
        return false;
    m_encoded += encode(*r);
    m_timeUs.append(m_timeUs.isEmpty() ? 0 : m_timeUs.last() + r->dtUs);
    return true;
}

QByteArray Chain::hash(QByteArrayView previousImprint, int from, int to, quint32 delayMs) const
{
    return chunkHash(previousImprint, to - from, delayMs, QByteArrayView(m_encoded).sliced(from * 12, (to - from) * 12));
}

QByteArray Chain::hashV3(QByteArrayView previousImprint, int from, int to, quint32 delayMs, QByteArrayView key,
                         QByteArrayView packetHashes, int packets) const
{
    return chunkHashV3(previousImprint, to - from, delayMs,
                       recordsHash(QByteArrayView(m_encoded).sliced(from * 12, (to - from) * 12), key), packets,
                       packetHashes);
}

Report verify(const KeyRecords &normalized, const QList<Stamp> &stamps, const QList<QByteArray> &certificates,
              const MediaClip *clip, qint64 clipModelOffsetUs, std::optional<quint32> firstDtUs, Cache *cache)
{
    Report rep;
    rep.records = int(normalized.size());
    rep.stamps = int(stamps.size());
    rep.packets = clip ? int(clip->packets.size()) : 0;
    for (const KeyRecord &r : normalized)
        if (r.isDown() && (r.flags & KeyRecord::Injected))
            ++rep.injected;
    if (stamps.isEmpty())
        return rep;

    const Records records(normalized, stamps, firstDtUs);
    // The model time of each record (the time of the document's packets is in it).
    QVector<qint64> modelUs(normalized.size());
    for (qsizetype i = 0; i < normalized.size(); ++i)
        modelUs[i] = (i ? modelUs[i - 1] : 0) + normalized[i].dtUs;

    QVector<char> confirmed(normalized.size(), 0);
    QSet<QString> authorities;
    int recordCursor = 0, packetCursor = 0;
    bool structure = true;   // the chunks follow each other within the document
    int lastGoodEnd = -1;    // the document's record cursor after the last good stamp of the current band
    qint64 bandLo = 0, bandHi = 0;
    qint64 virtualUs = 0;    // the time of the recording by the chain's records, hidden ones too
    std::optional<qint64> modelMinusVirtual;
    bool seenPresent = false;
    qint64 hiddenTail = 0;
    for (int k = 0; k < stamps.size(); ++k) {
        const Stamp &s = stamps[k];
        const QByteArray previous = k == 0 ? s.previous : imprintOf(stamps[k - 1].token);
        if (!isVersionChecked(s.version)) {
            // Not this program's to check: its records are not stamped, but nothing is broken; the chain goes on
            // after it (its chunk ends where it says).
            ++rep.unknown;
            lastGoodEnd = -1;
            if (structure && (s.end < recordCursor || s.mediaEnd < packetCursor))
                structure = false;
            recordCursor = s.end;
            packetCursor = s.mediaEnd;
            continue;
        }
        bool good = false;
        Layout l;
        if (structure) {
            l = layOut(s, recordCursor, packetCursor, records, clip);
            if (!l.ok || (l.records.isEmpty() && l.packets.isEmpty() && !s.voided) || (s.version < 2 && !s.voided && l.present == 0 && s.parts.isEmpty()))
                structure = false;
        }
        if (!structure) {
            ++rep.bad;
            lastGoodEnd = -1;
            continue;
        }
        // The time through the chunk; the constant between the model's time and the chain's.
        qint64 lastUs = virtualUs;
        for (const RecItem &r : l.records) {
            const qint64 d = qint64(durationOf(r));
            if (r.kind == RecItem::Unstamped)
                continue;
            virtualUs += d;
            lastUs = virtualUs;
            if (r.kind == RecItem::Present) {
                if (!modelMinusVirtual)
                    modelMinusVirtual = modelUs[r.doc] - virtualUs;
                if (!seenPresent)
                    rep.hiddenBeforeUs = virtualUs - d;
                seenPresent = true;
                hiddenTail = 0;
            } else if (seenPresent) {
                hiddenTail += d;
            }
        }
        TimeStamp::Info info;
        bool tokenOk = false, matches = false;
        const auto same = [&](const Cache::Entry &e) {
            return e.end == s.end && e.mediaEnd == s.mediaEnd && e.recordCursor == recordCursor
                   && e.packetCursor == packetCursor && e.parts == s.parts.size() && e.clip == (clip != nullptr)
                   && e.token == s.token && e.previous == previous;
        };
        if (cache && k < cache->entries.size() && same(cache->entries[k])) {
            const Cache::Entry &e = cache->entries[k];
            tokenOk = e.tokenOk;
            matches = e.matches;
            info = e.info;
        } else {
            tokenOk = TimeStamp::verify(s.token, certificates, &info) == TimeStamp::Check::Ok && info.sha256;
            matches = tokenOk && info.imprint == imprint(s, l, previous);
            if (cache) {
                cache->entries.resize(k);
                cache->entries.append({s.token, previous, s.end, s.mediaEnd, recordCursor, packetCursor,
                                       s.parts.size(), clip != nullptr, tokenOk, matches, info});
            }
        }
        const bool keptLeaves = s.version >= 2 && !s.parts.isEmpty();  // v3
        if (s.voided) {
            ++rep.voided;
            // A voided stamp v2 with its records as leaves still stamps its packets.
            if (keptLeaves ? !matches : !tokenOk)
                ++rep.bad;
            else if (keptLeaves)
                for (const PacketItem &p : l.packets)
                    rep.packetsStamped += p.doc >= 0;
        } else if (matches) {
            // Video timed after the stamp that holds it: it did not exist then as it says.
            bool late = false;
            if (clip && modelMinusVirtual) {
                const qint64 stampModelUs = lastUs + *modelMinusVirtual + qint64(s.delayMs) * 1000;
                for (const PacketItem &p : l.packets)
                    if (p.doc >= 0) {
                        const qint64 t = clip->docTime(clip->packets[p.doc].ptsUs) - clipModelOffsetUs;
                        if (t > stampModelUs + kToleranceMs * 1000) {
                            ++rep.packetsLate;
                            late = true;
                        }
                    }
            }
            if (late) {
                ++rep.bad;
            } else {
                good = true;
                rep.stamped += l.present;
                for (const PacketItem &p : l.packets)
                    rep.packetsStamped += p.doc >= 0;
                authorities.insert(shortName(info.authority));
                if (rep.firstMs == 0)
                    rep.firstMs = info.timeMs;
                rep.lastMs = info.timeMs;
                // The time of the stamp against the time of its last record: in one band they differ by about
                // the same (the delay of the network), whatever the pauses.
                const qint64 offset = info.timeMs - lastUs / 1000 - qint64(s.delayMs);
                const qint64 lo = std::min(bandLo, offset), hi = std::max(bandHi, offset);
                if (lastGoodEnd >= 0 && hi - lo <= kToleranceMs) {
                    std::fill(confirmed.begin() + std::max(0, lastGoodEnd - 1), confirmed.begin() + l.recordsEnd, 1);
                    bandLo = lo;
                    bandHi = hi;
                } else {
                    bandLo = bandHi = offset;
                }
                rep.driftMs = std::max(rep.driftMs, bandHi - bandLo);
                lastGoodEnd = l.recordsEnd;
            }
        } else {
            ++rep.bad;
        }
        if (!good)
            lastGoodEnd = -1; // the time is not followed across a stamp that does not check
        recordCursor = l.recordsEnd;
        packetCursor = l.packetsEnd;
    }
    rep.hiddenAfterUs = hiddenTail;
    rep.pendingRecords = structure ? std::max(0, rep.records - recordCursor) : 0;
    rep.pendingPackets = structure ? std::max(0, rep.packets - packetCursor) : 0;
    rep.confirmed = int(std::count(confirmed.begin(), confirmed.end(), 1));
    rep.authorities = QStringList(authorities.begin(), authorities.end());
    rep.authorities.sort();
    if (rep.bad > 0)
        rep.status = Report::Status::Broken;
    else if (rep.confirmed == rep.records && rep.packetsStamped == rep.packets)
        rep.status = Report::Status::Confirmed;
    else
        rep.status = Report::Status::Partial;
    return rep;
}

void follow(QList<Stamp> &stamps, const KeyRecords &before, const KeyRecords &edited)
{
    if (stamps.isEmpty())
        return;
    // The layouts over the records before the edit (v3 voided stamps keep them as leaves).
    const Records old(before, stamps, std::nullopt);
    QList<Layout> layouts;
    {
        int rc = 0, pc = 0;
        for (const Stamp &s : stamps) {
            Layout l = layOut(s, rc, pc, old, nullptr);
            rc = l.recordsEnd;
            pc = l.packetsEnd;
            layouts.append(l);
        }
    }
    // The new end of a stamp: the records left of those it covered (tags are old indices + 1, in order).
    QVector<quint32> tags;
    tags.reserve(edited.size());
    for (const KeyRecord &r : edited)
        tags.append(r.tag);
    QByteArray encoded;
    encoded.reserve(edited.size() * 12);
    for (const KeyRecord &r : edited)
        encoded += encode(r);
    QByteArray previous = stamps.first().previous;
    int prevEnd = 0, prevOldEnd = 0;
    for (int k = 0; k < stamps.size(); ++k) {
        Stamp &s = stamps[k];
        const int oldEnd = s.end;
        if (!isVersionChecked(s.version)) { // not followed: it stays where it was, if it can
            s.end = int(std::upper_bound(tags.begin(), tags.end(), quint32(s.end)) - tags.begin());
            s.end = std::max(s.end, prevEnd);
            prevEnd = s.end;
            prevOldEnd = oldEnd;
            previous = imprintOf(s.token);
            continue;
        }
        s.end = int(std::upper_bound(tags.begin(), tags.end(), quint32(s.end)) - tags.begin());
        const QByteArrayView now = QByteArrayView(encoded).sliced(prevEnd * 12, std::max(0, s.end - prevEnd) * 12);
        bool changed;
        if (!s.parts.isEmpty()) {
            changed = s.end - prevEnd != oldEnd - prevOldEnd; // a block's stamp: any record dropped
            // or changed in place (the records it shows are those it had)
            for (int i = 0; !changed && i < s.end - prevEnd; ++i)
                changed = encode(before[prevOldEnd + i]) != now.sliced(qsizetype(i) * 12, 12).toByteArray();
        } else if (s.version >= 2) {
            // The records of the chunk before and now.
            QByteArray then;
            for (int i = prevOldEnd; i < oldEnd && i < old.size(); ++i)
                then += old.at(i).toByteArray();
            changed = s.end <= prevEnd || then != now.toByteArray();
        } else {
            changed = s.end <= prevEnd || imprintOf(s.token) != chunkHash(previous, s.end - prevEnd, s.delayMs, now);
        }
        if (!s.voided && changed) {
            s.voided = true;
            if (s.version >= 2 && layouts[k].ok) {
                // Its records as leaves (none of them is shown), the records left of them unstamped, its packets.
                QList<StampPart> parts = partsOf(s, layouts[k], [](const RecItem &) { return false; },
                                                 [](const PacketItem &) { return true; }, false, nullptr);
                const int left = std::max(0, s.end - prevEnd);
                if (left)
                    parts.insert(std::find_if(parts.begin(), parts.end(),
                                              [](const StampPart &p) {
                                                  return p.kind == StampPart::Packets || p.kind == StampPart::HiddenPacket;
                                              }),
                                 StampPart{StampPart::Unstamped, left, 0, {}});
                s.parts = parts;
            }
        }
        s.end = std::max(s.end, prevEnd);
        prevEnd = s.end;
        prevOldEnd = oldEnd;
        previous = imprintOf(s.token);
    }
}

QList<Stamp> extract(const QList<Stamp> &stamps, const KeyRecords &normalized, const MediaClip *clip, int from, int to,
                     const QList<int> &keptPackets, int *revealed, std::optional<quint32> firstDtUs)
{
    if (revealed)
        *revealed = 0;
    if (stamps.isEmpty() || from >= to)
        return {};
    const Records records(normalized, stamps, firstDtUs);
    QList<Layout> layouts;
    int rc = 0, pc = 0;
    for (const Stamp &s : stamps) {
        Layout l = layOut(s, rc, pc, records, clip);
        if (!l.ok)
            return {}; // stamps that do not fit the document: none in the block
        rc = l.recordsEnd;
        pc = l.packetsEnd;
        layouts.append(l);
    }
    // The new indexes of the kept packets.
    QHash<int, int> packetIndex;
    for (int i = 0; i < keptPackets.size(); ++i)
        packetIndex.insert(keptPackets[i], i);
    auto keepRecord = [from, to](const RecItem &r) { return r.kind == RecItem::Present && r.doc >= from && r.doc < to; };
    auto keepPacket = [&packetIndex](const PacketItem &p) { return p.doc >= 0 && packetIndex.contains(p.doc); };
    // The stamps that hold something of the block, and the one before them (the band of time starts there).
    int first = -1, last = -1;
    for (int k = 0; k < layouts.size(); ++k) {
        bool holds = false;
        for (const RecItem &r : layouts[k].records)
            holds |= keepRecord(r);
        for (const PacketItem &p : layouts[k].packets)
            holds |= keepPacket(p);
        if (holds) {
            if (first < 0)
                first = k;
            last = k;
        }
    }
    if (first < 0)
        return {};
    first = std::max(0, first - 1);
    QList<Stamp> out;
    int end = 0, mediaEnd = 0;
    for (int k = first; k <= last; ++k) {
        const Stamp &s = stamps[k];
        Stamp b = s;
        // v3: the key stays with a stamp all of whose records are in the block; otherwise the records shown carry
        // their salts, and the others' cannot be had.
        bool all = true;
        for (const RecItem &r : layouts[k].records)
            all &= r.kind == RecItem::Unstamped || keepRecord(r);
        const bool keepKey = s.version >= 2 && all && !s.key.isEmpty();
        b.key = keepKey ? s.key : QByteArray();
        b.parts = partsOf(s, layouts[k], keepRecord, keepPacket, s.version >= 2 && !keepKey, revealed);
        for (const StampPart &p : b.parts) {
            if (p.kind == StampPart::Records)
                end += p.count;
            else if (p.kind == StampPart::Packets)
                mediaEnd += p.count;
        }
        b.end = end;
        b.mediaEnd = mediaEnd;
        b.voided = s.voided; // a voided v1 stamp without leaves stays only a link
        if (k == first)
            b.previous = k == 0 ? s.previous : imprintOf(stamps[k - 1].token);
        else
            b.previous.clear();
        out.append(b);
    }
    return out;
}

void addCertificates(QList<QByteArray> &pool, const QList<QByteArray> &certificates)
{
    for (const QByteArray &c : certificates)
        if (!pool.contains(c))
            pool.append(c);
}

} // namespace Stamps
