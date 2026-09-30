// The journal (.tsj, re/journal.md).

#include "core/Journal.h"
#include "core/TsfFile.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

namespace {

KeyRecord record(quint32 dt, quint32 flags, char16_t ch, const QString &comment = {})
{
    KeyRecord r;
    r.dtUs = dt;
    r.flags = flags;
    r.ch = ch;
    r.comment = comment;
    return r;
}

void compare(const KeyRecords &a, const KeyRecords &b)
{
    QCOMPARE(a.size(), b.size());
    for (int i = 0; i < a.size(); ++i) {
        QCOMPARE(a[i].dtUs, b[i].dtUs);
        QCOMPARE(a[i].flags, b[i].flags);
        QCOMPARE(a[i].ch, b[i].ch);
        QCOMPARE(a[i].comment, b[i].comment);
    }
}

} // namespace

class TstJournal : public QObject
{
    Q_OBJECT
private slots:
    void names()
    {
        QCOMPARE(Journal::fileName(QDate(2026, 9, 30)), QStringLiteral("2026_9.tsj"));
        QCOMPARE(Journal::fileName(QDate(2016, 12, 1)), QStringLiteral("2016_12.tsj"));
        QVERIFY(Journal::isJournal(QStringLiteral("C:/ts/2026_9.TSJ")));
        QVERIFY(!Journal::isJournal(QStringLiteral("C:/ts/text.tsf")));
    }

    void recordLayout()
    {
        const KeyRecord r = record(0x1A2B3, 0x0D41001E, u'ф');
        // dt ^ 0x554973, flags, character, no comment
        QCOMPARE(Journal::encode(r).toHex(), QByteArray("c0eb5400" "1e00410d" "44040000" "00"));
        const KeyRecord c = record(0, 0x80002000, 0, QStringLiteral("01.02.2016 Блокнот"));
        const QByteArray bytes = Journal::encode(c);
        QCOMPARE(bytes.size(), 13 + 18);
        QCOMPARE(bytes.left(4).toHex(), QByteArray("73495500"));
        QCOMPARE(int(bytes[12]), 18);
        QCOMPARE(bytes.mid(13, 11), QByteArray("01.02.2016 "));
        QCOMPARE(quint8(bytes[24]), quint8(0xC1)); // 'Б' in cp1251
    }

    void roundTripAndBrokenTail()
    {
        const KeyRecords recs{record(60000000, 0x01410000 | 0x1E, u'a'), record(51234, 0x0041201E | 0x8000, 0),
                              record(7, 0x01200039, u' ', QStringLiteral("комментарий; с точкой")),
                              record(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF)};
        QByteArray bytes;
        for (const KeyRecord &r : recs)
            bytes += Journal::encode(r);
        compare(Journal::decode(bytes), recs);
        // A record cut anywhere is dropped, the ones before it stay.
        const qsizetype third = Journal::encode(recs[0]).size() + Journal::encode(recs[1]).size();
        compare(Journal::decode(bytes.left(third + 5)), recs.mid(0, 2));
        compare(Journal::decode(bytes.left(third + 20)), recs.mid(0, 2));
        compare(Journal::decode(bytes.left(bytes.size() - 1)), recs.mid(0, 3));
        QVERIFY(Journal::decode({}).isEmpty());
    }

    void longCommentIsCut()
    {
        const KeyRecord r = record(1, 2, 3, QString(300, QLatin1Char('x')));
        const KeyRecords back = Journal::decode(Journal::encode(r) + Journal::encode(record(4, 5, 6)));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[0].comment, QString(255, QLatin1Char('x')));
        QCOMPARE(back[1].dtUs, 4u);
    }

    void writerAppendsByMonth()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        JournalWriter w(dir.path());
        QVERIFY(QDir(dir.path()).isEmpty()); // nothing is created before the first event
        const QDate sep(2026, 9, 30), oct(2026, 10, 1);
        QVERIFY(w.append(record(1, 0x01410000, u'a'), sep));
        QVERIFY(w.append(record(2, 0x01420000, u'b'), sep));
        QVERIFY(w.append(record(3, 0x01430000, u'c'), oct));
        QCOMPARE(w.path(sep), QDir(dir.path()).filePath(QStringLiteral("2026_9.tsj")));

        // Every event is on disk at once, while the writer still holds the file.
        KeyRecords recs;
        QVERIFY(Journal::read(w.path(sep), recs));
        QCOMPARE(recs.size(), 2);
        QCOMPARE(recs[1].ch, u'b');
        QVERIFY(Journal::read(w.path(oct), recs));
        QCOMPARE(recs.size(), 1);

        // A new session appends.
        w.close();
        JournalWriter w2(dir.path());
        QVERIFY(w2.append(record(4, 0x01440000, u'd'), oct));
        QVERIFY(Journal::read(w2.path(oct), recs));
        QCOMPARE(recs.size(), 2);
        QCOMPARE(recs[1].dtUs, 4u);
        QVERIFY(!Journal::read(dir.filePath(QStringLiteral("none.tsj")), recs));
    }

    void goldenFilesSurviveTheJournal_data()
    {
        QTest::addColumn<QString>("file");
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")}))
            QTest::newRow(qPrintable(f)) << QStringLiteral(TS_GOLDEN_DIR "/") + f;
    }

    void goldenFilesSurviveTheJournal()
    {
        QFETCH(QString, file);
        TsfDocument d;
        QCOMPARE(Tsf::read(file, d), Tsf::ReadError::None);
        QByteArray bytes;
        for (const KeyRecord &r : d.records)
            bytes += Journal::encode(r);
        compare(Journal::decode(bytes), d.records);
    }
};

QTEST_GUILESS_MAIN(TstJournal)
#include "tst_journal.moc"
