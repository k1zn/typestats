// Time stamps of a recording (re/stamps.md): DER, RSA, RFC 3161 tokens, the chain and its report. The golden
// recordings were typed on the clock and stamped by the real authorities by re/scripts/make_stamped.py - code
// written apart from the program's, so they also check that both read the format alike.
#include "core/Der.h"
#include "core/Editing.h"
#include "core/Recalc.h"
#include "core/Rsa.h"
#include "core/Stamps.h"
#include "core/TimeStamp.h"
#include "core/TsfFile.h"

#include <QDateTime>
#include <QTimeZone>
#include <QTest>

namespace {

TsfDocument load(const char *name)
{
    TsfDocument doc;
    const Tsf::ReadError err = Tsf::read(QStringLiteral(TS_GOLDEN_DIR "/stamps/") + QLatin1String(name), doc);
    Q_ASSERT(err == Tsf::ReadError::None);
    return doc;
}

Stamps::Report report(const TsfDocument &doc)
{
    return Stamps::verify(Recalc::normalized(doc.records), doc.stamps, doc.stampCertificates);
}

// With the webcam's clip, as the program asks (MainWindow::stampReport).
Stamps::Report reportWith(const TsfDocument &doc, const MediaClip &clip)
{
    std::optional<quint32> firstDt;
    for (const KeyRecord &r : doc.records)
        if (r.isDown()) {
            firstDt = r.dtUs;
            break;
        }
    return Stamps::verify(Recalc::normalized(doc.records), doc.stamps, doc.stampCertificates, &clip,
                          DocTime::modelOffset(doc.records), firstDt);
}

} // namespace

class TstStamps : public QObject
{
    Q_OBJECT

private slots:
    void init() { TimeStamp::setRevokedKeys({}); } // and clears the cache

    void derTime()
    {
        const auto at = [](quint8 tag, const char *s) {
            return Der::time(Der::Node{tag, {}, QByteArrayView(s)});
        };
        const qint64 t = QDateTime(QDate(2026, 10, 6), QTime(18, 13, 19), QTimeZone::utc()).toMSecsSinceEpoch();
        QCOMPARE(at(Der::GeneralizedTime, "20261006181319Z"), t);
        QCOMPARE(at(Der::GeneralizedTime, "20261006181319.25Z"), t + 250);
        QCOMPARE(at(Der::UtcTime, "261006181319Z"), t);
        QCOMPARE(at(Der::UtcTime, "991231235959Z"),
                 QDateTime(QDate(1999, 12, 31), QTime(23, 59, 59), QTimeZone::utc()).toMSecsSinceEpoch());
        QVERIFY(!at(Der::GeneralizedTime, "20261006181319"));   // no Z
        QVERIFY(!at(Der::GeneralizedTime, "20261306181319Z"));  // month 13
        QVERIFY(!at(Der::GeneralizedTime, "20261006181319.Z"));
    }

    void derMalformed()
    {
        QVERIFY(!Der::parse(QByteArray::fromHex("3005020101")));    // too short
        QVERIFY(!Der::parse(QByteArray::fromHex("30030201010000"))); // trailing bytes
        QVERIFY(!Der::parse(QByteArray::fromHex("3085000000000000"))); // length of 5 bytes
        QVERIFY(Der::parse(QByteArray::fromHex("3003020101")));
        QCOMPARE(Der::oid("2.16.840.1.101.3.4.2.1").toHex(), QByteArray("608648016503040201"));
        QCOMPARE(Der::integer(0x80).toHex(), QByteArray("02020080"));
        QCOMPARE(Der::tlv(Der::OctetString, QByteArray(200, 'x')).left(3).toHex(), QByteArray("0481c8"));
    }

    void request()
    {
        const QByteArray hash = QCryptographicHash::hash("x", QCryptographicHash::Sha256);
        const QByteArray der = TimeStamp::request(hash, 0x1234, true); // the nodes point into it
        const std::optional<Der::Node> req = Der::parse(der);
        QVERIFY(req && req->tag == Der::Sequence);
        Der::Reader r(req->content);
        const auto version = r.next(Der::Integer), imprint = r.next(Der::Sequence), nonce = r.next(Der::Integer),
                   certReq = r.next(Der::Boolean);
        QVERIFY(version && imprint && nonce && certReq && r.atEnd());
        QCOMPARE(Der::unsignedInteger(*nonce).toByteArray().toHex(), QByteArray("1234"));
        QVERIFY(imprint->content.toByteArray().endsWith(hash));
        // certReq FALSE is the default: DER leaves it out.
        const QByteArray plainDer = TimeStamp::request(hash, 1, false);
        Der::Reader plain(Der::parse(plainDer)->content);
        plain.next();
        plain.next();
        plain.next();
        QVERIFY(plain.atEnd());
    }

    void tokens()
    {
        const TsfDocument doc = load("stamped.tsf");
        QCOMPARE(doc.stamps.size(), 3);
        QStringList authorities;
        for (const Stamp &s : doc.stamps) {
            TimeStamp::Info info;
            QCOMPARE(TimeStamp::verify(s.token, {}, &info), TimeStamp::Check::Ok); // certificates in the token
            QVERIFY(info.sha256);
            QCOMPARE(info.imprint.size(), 32);
            authorities << info.authority.section(u' ', 0, 0);
            // Without its certificates it checks against a pool that has them.
            const QByteArray bare = TimeStamp::withoutCertificates(s.token);
            QVERIFY(bare.size() < s.token.size());
            QCOMPARE(TimeStamp::verify(bare, {}), TimeStamp::Check::UnknownSigner);
            QCOMPARE(TimeStamp::verify(bare, TimeStamp::certificates(s.token)), TimeStamp::Check::Ok);
            QCOMPARE(TimeStamp::info(bare)->imprint, info.imprint);
            // The signature: its last byte changed.
            QByteArray forged = s.token;
            const QByteArray sig = forged.right(16);
            const qsizetype at = forged.lastIndexOf(sig) + 15;
            forged[at] = char(forged[at] ^ 1);
            QVERIFY(TimeStamp::verify(forged, {}) != TimeStamp::Check::Ok);
            // The time in TSTInfo moved by a second: messageDigest no longer matches.
            const QByteArray when = QDateTime::fromMSecsSinceEpoch(info.timeMs, QTimeZone::utc())
                                        .toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
            QByteArray moved = s.token;
            const qsizetype t = moved.indexOf(when);
            QVERIFY(t > 0);
            moved[t + 13] = char(moved[t + 13] == '9' ? '8' : moved[t + 13] + 1);
            QCOMPARE(TimeStamp::verify(moved, {}), TimeStamp::Check::BadSignature);
        }
        authorities.sort();
        QCOMPARE(authorities, (QStringList{"DigiCert", "Globalsign", "Sectigo"}));
        QCOMPARE(TimeStamp::verify(QByteArray("not a token"), {}), TimeStamp::Check::Malformed);
    }

    // A key of an authority known to be stolen, and a time before the program stamped anything.
    void revokedAndEarly()
    {
        const TsfDocument doc = load("stamped.tsf");
        QCOMPARE(report(doc).status, Stamps::Report::Status::Confirmed);
        const QByteArray &token = doc.stamps[0].token;
        TimeStamp::Info info;
        QCOMPARE(TimeStamp::verify(token, {}, &info), TimeStamp::Check::Ok);

        // The SubjectPublicKeyInfo of a certificate: the 7th field of its TBSCertificate (with the version).
        const auto spkiOf = [](const QByteArray &der) {
            Der::Reader cert(Der::parse(der)->content);
            Der::Reader tbs(cert.next(Der::Sequence)->content);
            tbs.next(Der::context(0));
            for (int i = 0; i < 5; ++i)
                tbs.next();
            return QCryptographicHash::hash(tbs.next(Der::Sequence)->whole, QCryptographicHash::Sha256);
        };
        int revoked = 0;
        QByteArray signerKey;
        for (const QByteArray &cert : TimeStamp::certificates(token)) {
            TimeStamp::setRevokedKeys({spkiOf(cert)});
            const TimeStamp::Check c = TimeStamp::verify(token, {});
            // The signer is revoked; a stolen intermediate untrusts the chain; a root in the token is not used.
            QVERIFY(c == TimeStamp::Check::Revoked || c == TimeStamp::Check::Untrusted || c == TimeStamp::Check::Ok);
            if (c == TimeStamp::Check::Revoked) {
                ++revoked;
                signerKey = spkiOf(cert);
            }
        }
        QCOMPARE(revoked, 1);
        TimeStamp::setRevokedKeys({signerKey});
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
        TimeStamp::setRevokedKeys({});
        QCOMPARE(TimeStamp::verify(token, {}), TimeStamp::Check::Ok);
        QCOMPARE(report(doc).status, Stamps::Report::Status::Confirmed);

        // A year earlier: refused before its signature is looked at.
        const QByteArray when = QDateTime::fromMSecsSinceEpoch(info.timeMs, QTimeZone::utc())
                                    .toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
        QVERIFY(when.startsWith("2026"));
        QByteArray early = token;
        const qsizetype t = early.indexOf(when);
        QVERIFY(t > 0);
        early[t + 3] = '5';
        QCOMPARE(TimeStamp::verify(early, {}), TimeStamp::Check::TooEarly);
    }

    void rsaRejects()
    {
        // A signature not below the modulus, a wrong length.
        Rsa::PublicKey key{QByteArray(128, char(0xC3)), QByteArray::fromHex("010001")};
        key.modulus.back() = char(0xC3);
        QVERIFY(!Rsa::publicOperation(key, QByteArray(128, char(0xFF))));
        QVERIFY(!Rsa::publicOperation(key, QByteArray(127, char(0x01))));
        QVERIFY(!Rsa::verify(key, QCryptographicHash::Sha256, QByteArray(32, 0), QByteArray(128, char(0x01))));
    }

    void confirmed()
    {
        const TsfDocument doc = load("stamped.tsf");
        const Stamps::Report r = report(doc);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
        QCOMPARE(r.stamps, 3);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.records, 176);
        QCOMPARE(r.stamped, 176);
        QCOMPARE(r.confirmed, 176);
        QVERIFY(r.driftMs < Stamps::kToleranceMs);
        QCOMPARE(r.authorities, (QStringList{"DigiCert", "GlobalSign", "Sectigo"}));
        QVERIFY(r.lastMs - r.firstMs > 15000); // typed for ~20 s
        QCOMPARE(r.injected, 0);
    }

    void moreAuthorities()
    {
        // The reserve: Certum, SwissSign, Microsoft (their roots pinned like the first three).
        const Stamps::Report r = report(load("more.tsf"));
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.confirmed, r.records);
        QCOMPARE(r.authorities, (QStringList{"Certum", "Microsoft", "SwissSign"}));
    }

    void compressedTime()
    {
        // The records claim 15 % faster typing than the real one: the hashes match, the time does not.
        const Stamps::Report r = report(load("compressed.tsf"));
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.stamped, 176);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QVERIFY(r.confirmed < 100);
    }

    void keyLimits()
    {
        using namespace Der;
        const auto spki = [](const QByteArray &n, const QByteArray &e) {
            const QByteArray key = tlv(Sequence, tlv(Integer, n) + tlv(Integer, e));
            return tlv(Sequence, tlv(Sequence, tlv(Oid, oid("1.2.840.113549.1.1.1")) + tlv(Null, {}))
                                     + tlv(BitString, QByteArray(1, '\0') + key));
        };
        QByteArray n(256, char(0xC3));
        n[0] = 0x43;
        n[255] = char(0xC5);
        QVERIFY(Rsa::fromSpki(spki(n, QByteArray::fromHex("010001"))));
        QVERIFY(Rsa::fromSpki(spki(n, QByteArray::fromHex("03"))));
        QVERIFY(!Rsa::fromSpki(spki(n, QByteArray::fromHex("01"))));
        QVERIFY(!Rsa::fromSpki(spki(n, QByteArray::fromHex("7FFFFFFFFF"))));
        QByteArray big(1024, char(0x43));
        big[1023] = char(0xC5);
        QVERIFY(!Rsa::fromSpki(spki(big, QByteArray::fromHex("010001"))));
    }

    void previousImprint()
    {
        TsfDocument doc = load("stamped.tsf");
        doc.stamps[0].previous = QByteArray(31, 'x');
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
    }

    void delayedTime()
    {
        // Twice as fast as the real typing, the difference in the delays of the requests: longer than the gap to the
        // next key, they do not time the records.
        const Stamps::Report r = report(load("delayed.tsf"));
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.stamped, 176);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QCOMPARE(r.confirmed, 0);
    }

    void fileRoundTrip()
    {
        TsfDocument doc = load("stamped.tsf");
        for (Stamp &s : doc.stamps) {
            Stamps::addCertificates(doc.stampCertificates, TimeStamp::certificates(s.token));
            s.token = TimeStamp::withoutCertificates(s.token);
        }
        doc.stamps[1].voided = true;
        const TsfDocument back = Tsf::parse(Tsf::serialize(doc, true));
        QCOMPARE(back.stamps.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(back.stamps[i].end, doc.stamps[i].end);
            QCOMPARE(back.stamps[i].delayMs, doc.stamps[i].delayMs);
            QCOMPARE(back.stamps[i].voided, i == 1);
            QCOMPARE(back.stamps[i].token, doc.stamps[i].token);
        }
        QCOMPARE(back.stampCertificates, doc.stampCertificates);
        QCOMPARE(back.records.size(), doc.records.size()); // the header lines are not records
        QVERIFY(back.signatureValid);
        doc.stamps[1].voided = false;
        QCOMPARE(report(doc).status, Stamps::Report::Status::Confirmed);
    }

    void marksKeep()
    {
        // "Пометить" and labels are put on records later: they do not break the stamps.
        TsfDocument doc = load("stamped.tsf");
        doc.records[10].flags |= KeyRecord::Marked;
        doc.records[11].comment = QStringLiteral("метка");
        doc.records[12].flags |= KeyRecord::Transient | KeyRecord::SegmentStart;
        QCOMPARE(report(doc).status, Stamps::Report::Status::Confirmed);
    }

    void tampering()
    {
        const TsfDocument orig = load("stamped.tsf");
        TsfDocument doc = orig;
        doc.records[120].dtUs -= 50000; // a pause shortened
        Stamps::Report r = report(doc);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
        QCOMPARE(r.bad, 1);
        QCOMPARE(r.confirmed, 89); // up to the second stamp

        doc = orig;
        doc.records[5].ch = u'x';
        r = report(doc);
        QCOMPARE(r.bad, 1);       // the second stamp; the third links to its signed imprint and still checks
        QCOMPARE(r.confirmed, 0); // the time is not followed across it
        doc = orig;
        doc.stamps.removeAt(1);
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
        doc = orig;
        doc.stamps.swapItemsAt(0, 1);
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
        doc = orig;
        doc.stamps[2].delayMs = 0; // the delay is under the hash
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
        doc = orig;
        doc.stamps[2].voided = true; // voided by hand: its records are not confirmed
        r = report(doc);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QCOMPARE(r.voided, 1);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.confirmed, 89);
    }

    void editFollows()
    {
        // Deleting in the last part voids its stamp only; the ones before keep checking.
        TsfDocument doc = load("stamped.tsf");
        KeyRecords recs = Recalc::normalized(doc.records);
        for (int i = 0; i < recs.size(); ++i)
            recs[i].tag = quint32(i + 1);
        KeyRecords before = recs;
        Editing::deleteRange(recs, 120, 124);
        Stamps::follow(doc.stamps, before, recs);
        QCOMPARE(doc.stamps[1].end, 89);
        QVERIFY(!doc.stamps[0].voided && !doc.stamps[1].voided && doc.stamps[2].voided);
        QVERIFY(doc.stamps[2].end < 176);
        doc.records = recs;
        Stamps::Report r = report(doc);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.confirmed, 89);

        // Deleting in the first part: the stamps after it follow their records and keep checking.
        doc = load("stamped.tsf");
        recs = Recalc::normalized(doc.records);
        for (int i = 0; i < recs.size(); ++i)
            recs[i].tag = quint32(i + 1);
        before = recs;
        Editing::deleteRange(recs, 30, 32);
        Stamps::follow(doc.stamps, before, recs);
        QVERIFY(!doc.stamps[0].voided && doc.stamps[1].voided && !doc.stamps[2].voided);
        doc.records = recs;
        r = report(doc);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.stamped, int(recs.size()) - doc.stamps[1].end + 1);
    }

    void liveChain()
    {
        // The chain built record by record gives the imprints that were stamped.
        const TsfDocument doc = load("stamped.tsf");
        Stamps::Chain chain;
        for (const KeyRecord &r : doc.records)
            QVERIFY(chain.push(r));
        QByteArray previous;
        int from = 0;
        for (const Stamp &s : doc.stamps) {
            const QByteArray imprint = TimeStamp::info(s.token)->imprint;
            QCOMPARE(chain.hash(previous, from, s.end, s.delayMs), imprint);
            previous = imprint;
            from = s.end;
        }
        // Auto-repeated modifiers are not kept, leading releases neither.
        Stamps::Chain c;
        KeyRecord up;
        up.flags = 0xA0u << 16 | KeyRecord::KeyUp;
        QVERIFY(!c.push(up));
        KeyRecord shift;
        shift.flags = 0xA0u << 16;
        QVERIFY(c.push(shift));
        QVERIFY(!c.push(shift));
        QCOMPARE(c.size(), 1);
    }

    void videoChain()
    {
        // The chain v3 with a webcam clip (make_stamped.py --v3 --video): records and packets confirmed.
        const TsfDocument doc = load("video.tsf");
        QCOMPARE(doc.stamps.first().version, 3);
        QCOMPARE(doc.stamps.first().key.size(), Stamps::kKeySize);
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        QCOMPARE(clip.packets.size(), 206);
        Stamps::Report r = reportWith(doc, clip);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
        QCOMPARE(r.packetsStamped, 206);
        QCOMPARE(r.packetsLate, 0);
        QCOMPARE(r.confirmed, 176);

        // A packet changed, one dropped, the video taken away: the stamps do not check.
        MediaClip changed = clip;
        changed.packets[100].data[0] = char(changed.packets[100].data[0] ^ 1);
        QCOMPARE(reportWith(doc, changed).status, Stamps::Report::Status::Broken);
        MediaClip dropped = clip;
        dropped.packets.removeAt(50);
        QCOMPARE(reportWith(doc, dropped).status, Stamps::Report::Status::Broken);
        QCOMPARE(report(doc).status, Stamps::Report::Status::Broken);
        // A packet re-dated (its bytes hold the time): broken too.
        MediaClip redated = clip;
        redated.packets[10].ptsUs += 1000;
        QCOMPARE(reportWith(doc, redated).status, Stamps::Report::Status::Broken);
        // Video added after the last stamp: not under the stamps.
        MediaClip more = clip;
        more.packets.append({0, false, clip.packets.last().ptsUs + 100000, "x"});
        r = reportWith(doc, more);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QCOMPARE(r.packetsStamped, 206);
        QCOMPARE(r.pendingPackets, 1); // after the last stamp: while recording it waits for the next one
        QCOMPARE(r.pendingRecords, 0);

        // The video claims to be 5 s later than the stamps that hold it.
        const TsfDocument lateDoc = load("video_late.tsf");
        MediaClip late;
        QVERIFY(MediaClip::parse(lateDoc.webcam, late));
        r = reportWith(lateDoc, late);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
        QVERIFY(r.packetsLate > 0);

        // The clip's origin (under no hash) slid so the whole video reads earlier than its stamps: caught too - the
        // window is two-sided, not only "from the future".
        MediaClip early = clip;
        early.originUs += 10000000;
        r = reportWith(doc, early);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
        QVERIFY(r.packetsLate > 0);
    }

    void cache()
    {
        // The checks kept between reports: the same report; a stamp added later is checked on its own.
        const TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        const KeyRecords normalized = Recalc::normalized(doc.records);
        Stamps::Cache cache;
        QList<Stamp> first = doc.stamps.mid(0, 2);
        Stamps::Report r = Stamps::verify(normalized, first, doc.stampCertificates, &clip, DocTime::modelOffset(doc.records),
                                          {}, &cache);
        QCOMPARE(cache.entries.size(), 2);
        r = Stamps::verify(normalized, doc.stamps, doc.stampCertificates, &clip, DocTime::modelOffset(doc.records), {},
                           &cache);
        QCOMPARE(cache.entries.size(), 3);
        const Stamps::Report plain = reportWith(doc, clip);
        QCOMPARE(r.status, plain.status);
        QCOMPARE(r.confirmed, plain.confirmed);
        QCOMPARE(r.packetsStamped, plain.packetsStamped);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
    }

    void unknownVersion()
    {
        // A file made entirely of stamps this program does not check (the unreleased v2, a newer one): it is simply
        // from a newer program - the records are not stamped, but nothing is broken.
        TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        TsfDocument whole = doc;
        for (Stamp &s : whole.stamps)
            s.version = 4;
        whole = Tsf::parse(Tsf::serialize(whole, true));
        Stamps::Report r = reportWith(whole, clip);
        QCOMPARE(r.unknown, whole.stamps.size());
        QCOMPARE(r.bad, 0);
        QVERIFY(r.status != Stamps::Report::Status::Broken);

        // But an uncheckable stamp mixed in with checkable ones is tampering (a stamp relabelled to a version that is
        // skipped, to turn a broken chain into a reassuring "partial"): it must break the chain.
        TsfDocument mixed = doc;
        mixed.stamps[0].version = 4;
        mixed = Tsf::parse(Tsf::serialize(mixed, true));
        QCOMPARE(mixed.stamps[0].version, 4);
        r = reportWith(mixed, clip);
        QVERIFY(r.bad > 0);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
    }

    void liveChainV3()
    {
        // The chain v3 record by record, with the packets of each stamp: the imprints that were stamped.
        const TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        Stamps::Chain chain;
        for (const KeyRecord &r : doc.records)
            QVERIFY(chain.push(r));
        QByteArray previous;
        int from = 0, media = 0;
        for (const Stamp &s : doc.stamps) {
            QByteArray hashes;
            for (int i = media; i < s.mediaEnd; ++i)
                hashes += Stamps::packetHash(clip.packets[i]);
            const QByteArray imprint = TimeStamp::info(s.token)->imprint;
            QCOMPARE(chain.hashV3(previous, from, s.end, s.delayMs, s.key, hashes, s.mediaEnd - media), imprint);
            previous = imprint;
            from = s.end;
            media = s.mediaEnd;
        }
    }

    void blocks_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::newRow("middle") << "video.tsf" << 30 << 120;
        QTest::newRow("start") << "video.tsf" << 0 << 40;
        QTest::newRow("end") << "video.tsf" << 130 << 176;
        QTest::newRow("in one chunk") << "video.tsf" << 100 << 110;
        QTest::newRow("group edges") << "video.tsf" << 8 << 96;
        QTest::newRow("v1") << "stamped.tsf" << 30 << 120;
        QTest::newRow("v1 end") << "stamped.tsf" << 95 << 176;
    }

    void blocks()
    {
        // "Сохранить блок": the stamps of the records and packets of the block, the rest hidden; through the file.
        QFETCH(QString, file);
        QFETCH(int, from);
        QFETCH(int, to);
        const TsfDocument doc = load(file.toLatin1().constData());
        MediaClip clip;
        const bool video = MediaClip::parse(doc.webcam, clip);
        const KeyRecords normalized = Recalc::normalized(doc.records);
        while (normalized[from].isUp()) // the block starts at a press (MainWindow::documentForSave)
            ++from;
        QList<int> kept;
        MediaClip cut;
        if (video) {
            const qint64 offset = DocTime::modelOffset(doc.records);
            cut = clip.cut(DocTime::of(normalized, from) + offset, DocTime::of(normalized, to - 1) + offset, &kept);
            cut.moveOrigin(-qint64(normalized[from].dtUs));
        }
        int revealed = 0;
        TsfDocument block;
        block.records = normalized.mid(from, to - from);
        block.stamps = Stamps::extract(doc.stamps, normalized, video ? &clip : nullptr, from, to, kept, &revealed);
        block.stampCertificates = doc.stampCertificates;
        if (video)
            block.webcam = cut.serialize();
        QVERIFY(!block.stamps.isEmpty());
        // Records outside go as they are only for v1; v3 - as hashes of salted records, and the salts only of the
        // block's records (the key only with a stamp all of whose records are in it).
        if (file.startsWith(u"stamped")) {
            QVERIFY(revealed > 0);
        } else {
            QCOMPARE(revealed, 0);
            for (const Stamp &s : block.stamps) {
                bool hides = false;
                for (const StampPart &p : s.parts) {
                    QVERIFY(p.kind != StampPart::Hidden);
                    hides |= p.kind == StampPart::Leaf || p.kind == StampPart::Commitments;
                    if (p.kind == StampPart::Records)
                        QCOMPARE(p.data.size(), s.key.isEmpty() ? qsizetype(p.count) * Stamps::kSaltSize : 0);
                }
                if (hides)
                    QVERIFY(s.key.isEmpty());
            }
        }
        const TsfDocument back = Tsf::parse(Tsf::serialize(block, true));
        QCOMPARE(back.stamps.size(), block.stamps.size());
        for (int i = 0; i < block.stamps.size(); ++i) {
            QCOMPARE(back.stamps[i].parts, block.stamps[i].parts);
            QCOMPARE(back.stamps[i].previous, block.stamps[i].previous);
            QCOMPARE(back.stamps[i].mediaEnd, block.stamps[i].mediaEnd);
            QCOMPARE(back.stamps[i].key, block.stamps[i].key);
        }
        MediaClip backClip;
        if (video)
            QVERIFY(MediaClip::parse(back.webcam, backClip));
        const Stamps::Report r = reportWith(back, backClip);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.confirmed, int(back.records.size()));
        if (video)
            QCOMPARE(r.packetsStamped, int(backClip.packets.size()));
        QCOMPARE(r.hiddenBeforeUs > 0, from > 0);
        QCOMPARE(r.hiddenAfterUs > 0, to < normalized.size());

        // A hidden group's duration or a hidden record changed: broken.
        TsfDocument forged = back;
        bool touched = false;
        for (Stamp &s : forged.stamps)
            for (StampPart &p : s.parts)
                if (!touched && p.kind == StampPart::Leaf) {
                    p.durationUs += 1000;
                    touched = true;
                } else if (!touched && (p.kind == StampPart::Hidden || p.kind == StampPart::Commitments)) {
                    p.data[0] = char(p.data[0] ^ 1);
                    touched = true;
                }
        if (touched)
            QCOMPARE(reportWith(forged, backClip).status, Stamps::Report::Status::Broken);
        // The hidden time of a cut group or a salt changed: broken.
        forged = back;
        touched = false;
        for (Stamp &s : forged.stamps)
            for (StampPart &p : s.parts)
                if (!touched && p.kind == StampPart::Commitments) {
                    p.durationUs += 1000;
                    touched = true;
                } else if (!touched && p.kind == StampPart::Records && !p.data.isEmpty()) {
                    p.data[0] = char(p.data[0] ^ 1);
                    touched = true;
                }
        if (touched)
            QCOMPARE(reportWith(forged, backClip).status, Stamps::Report::Status::Broken);
        // A record of the block changed: broken.
        TsfDocument edited = back;
        edited.records[2].ch = u'Z';
        QCOMPARE(reportWith(edited, backClip).status, Stamps::Report::Status::Broken);
    }

    void followV3()
    {
        // An edit voids the stamp of its records, which keeps them as leaves: its packets stay stamped.
        TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        KeyRecords recs = Recalc::normalized(doc.records);
        for (int i = 0; i < recs.size(); ++i)
            recs[i].tag = quint32(i + 1);
        const KeyRecords before = recs;
        Editing::deleteRange(recs, 120, 124);
        Stamps::follow(doc.stamps, before, recs);
        QVERIFY(!doc.stamps[0].voided && !doc.stamps[1].voided && doc.stamps[2].voided);
        QVERIFY(!doc.stamps[2].parts.isEmpty());
        doc.records = recs;
        Stamps::Report r = reportWith(doc, clip);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.voided, 1);
        QCOMPARE(r.confirmed, 89);
        QCOMPARE(r.packetsStamped, 206);
        // Through the file.
        const TsfDocument back = Tsf::parse(Tsf::serialize(doc, true));
        QCOMPARE(reportWith(back, clip).packetsStamped, 206);
    }

    void forgedParts()
    {
        // Parts a stamp may have only where the program writes them: records not under a stamp only in a voided one,
        // hidden records only at the edges of a block.
        const TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        const KeyRecords normalized = Recalc::normalized(doc.records);
        QVERIFY(doc.stamps.size() >= 3);
        const int from = doc.stamps[0].end, len = doc.stamps[1].end - from, a = len / 2;
        const int packets = doc.stamps[1].mediaEnd - doc.stamps[0].mediaEnd;
        QVERIFY(len > 4);

        // Records slipped into a stamp that is not voided as "not under it".
        TsfDocument inserted = doc;
        inserted.records = normalized;
        KeyRecord fake;
        fake.dtUs = 1000;
        fake.flags = 0x58u << 16;
        fake.ch = u'x';
        for (int i = 0; i < 3; ++i)
            inserted.records.insert(from + a, fake);
        inserted.stamps[1].parts = {{StampPart::Records, a, 0, {}},
                                    {StampPart::Unstamped, 3, 0, {}},
                                    {StampPart::Records, len - a, 0, {}},
                                    {StampPart::Packets, packets, 0, {}}};
        for (int k = 1; k < inserted.stamps.size(); ++k)
            inserted.stamps[k].end += 3;
        Stamps::Report r = reportWith(inserted, clip);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
        // The same at the end of the last stamp.
        TsfDocument appended = doc;
        appended.records = normalized;
        for (int i = 0; i < 3; ++i)
            appended.records.append(fake);
        Stamp &final = appended.stamps.last();
        final.parts = {{StampPart::Records, final.end - appended.stamps[appended.stamps.size() - 2].end, 0, {}},
                       {StampPart::Unstamped, 3, 0, {}},
                       {StampPart::Packets, final.mediaEnd - appended.stamps[appended.stamps.size() - 2].mediaEnd, 0, {}}};
        final.end += 3;
        r = reportWith(appended, clip);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);

        // A record in the middle hidden (a typo and its BackSpace taken out): the stamp still checks, but such a chain
        // is not a block of the recording.
        TsfDocument hidden = doc;
        hidden.records = normalized;
        const KeyRecord gone = hidden.records.takeAt(from + a);
        const QByteArray c = Stamps::commitment(Stamps::salt(doc.stamps[1].key, quint32(a)), Stamps::encode(gone));
        hidden.stamps[1].parts = {{StampPart::Records, a, 0, {}},
                                  {StampPart::Commitments, 1, gone.dtUs, c},
                                  {StampPart::Records, len - a - 1, 0, {}},
                                  {StampPart::Packets, packets, 0, {}}};
        for (int k = 1; k < hidden.stamps.size(); ++k)
            hidden.stamps[k].end -= 1;
        r = reportWith(hidden, clip);
        QCOMPARE(r.status, Stamps::Report::Status::Broken);
        // The same hidden record at the end of the last stamp is a block's edge: fine.
        TsfDocument tail = doc;
        tail.records = normalized;
        const int last = int(tail.stamps.size()) - 1, lastFrom = tail.stamps[last - 1].end;
        const int lastLen = tail.stamps[last].end - lastFrom;
        const KeyRecord end = tail.records.takeLast();
        tail.stamps[last].parts = {{StampPart::Records, lastLen - 1, 0, {}},
                                   {StampPart::Commitments, 1, end.dtUs,
                                    Stamps::commitment(Stamps::salt(doc.stamps[last].key, quint32(lastLen - 1)),
                                                       Stamps::encode(end))},
                                   {StampPart::Packets, tail.stamps[last].mediaEnd - tail.stamps[last - 1].mediaEnd, 0, {}}};
        tail.stamps[last].end -= 1;
        r = reportWith(tail, clip);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.status, Stamps::Report::Status::Confirmed);

        // A voided stamp that leaves no records in the document: not all of the recording is confirmed.
        TsfDocument voided = doc;
        KeyRecords recs = normalized;
        for (int i = 0; i < recs.size(); ++i)
            recs[i].tag = quint32(i + 1);
        const KeyRecords before = recs;
        recs.remove(doc.stamps[last - 1].end, doc.stamps[last].end - doc.stamps[last - 1].end);
        Stamps::follow(voided.stamps, before, recs);
        QVERIFY(voided.stamps[last].voided);
        voided.records = recs;
        r = reportWith(voided, clip);
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.voided, 1);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
    }

    void videoHoles()
    {
        // A stretch taken out of the middle of the video (the packets as hidden ones): the hashes still check, but the
        // shown packets around the hole are too far apart for a block's cut.
        const TsfDocument doc = load("video.tsf");
        MediaClip clip;
        QVERIFY(MediaClip::parse(doc.webcam, clip));
        int k = 0; // the stamp with the most packets
        for (int i = 1; i < doc.stamps.size(); ++i)
            if (doc.stamps[i].mediaEnd - doc.stamps[i - 1].mediaEnd > doc.stamps[k].mediaEnd - (k ? doc.stamps[k - 1].mediaEnd : 0))
                k = i;
        const int m0 = k ? doc.stamps[k - 1].mediaEnd : 0, m1 = doc.stamps[k].mediaEnd;
        const int r0 = k ? doc.stamps[k - 1].end : 0;
        const auto isVideo = [&clip](int i) { return clip.streams[clip.packets[i].stream].kind == MediaStream::Video; };
        // A second of the chunk's middle.
        const qint64 mid = (clip.packets[m0].ptsUs + clip.packets[m1 - 1].ptsUs) / 2;
        const auto hide = [&](bool videoOnly) {
            TsfDocument forged = doc;
            forged.records = Recalc::normalized(doc.records);
            MediaClip cut = clip;
            cut.packets.clear();
            Stamp &s = forged.stamps[k];
            s.parts = {{StampPart::Records, s.end - r0, 0, {}}};
            int hidden = 0;
            for (int i = 0; i < clip.packets.size(); ++i) {
                const MediaPacket &p = clip.packets[i];
                const bool inHole = i >= m0 && i < m1 && p.ptsUs >= mid && p.ptsUs < mid + 1000000 && (!videoOnly || isVideo(i));
                if (i >= m0 && i < m1) {
                    if (inHole)
                        s.parts.append({StampPart::HiddenPacket, 0, 0, Stamps::packetHash(p)});
                    else
                        s.parts.append({StampPart::Packets, 1, 0, {}});
                }
                if (inHole)
                    ++hidden;
                else
                    cut.packets.append(p);
            }
            for (int i = k; i < forged.stamps.size(); ++i)
                forged.stamps[i].mediaEnd -= hidden;
            forged.webcam = cut.serialize();
            const TsfDocument back = Tsf::parse(Tsf::serialize(forged, true));
            MediaClip backClip;
            Q_ASSERT(MediaClip::parse(back.webcam, backClip));
            return std::pair{hidden, reportWith(back, backClip)};
        };
        for (const bool videoOnly : {true, false}) {
            const auto [hidden, r] = hide(videoOnly);
            QVERIFY(hidden > 5);
            QVERIFY(r.packetsGaps > 0);
            QCOMPARE(r.status, Stamps::Report::Status::Broken);
        }
    }
};

QTEST_GUILESS_MAIN(TstStamps)
#include "tst_stamps.moc"
