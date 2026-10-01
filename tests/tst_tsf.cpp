#include "core/TsfFile.h"

#include <QDir>
#include <QFile>
#include <QTest>

class TstTsf : public QObject
{
    Q_OBJECT
private slots:
    void dataLineFormat()
    {
        KeyRecord r;
        r.dtUs = 0x1A2B3;
        r.flags = 0x0D410000 | 0x1E | KeyRecord::Transient; // vk 'A', scan 0x1E
        r.ch = u'ф';
        QCOMPARE(Tsf::dataLine(r), QStringLiteral("0001A2B3 04440D41001E"));
        r.comment = QStringLiteral("метка");
        QCOMPARE(Tsf::dataLine(r), QStringLiteral("0001A2B3 04440D41001E\t;метка"));
    }

    void roundTrip()
    {
        TsfDocument d;
        for (int i = 0; i < 5; ++i) {
            KeyRecord r;
            r.dtUs = 1000 * i + 7;
            r.flags = 0x01000000u | (0x41u + i) << 16 | (0x1Eu + i);
            r.ch = char16_t(u'a' + i);
            if (i == 2)
                r.comment = QStringLiteral("x;y");
            d.records.append(r);
        }
        d.author = QStringLiteral("Автор");
        d.comment = QStringLiteral("line1\r\nline2");
        d.date = QStringLiteral("29.09.2026 12:00:00");
        d.fingerZonesName = QStringLiteral("Стандарт");
        for (int i = 0; i < 8; ++i)
            d.fingers.append(QString::number(i));

        const QStringList lines = Tsf::serialize(d, false);
        QCOMPARE(lines.value(5), QStringLiteral("tsfVersion=1"));
        Tsf::ReadError err;
        TsfDocument back = Tsf::parse(lines, &err);
        QCOMPARE(err, Tsf::ReadError::None);
        QCOMPARE(back.records.size(), 5);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(back.records[i].dtUs, d.records[i].dtUs);
            QCOMPARE(back.records[i].flags, d.records[i].flags);
            QCOMPARE(back.records[i].ch, d.records[i].ch);
            QCOMPARE(back.records[i].comment, d.records[i].comment);
        }
        QCOMPARE(back.author, d.author);
        QCOMPARE(back.comment, QStringLiteral("line1line2"));
        QCOMPARE(back.date, d.date);
        QCOMPARE(back.fingerZonesName, d.fingerZonesName);
        QCOMPARE(back.fingers, d.fingers);
    }

    void headerLinesAreNotData()
    {
        const QStringList lines = {QStringLiteral("date=12.03.2016"), QStringLiteral("Finger0=abc"),
                                   QStringLiteral("AttachedVideo=a.avi"), QStringLiteral("tsfVersion=1")};
        QCOMPARE(Tsf::parse(lines).records.size(), 0);
    }

    void edgeCases()
    {
        // sscanf("%x %s") and TStrings::Values as the original reads them.
        const QStringList lines = {
            QStringLiteral("  0001A2B3 04440D41001E"),
            QStringLiteral("0x0001A2B3 04440D41001E\t;комм=ент;с;точкой"),
            QStringLiteral("1a2b3 0000000041001e"),
            QStringLiteral("FFFFFFFFF 04440D41001E"),          // dt wraps around
            QStringLiteral("0001A2B3 1234567890ABCDEF0"),      // more than 16 digits: not data
            QStringLiteral("0001A2B3 G4440D41001E"),
            QStringLiteral("0001A2B3"),
            QStringLiteral("zzz 123"),
            QStringLiteral("AUTOR=Первый"),
            QStringLiteral("autor=второй"),                    // the first one counts
            QStringLiteral("Date=01.01.2020"),
            QStringLiteral("comment=описание = с равно"),
            QStringLiteral("FingerZonesName=Своя"),
            QStringLiteral("finger0=1"), QStringLiteral("Finger1=2"), QStringLiteral("FINGER7=8"),
            QStringLiteral("AttachedVideo=v.avi"), QStringLiteral("VideoTimeShift=-250"),
            QStringLiteral("tsfVersion=1"),
            QStringLiteral("signature=0123"),
            QStringLiteral("\t00000010   0000000041001E   хвост ; коммент"),
        };
        const TsfDocument d = Tsf::parse(lines);
        const QList<std::tuple<quint32, quint32, int, QString>> expected = {
            {0x1A2B3, 0x0D41001E, 0x444, {}},
            {0x1A2B3, 0x0D41001E, 0x444, QStringLiteral("комм=ент;с;точкой")},
            {0x1A2B3, 0x41001E, 0, {}},
            {0xFFFFFFFF, 0x0D41001E, 0x444, {}},
            {0x10, 0x41001E, 0, QStringLiteral(" коммент")},
        };
        QCOMPARE(d.records.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            const auto &[dt, flags, ch, comment] = expected[i];
            QCOMPARE(d.records[i].dtUs, dt);
            QCOMPARE(d.records[i].flags, flags);
            QCOMPARE(int(d.records[i].ch), ch);
            QCOMPARE(d.records[i].comment, comment);
        }
        QCOMPARE(d.author, QStringLiteral("Первый"));
        QCOMPARE(d.date, QStringLiteral("01.01.2020"));
        QCOMPARE(d.comment, QStringLiteral("описание = с равно"));
        QCOMPARE(d.fingers, (QStringList{"1", "2", "", "", "", "", "", "8"}));
        QCOMPARE(d.videoTimeShiftMs, -250);
        QVERIFY(d.signed_);
        QVERIFY(!d.signatureValid);
    }

    void version0()
    {
        // "%d %d": dt, key with ANSI char in the low byte and vk in bits 16..23
        const QStringList lines = {QStringLiteral("1500 %1").arg(0x01410000 | 0xE0)}; // 'A' vk, cp1251 'а'
        TsfDocument d = Tsf::parse(lines);
        QCOMPARE(d.version, 0);
        QCOMPARE(d.records.size(), 1);
        QCOMPARE(d.records[0].dtUs, 1500u);
        QCOMPARE(d.records[0].ch, u'а');
        QCOMPARE(d.records[0].scan(), 0x1E);
        QCOMPARE(d.records[0].vk(), 0x41);
    }

    void goldenFiles_data()
    {
        QTest::addColumn<QString>("file");
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")}))
            QTest::newRow(qPrintable(f)) << QStringLiteral(TS_GOLDEN_DIR "/") + f;
    }

    void goldenFiles()
    {
        QFETCH(QString, file);
        TsfDocument d;
        QCOMPARE(Tsf::read(file, d), Tsf::ReadError::None);
        QVERIFY(d.records.size() > 10);
        QVERIFY(d.signed_);
        QVERIFY2(d.signatureValid, "MD5 signature mismatch");
        // Re-serialising must reproduce the original file byte for byte.
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray original = f.readAll();
        const QString tmp = QDir::temp().filePath(QStringLiteral("ts_roundtrip.tsf"));
        QVERIFY(Tsf::write(tmp, d, true));
        QFile g(tmp);
        QVERIFY(g.open(QIODevice::ReadOnly));
        QCOMPARE(g.readAll(), original);
    }

    void newerVersionWarns()
    {
        Tsf::ReadError err;
        Tsf::parse({QStringLiteral("tsfVersion=2")}, &err);
        QCOMPARE(err, Tsf::ReadError::NewerVersion);
    }
};

QTEST_GUILESS_MAIN(TstTsf)
#include "tst_tsf.moc"
