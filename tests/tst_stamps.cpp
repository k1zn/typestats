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

} // namespace

class TstStamps : public QObject
{
    Q_OBJECT

private slots:
    void init() { TimeStamp::clearCache(); }

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
        const std::optional<Der::Node> req = Der::parse(TimeStamp::request(hash, 0x1234, true));
        QVERIFY(req && req->tag == Der::Sequence);
        Der::Reader r(req->content);
        const auto version = r.next(Der::Integer), imprint = r.next(Der::Sequence), nonce = r.next(Der::Integer),
                   certReq = r.next(Der::Boolean);
        QVERIFY(version && imprint && nonce && certReq && r.atEnd());
        QCOMPARE(Der::unsignedInteger(*nonce).toByteArray().toHex(), QByteArray("1234"));
        QVERIFY(imprint->content.toByteArray().endsWith(hash));
        // certReq FALSE is the default: DER leaves it out.
        Der::Reader plain(Der::parse(TimeStamp::request(hash, 1, false))->content);
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

    void compressedTime()
    {
        // The records claim 15 % faster typing than the real one: the hashes match, the time does not.
        const Stamps::Report r = report(load("compressed.tsf"));
        QCOMPARE(r.bad, 0);
        QCOMPARE(r.stamped, 176);
        QCOMPARE(r.status, Stamps::Report::Status::Partial);
        QVERIFY(r.confirmed < 100);
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
        Editing::deleteRange(recs, 120, 124);
        Stamps::follow(doc.stamps, recs);
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
        Editing::deleteRange(recs, 30, 32);
        Stamps::follow(doc.stamps, recs);
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
};

QTEST_GUILESS_MAIN(TstStamps)
#include "tst_stamps.moc"
