// Extra statistics (Form3, re/extra_stats.md) on synthetic text models.

#include "core/ExtraStats.h"
#include "core/FingerZones.h"

#include <QTemporaryDir>
#include <QTest>

using namespace ExtraStats;

namespace {

// One element per character, 100 ms apart. '|' before a character starts a fragment, '~' marks it
// erased, '\r' is Enter. Scan codes are not set: fingers are passed to collect() separately.
TextModel model(const QString &s, float pause = 100.0f)
{
    TextModel m;
    bool fragment = true, erased = false;
    for (QChar c : s) {
        if (c == u'|') {
            fragment = true;
        } else if (c == u'~') {
            erased = true;
        } else {
            m.names << QString(c);
            m.flags << (erased ? quint32(KeyRecord::Erased) : 0u);
            m.recIndex << m.names.size() - 1;
            m.pauses << (fragment ? kFragmentStart : pause);
            fragment = erased = false;
        }
    }
    return m;
}

QStringList texts(const QVector<Occurrence> &occ)
{
    QStringList r;
    for (const Occurrence &o : occ)
        r << QStringLiteral("%1:%2").arg(o.pos).arg(o.text);
    return r;
}

QStringList all(const QString &s, Kind kind, const QString &pattern = {}, const CharFilter &f = {},
                const QVector<quint8> &fingers = {})
{
    const TextModel m = model(s);
    return texts(collect(m, fingers, 0, m.size(), kind, pattern, f));
}

QStringList list(std::initializer_list<const char *> items)
{
    QStringList r;
    for (const char *s : items)
        r << QString::fromUtf8(s);
    return r;
}

} // namespace

class TstExtra : public QObject
{
    Q_OBJECT
private slots:
    void nGrams()
    {
        QCOMPARE(all(QStringLiteral("abcd"), Pairs), list({"0:ab", "1:bc", "2:cd"}));
        QCOMPARE(all(QStringLiteral("abcd"), Triples), list({"0:abc", "1:bcd"}));
        QCOMPARE(all(QStringLiteral("abcd"), Quads), list({"0:abcd"}));
        QCOMPARE(all(QStringLiteral("abc"), Quads), QStringList());
        // Erased elements and fragment borders break a combination; Enter is shown as [Enter].
        QCOMPARE(all(QStringLiteral("ab~cd|ef\rg"), Pairs), list({"0:ab", "4:ef", "5:f[Enter]", "6:[Enter]g"}));
    }

    void nGramSpeed()
    {
        TextModel m = model(QStringLiteral("abc"));
        m.pauses[2] = 50.0f;
        auto occ = collect(m, {}, 0, 3, Pairs);
        QCOMPARE(occ[0].speed, 600.0f);  // one character in 100 ms
        QCOMPARE(occ[1].speed, 1200.0f);
        QCOMPARE(collect(m, {}, 0, 3, Triples)[0].speed, 800.0f); // two characters in 150 ms
        m.pauses[1] = 10.0f; // not above the 10 ms threshold
        QCOMPARE(collect(m, {}, 0, 3, Pairs)[0].speed, 0.0f);
        // A sub-range.
        QCOMPARE(texts(collect(m, {}, 1, 3, Pairs)), list({"1:bc"}));
    }

    void words()
    {
        QCOMPARE(all(QStringLiteral("ab cd  e fgh"), Words), list({"0:ab", "3:cd", "9:fgh"}));
        // A word with an erased character is skipped by "Слова" and is the only one in "Слова с ошибками";
        // the erased character itself is not part of the text.
        const QString s = QStringLiteral("ab c~xd ef");
        QCOMPARE(all(s, Words), list({"0:ab", "7:ef"}));
        QCOMPARE(all(s, WordsWithErrors), list({"3:cd"}));
        // An erased space does not separate.
        QCOMPARE(all(QStringLiteral("ab~ cd ef"), WordsWithErrors), list({"0:abcd"}));
        // Enter and a fragment border end a word.
        QCOMPARE(all(QStringLiteral("ab\rcd|ef"), Words), list({"0:ab", "3:cd", "5:ef"}));
    }

    void wordSpeed()
    {
        const TextModel m = model(QStringLiteral("abc de"), 200.0f);
        const auto occ = collect(m, {}, 0, m.size(), Words);
        QCOMPARE(occ[0].speed, 300.0f); // 2 characters in 400 ms
        QCOMPARE(occ[1].speed, 300.0f); // 1 character in 200 ms: the pause before the word is not counted
    }

    void sentences()
    {
        QCOMPARE(all(QStringLiteral("Ab cd. Ef gh! x\rIj"), Sentences),
                 list({"0:Ab cd.", "7:Ef gh!", "16:Ij"}));
        // An erased character does not drop a sentence.
        QCOMPARE(all(QStringLiteral("Ab ~xcd."), Sentences), list({"0:Ab cd."}));
    }

    void templateParsing()
    {
        using T = TemplateItem;
        QCOMPARE(parseTemplate(QStringLiteral("a/б/Б/*/0/,/е/e/c/С/3/(/)//")),
                 (QVector<T>{{T::Literal, u'a'}, {T::RuLower, u'б'}, {T::RuUpper, u'Б'}, {T::Any, u'*'},
                             {T::Digit, u'0'}, {T::Punct, u','}, {T::Erased, u'е'}, {T::Erased, u'e'},
                             {T::LatLower, u'c'}, {T::LatUpper, u'С'}, {T::Finger, u'3'}, {T::Hand, u'('},
                             {T::Hand, u')'}, {T::Slash, u'/'}}));
        QCOMPARE(parseTemplate(QStringLiteral("/x")), (QVector<T>{{T::Literal, u'x'}}));
    }

    void templates()
    {
        QCOMPARE(all(QStringLiteral("ab1 c2,Дж"), Template, QStringLiteral("/c/0")), list({"1:b1", "4:c2"}));
        QCOMPARE(all(QStringLiteral("ab1 c2,Дж"), Template, QStringLiteral("/0/,")), list({"5:2,"}));
        QCOMPARE(all(QStringLiteral("ab1 c2,Дж"), Template, QStringLiteral("/Б/б")), list({"7:Дж"}));
        QCOMPARE(all(QStringLiteral("ab1 c2,Дж"), Template, QStringLiteral("b/*")), list({"1:b1"}));
        QCOMPARE(all(QStringLiteral("a/b"), Template, QStringLiteral("///c")), list({"1:/b"}));
        // Erased characters match only /е; Enter and fragment borders never match.
        QCOMPARE(all(QStringLiteral("a~bc"), Template, QStringLiteral("a/*")), QStringList());
        QCOMPARE(all(QStringLiteral("a~bc"), Template, QStringLiteral("a/е")), list({"0:ab"}));
        QCOMPARE(all(QStringLiteral("a\rb|cd"), Template, QStringLiteral("/*/*")), list({"3:cd"}));
        // Fingers and hands.
        const QVector<quint8> fingers{0, 3, 4, 8, 7};
        QCOMPARE(all(QStringLiteral("abcde"), Template, QStringLiteral("/1"), {}, fingers), list({"0:a"}));
        QCOMPARE(all(QStringLiteral("abcde"), Template, QStringLiteral("/(/)"), {}, fingers), list({"1:bc"}));
        QCOMPARE(all(QStringLiteral("abcde"), Template, QStringLiteral("/)"), {}, fingers), list({"2:c", "4:e"}));
        const TextModel m = model(QStringLiteral("abc"));
        QCOMPARE(collect(m, {}, 0, 3, Template, QStringLiteral("/*/*/*"))[0].speed, 600.0f);
    }

    void filter()
    {
        CharFilter f;
        QVERIFY(f.pass(u"abc"));
        f.onlyOn = true;
        f.only = QStringLiteral("abc");
        QVERIFY(f.pass(u"cab"));
        QVERIFY(!f.pass(u"abd"));
        f = {};
        f.anyOn = true;
        f.any = QStringLiteral("xy");
        QVERIFY(f.pass(u"axb"));
        QVERIFY(!f.pass(u"ab"));
        QVERIFY(!f.pass(u""));
        f.excludeOn = true;
        f.exclude = QStringLiteral("b");
        QVERIFY(!f.pass(u"axb"));
        QVERIFY(f.pass(u"ax"));
        QCOMPARE(all(QStringLiteral("abxb"), Pairs, {}, f), QStringList());
        f.excludeOn = false;
        QCOMPARE(all(QStringLiteral("abxb"), Pairs, {}, f), list({"1:bx", "2:xb"}));
    }

    void sorting()
    {
        const QVector<Occurrence> occ{{300, 0, "b"}, {100, 1, "a"}, {300, 2, "a"}, {200, 3, "c"}, {500, 4, "b"},
                                      {600, 5, "b"}};
        auto str = [](const QVector<Row> &rows) {
            QStringList r;
            for (const Row &x : rows)
                r << QStringLiteral("%1 %2 %3").arg(x.text).arg(x.speed).arg(x.value);
            return r.join(QLatin1String(", "));
        };
        // Stable: equal keys stay in text order.
        QCOMPARE(str(rows(occ, false, 0)), QStringLiteral("a 100 1, c 200 3, b 300 0, a 300 2, b 500 4, b 600 5"));
        QCOMPARE(str(rows(occ, false, 1)), QStringLiteral("a 100 1, a 300 2, b 300 0, b 500 4, b 600 5, c 200 3"));
        QCOMPARE(str(rows(occ, false, 0, true)),
                 QStringLiteral("b 600 5, b 500 4, a 300 2, b 300 0, c 200 3, a 100 1"));
        // Averages: value is the count.
        QCOMPARE(str(rows(occ, true, 0)), QStringLiteral("a 200 2, c 200 1, b 466.667 3"));
        QCOMPARE(str(rows(occ, true, 1)), QStringLiteral("a 200 2, b 466.667 3, c 200 1"));
        QCOMPARE(str(rows(occ, true, 2)), QStringLiteral("c 200 1, a 200 2, b 466.667 3"));
        QCOMPARE(str(rows(occ, true, 3)), QStringLiteral("b 466.667 3, a 200 2, c 200 1"));
        QCOMPARE(texts(occurrences(occ, QStringLiteral("b"))), list({"0:b", "4:b", "5:b"}));
        QCOMPARE(occurrences(occ, QStringLiteral("a"))[0].speed, 100.0f);
    }

    void sortState()
    {
        Sort s;
        QCOMPARE(s.headers(false), list({"▲Скорость", "Текст"}));
        s.clickColumn(0, false);
        QCOMPARE(s.headers(true), list({"▼Скорость", "Текст", "Кол-во"}));
        s.clickColumn(1, false); // another column keeps the direction
        QCOMPARE(s.headers(false), list({"Скорость", "▼Текст"}));
        s.clickColumn(1, false);
        QCOMPARE(s.headers(false), list({"Скорость", "▲Текст"}));
        s.clickColumn(2, false); // no such column without averages
        QCOMPARE(s.mode, 1);
        s.clickColumn(2, true);
        QCOMPARE(s.headers(true), list({"Скорость", "Текст", "▲Кол-во"}));
        s.clickColumn(2, true);
        QCOMPARE(s.mode, 3);
        QCOMPARE(s.headers(true), list({"Скорость", "Текст", "▼Кол-во"}));
        s.clickColumn(2, true);
        s.descending = true;
        s.clickColumn(2, true); // the count column resets the direction
        QVERIFY(!s.descending);
        s.setAverages(false);
        QCOMPARE(s.mode, 0);
    }

    void speedFormat()
    {
        const QLocale ru(QLocale::Russian), c(QLocale::C);
        QCOMPARE(formatSpeed(0.0f, ru), QStringLiteral("0,00"));
        QCOMPARE(formatSpeed(600.0f, ru), QStringLiteral("600,00"));
        QCOMPARE(formatSpeed(1234.567f, c), QStringLiteral("1234.57"));
        QCOMPARE(formatSpeed(0.004f, c), QStringLiteral("0.00"));
        QCOMPARE(formatSpeed(0.005f, c), QStringLiteral("0.00")); // the float is 0.00499999988…
        QCOMPARE(formatSpeed(0.006f, c), QStringLiteral("0.01"));
        QCOMPARE(formatSpeed(0.125f, c), QStringLiteral("0.13"));  // half away from zero
        QCOMPARE(formatSpeed(2.5e-4f, c), QStringLiteral("0.00"));
        QCOMPARE(formatSpeed(999.996f, c), QStringLiteral("1000.00"));
        QCOMPARE(formatSpeed(-1.5f, c), QStringLiteral("-1.50"));
        // No group separators; at most 8 significant digits.
        QCOMPARE(formatSpeed(123456.78f, ru), QStringLiteral("123456,78"));
        QCOMPARE(formatSpeed(12345678.0f, c), QStringLiteral("12345678.00"));
        QCOMPARE(formatSpeed(123456792.0f, c), QStringLiteral("123456790.00"));
    }

    void saveText()
    {
        const QVector<Row> r{{600, 2, "ab"}};
        QCOMPARE(toText(r, false, QLocale::c()), QStringLiteral("Текст\tСкорость\r\nab\t600.00\r\n"));
        QCOMPARE(toText(r, true, QLocale::c()), QStringLiteral("Текст\tСкорость\tКол-во\r\nab\t600.00\t2\r\n"));
    }

    void templateList()
    {
        const QStringList items = list({"/б/б", "a/*"});
        QCOMPARE(TemplateList::decode(TemplateList::encode(items)), items);
        QCOMPARE(TemplateList::decode(QStringLiteral("/б/б\r\na/*\r\n").toUtf8()), items);
        QCOMPARE(TemplateList::decode(QByteArray("/\xE1/\xE1\r\na/*")), items); // cp1251
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ExStats.ini"));
        {
            TemplateList t(path);
            QVERIFY(t.items().isEmpty());
            QVERIFY(t.add(items[0]));
            QVERIFY(t.add(items[1]));
            QVERIFY(!t.add(items[1]));
        }
        TemplateList t(path);
        QCOMPARE(t.items(), items);
        QVERIFY(t.remove(items[0]));
        QCOMPARE(TemplateList(path).items(), QStringList{items[1]});
    }
};

QTEST_GUILESS_MAIN(TstExtra)
#include "tst_extra.moc"
