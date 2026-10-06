#include "NumberFormat.h"

#include "Ext80.h"

#include <algorithm>
#include <array>
#include <cmath>

QString formatFixed(double v, int decimals, const QLocale &loc)
{
    return formatFixed(v, decimals, decimals > 0 ? loc.decimalPoint() : QString());
}

QString formatFixed(double v, int decimals, QStringView point)
{
    if (!std::isfinite(v)) // as FloatToStrF writes them
        return std::isnan(v) ? QStringLiteral("NAN") : v < 0 ? QStringLiteral("-INF") : QStringLiteral("INF");
    static const std::array<Ext, 19> powers = [] {
        std::array<Ext, 19> p;
        p[0] = Ext(1);
        for (std::size_t i = 1; i < p.size(); ++i)
            p[i] = p[i - 1] * Ext(10); // exact
        return p;
    }();
    decimals = std::clamp(decimals, 0, int(powers.size()) - 1);
    // The rounding of the original: |v|·10^decimals + 0.5 in 80-bit precision, then the integer part.
    const Ext scaled = extFloor(extFabs(Ext(v)) * powers[decimals] + Ext(0.5));
    const bool minus = v < 0 && scaled != Ext(0);

    if (scaled >= Ext(18446744073709551616.0)) { // 2^64: an integer double, or close to it
        QString s = QString::number(std::fabs(v), 'f', decimals);
        if (decimals > 0)
            s.replace(QLatin1Char('.'), point.toString());
        return minus ? u'-' + s : s;
    }
    std::uint64_t scale = 1;
    for (int i = 0; i < decimals; ++i)
        scale *= 10;
    const auto n = std::uint64_t(scaled);
    std::uint64_t whole = n / scale, frac = n % scale;
    if (point.size() > 4) {
        QString s = QString::number(whole);
        if (decimals > 0) {
            s += point;
            s += QString::number(frac).rightJustified(decimals, u'0');
        }
        return minus ? u'-' + s : s;
    }

    // Written from the end: the fraction (zero-padded), the separator, the whole part, the sign.
    char16_t buf[48]; // 18 decimals + 4 + 20 digits + sign
    char16_t *const end = buf + std::size(buf);
    char16_t *p = end;
    if (decimals > 0) {
        for (int i = 0; i < decimals; ++i, frac /= 10)
            *--p = char16_t(u'0' + frac % 10);
        for (qsizetype i = point.size() - 1; i >= 0; --i)
            *--p = point[i].unicode();
    }
    do {
        *--p = char16_t(u'0' + whole % 10);
        whole /= 10;
    } while (whole != 0);
    if (minus)
        *--p = u'-';
    return QString(reinterpret_cast<const QChar *>(p), end - p);
}
