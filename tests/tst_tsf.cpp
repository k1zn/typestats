#include "core/TsfFile.h"

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

    void newerVersionWarns()
    {
        Tsf::ReadError err;
        Tsf::parse({QStringLiteral("tsfVersion=2")}, &err);
        QCOMPARE(err, Tsf::ReadError::NewerVersion);
    }
};

QTEST_GUILESS_MAIN(TstTsf)
#include "tst_tsf.moc"
