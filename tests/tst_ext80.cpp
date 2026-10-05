// Ext80 (the software 80-bit format) against the x87 hardware: every result bit for bit. On a
// platform without the 80-bit long double only the self-checks run - Ext80 is then what the core uses.

#include "core/Ext80.h"

#include <QRandomGenerator>
#include <QTest>

#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

#if LDBL_MANT_DIG == 64
constexpr bool kHardware = true;

// The significand and the unbiased exponent of an x87 value; zero, inf and NaN apart.
bool same(const Ext80 &a, long double b)
{
    if (std::isnan(b))
        return isnan(a);
    if (std::signbit(b) != signbit(a))
        return false;
    if (b == 0)
        return a == Ext80(0);
    if (std::isinf(b))
        return !isfinite(a) && !isnan(a);
    unsigned char raw[sizeof(long double)] = {};
    std::memcpy(raw, &b, sizeof(b));
    std::uint64_t mant;
    std::uint16_t se;
    std::memcpy(&mant, raw, 8);
    std::memcpy(&se, raw + 8, 2);
    return a.significand() == mant && a.exponent() == (se & 0x7FFF) - 16383;
}
#else
constexpr bool kHardware = false;
#endif

// Numbers that make rounding hard: full 64-bit significands, ties, wide exponent gaps.
[[maybe_unused]] double randomDouble(QRandomGenerator &rnd) // the x87 comparison only
{
    switch (rnd.bounded(6)) {
    case 0: return (rnd.generateDouble() - 0.5) * std::pow(10.0, rnd.bounded(-8, 12));
    case 1: return double(float(rnd.generateDouble() * std::pow(10.0, rnd.bounded(-4, 8))));
    case 2: return double(qint64(rnd.generate64() >> rnd.bounded(1, 63))) * (rnd.bounded(2) ? 1 : -1);
    case 3: return std::ldexp(double(rnd.generate64() | 1), rnd.bounded(-120, 60));
    case 4: return double(rnd.bounded(-1000, 1000)) + 0.5 * rnd.bounded(3);
    default: {
        double d;
        quint64 bits = rnd.generate64();
        bits &= ~(quint64(0x7FF) << 52);
        bits |= quint64(rnd.bounded(1023 - 200, 1023 + 200)) << 52;
        std::memcpy(&d, &bits, 8);
        return d;
    }
    }
}

} // namespace

class TstExt80 : public QObject
{
    Q_OBJECT
private slots:
    void basics()
    {
        QCOMPARE(double(Ext80(0.1)), 0.1);
        QCOMPARE(float(Ext80(1) / Ext80(3)), 1.0f / 3.0f);
        QCOMPARE(qint64(Ext80(-7.9)), qint64(-7));
        QCOMPARE(quint64(Ext80(18446744073709551615ull)), 18446744073709551615ull);
        QCOMPARE(double(floor(Ext80(-0.5))), -1.0);
        QCOMPARE(double(floor(Ext80(2.999))), 2.0);
        QVERIFY(isnan(Ext80(0) / Ext80(0)));
        QVERIFY(!isfinite(Ext80(1) / Ext80(0)));
        QVERIFY(Ext80(-0.0) == Ext80(0.0));
        QVERIFY(Ext80(2) > Ext80(1) && Ext80(-2) < Ext80(-1) && Ext80(1) <= Ext80(1));
        // 0.001L of the original: 0x8312 6E97 8D4F DF3B, exponent -10.
        const Ext80 milli = Ext80(1) / Ext80(1000);
        QCOMPARE(milli.significand(), quint64(0x83126E978D4FDF3Bull));
        QCOMPARE(milli.exponent(), -10);
    }

    void againstHardware()
    {
        if (!kHardware)
            QSKIP("long double is not the x87 format here");
#if LDBL_MANT_DIG == 64
        QRandomGenerator rnd(80);
        int checked = 0;
        auto check = [&](const Ext80 &e, long double h, const char *what, double a, double b) {
            ++checked;
            if (!same(e, h))
                QFAIL(qPrintable(QStringLiteral("%1 %2 %3: %4 != %5").arg(QLatin1String(what)).arg(a, 0, 'g', 17)
                                     .arg(b, 0, 'g', 17).arg(double(e), 0, 'g', 17).arg(double(h), 0, 'g', 17)));
        };
        for (int i = 0; i < 300000; ++i) {
            const double a = randomDouble(rnd), b = randomDouble(rnd), c = randomDouble(rnd);
            const Ext80 ea(a), eb(b), ec(c);
            const long double ha = a, hb = b, hc = c;
            // Single operations, then chains on full 64-bit significands.
            check(ea + eb, ha + hb, "+", a, b);
            check(ea - eb, ha - hb, "-", a, b);
            check(ea * eb, ha * hb, "*", a, b);
            if (b != 0)
                check(ea / eb, ha / hb, "/", a, b);
            const Ext80 q = c != 0 ? ea / ec : ea * ec;
            const long double hq = c != 0 ? ha / hc : ha * hc;
            const Ext80 r = b != 0 ? eb / Ext80(7) : eb + Ext80(1);
            const long double hr = b != 0 ? hb / 7.0L : hb + 1.0L;
            check(q, hq, "q", a, c);
            check(q + r, hq + hr, "q+r", a, b);
            check(q - r, hq - hr, "q-r", a, b);
            check(q * r, hq * hr, "q*r", a, b);
            if (r != Ext80(0))
                check(q / r, hq / hr, "q/r", a, b);
            check(floor(q), std::floor(hq), "floor", a, c);
            check(floor(q * Ext80(1000) + Ext80(0.5)), std::floor(hq * 1000.0L + 0.5L), "round", a, c);
            if (float(q) != float(hq) && !(std::isnan(float(q)) && std::isnan(float(hq))))
                QFAIL(qPrintable(QStringLiteral("float %1").arg(a, 0, 'g', 17)));
            if (double(q) != double(hq) && !(std::isnan(double(q)) && std::isnan(double(hq))))
                QFAIL(qPrintable(QStringLiteral("double %1").arg(a, 0, 'g', 17)));
            if (std::fabs(hq) < 9e18 && qint64(q) != qint64(hq))
                QFAIL(qPrintable(QStringLiteral("int %1").arg(a, 0, 'g', 17)));
            if ((q < r) != (hq < hr) || (q == r) != (hq == hr))
                QFAIL(qPrintable(QStringLiteral("compare %1 %2").arg(a, 0, 'g', 17).arg(b, 0, 'g', 17)));
            // Integers as the core converts them (times in microseconds).
            const qint64 n = qint64(rnd.generate64() >> rnd.bounded(1, 64)) * (rnd.bounded(2) ? 1 : -1);
            check(Ext80(n), (long double)n, "int64", double(n), 0);
            check(Ext80(1) / Ext80(1000) * Ext80(n), 0.001L * n, "ms", double(n), 0);
        }
        // Ties of the 64-bit significand: odd 64-bit integers times small factors.
        for (int i = 0; i < 100000; ++i) {
            const quint64 m = rnd.generate64() | (quint64(1) << 63) | 1;
            const quint64 k = rnd.bounded(2, 1000);
            check(Ext80(m) * Ext80(k), (long double)m * (long double)k, "tie*", double(m), double(k));
            check(Ext80(m) + Ext80(m >> 1), (long double)m + (long double)(m >> 1), "tie+", double(m), 0);
            check(Ext80(m) / Ext80(k), (long double)m / (long double)k, "tie/", double(m), double(k));
        }
        QVERIFY(checked > 3000000);
#endif
    }
};

QTEST_APPLESS_MAIN(TstExt80)
#include "tst_ext80.moc"
