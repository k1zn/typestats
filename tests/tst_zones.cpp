// Finger zones (re/finger_zones.md) and the INI reader they are stored with.

#include "core/Cp1251.h"
#include "core/FingerZones.h"
#include "core/IniFile.h"

#include <QTemporaryDir>
#include <QTest>

class TstZones : public QObject
{
    Q_OBJECT
private slots:
    void standardScheme()
    {
        const FingerZones z = FingerZones::standard();
        QVERIFY(z.readOnly());
        QCOMPARE(z.finger(0x1E), quint8(0)); // A: left little, home
        QVERIFY(z.isHome(0x1E));
        QVERIFY(!z.isHome(0x2C));
        QCOMPARE(z.finger(0x36), quint8(7)); // RShift
        QCOMPARE(z.finger(0x39), FingerZones::kNone); // space
        QCOMPARE(z.finger(0x40000000u | 0x24), quint8(4)); // flags: only scan & 0x7f counts
        QCOMPARE(z.toString(0), QStringLiteral("1E2C1029022A"));
        QCOMPARE(FingerZones::fromStrings(z.toStrings()), z);
    }

    void parseLikeStrToIntDef()
    {
        // odd trailing digit dropped, junk pair -> scan 0, >= 0x80 skipped
        const FingerZones z = FingerZones::fromStrings({QStringLiteral("1e2cZZ9F3"), QStringLiteral("20")});
        QCOMPARE(z.keys(0), (QVector<quint8>{0x1E, 0x2C, 0x00}));
        QCOMPARE(z.finger(0x00), quint8(0));
        QCOMPARE(z.finger(0x1F), FingerZones::kNone);
        QCOMPARE(z.keys(1), QVector<quint8>{0x20});
        QCOMPARE(z.toString(0), QStringLiteral("1E2C00"));
    }

    void editor()
    {
        FingerZones z = FingerZones::standard();
        z.assign(0x39, 3, false); // space -> left index, appended
        QCOMPARE(z.finger(0x39), quint8(3));
        QVERIFY(!z.isHome(0x39));
        z.assign(0x39, 3, true); // now its home key
        QVERIFY(z.isHome(0x39));
        QCOMPARE(z.keys(3).count(quint8(0x39)), 1);
        z.assign(0x39, FingerZones::kNone, false);
        QCOMPARE(z.finger(0x39), FingerZones::kNone);
        QCOMPARE(z, FingerZones::standard());
        // Equality ignores the order of non-home keys.
        FingerZones a = FingerZones::standard();
        a.assign(0x2C, 0, false); // same finger, not home: no change
        QCOMPARE(a.toString(0), FingerZones::standard().toString(0));
        a.assign(0x29, 1, false);
        a.assign(0x29, 0, false); // moved to the end
        QVERIFY(a.toString(0) != FingerZones::standard().toString(0));
        QCOMPARE(a, FingerZones::standard());
    }

    void iniCp1251AndUtf8()
    {
        const QString text = QStringLiteral("[Моя]\r\nFinger0 = 1E2C\r\n; comment\r\nfinger0=00\r\n[other]\r\nK=\"v\"\r\n");
        for (const QByteArray &bytes : {Cp1251::encode(text), text.toUtf8()}) {
            IniFile ini;
            ini.parse(IniFile::decode(bytes));
            QCOMPARE(ini.sections(), (QStringList{QStringLiteral("Моя"), QStringLiteral("other")}));
            QCOMPARE(ini.value(QStringLiteral("моя"), QStringLiteral("FINGER0")), QStringLiteral("1E2C"));
            QCOMPARE(ini.value(QStringLiteral("other"), QStringLiteral("k")), QStringLiteral("v"));
        }
    }

    void iniSavedAsAnsi()
    {
        // Written as the original reads it (cp1251); UTF-8 only when cp1251 has no place for a character.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("z.ini"));
        IniFile ini;
        ini.parse(QStringLiteral("[Моя схема]\nFinger0=1E\n"));
        QVERIFY(ini.save(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        f.close();
        QCOMPARE(bytes, Cp1251::encode(ini.toString()));
        IniFile back;
        QVERIFY(back.load(path));
        QCOMPARE(back.sections(), QStringList{QStringLiteral("Моя схема")});

        ini.parse(QStringLiteral("[日本]\nFinger0=1E\n"));
        QVERIFY(ini.save(path));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), ini.toString().toUtf8());
        f.close();
        QVERIFY(back.load(path));
        QCOMPARE(back.sections(), QStringList{QStringLiteral("日本")});
    }

    void schemes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("FingerZones.ini"));
        FingerZones mine = FingerZones::standard();
        mine.assign(0x39, 3, true);
        {
            FingerZoneSchemes s(path);
            QCOMPARE(s.names(), QStringList{QStringLiteral("Стандарт")});
            // A file with the standard scheme under another name maps to "Стандарт".
            QCOMPARE(s.adopt(QStringLiteral("X"), FingerZones::standard()), QStringLiteral("Стандарт"));
            QCOMPARE(s.adopt(QStringLiteral("Стандарт"), mine), QStringLiteral("Стандарт_"));
            QCOMPARE(s.adopt(QStringLiteral("Y"), mine), QStringLiteral("Стандарт_"));
        }
        FingerZoneSchemes s(path);
        QCOMPARE(s.names(), (QStringList{QStringLiteral("Стандарт"), QStringLiteral("Стандарт_")}));
        QCOMPARE(s.zones(QStringLiteral("Стандарт_")), mine);
        QVERIFY(!s.zones(QStringLiteral("Стандарт_")).readOnly());
        s.remove(QStringLiteral("Стандарт_"));
        QCOMPARE(FingerZoneSchemes(path).names().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstZones)
#include "tst_zones.moc"
