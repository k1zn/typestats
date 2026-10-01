#include "NumberFormat.h"

#include <array>
#include <cmath>

QString formatFixed(double v, int decimals, const QLocale &loc)
{
    static const std::array<long double, 19> powers = [] {
        std::array<long double, 19> p;
        for (int i = 0; i < int(p.size()); ++i)
            p[i] = std::pow(10.0L, i);
        return p;
    }();
    const bool small = decimals >= 0 && decimals < int(powers.size());
    const long double scale = small ? powers[decimals] : std::pow(10.0L, decimals);
    const long double scaled = std::floor(std::fabs((long double)v) * scale + 0.5L);
    const long double whole = std::floor(scaled / scale);
    const bool minus = v < 0 && scaled != 0;
    const QString point = decimals > 0 ? loc.decimalPoint() : QString();
    // The fraction has no more digits than decimals unless long double rounding says otherwise.
    if (!small || point.size() > 4 || qulonglong(scaled - whole * scale) >= qulonglong(scale)) {
        QString s = QString::number(qulonglong(whole));
        if (decimals > 0)
            s += point + QString::number(qulonglong(scaled - whole * scale)).rightJustified(decimals, u'0');
        return minus ? u'-' + s : s;
    }

    // Written from the end: the fraction (zero-padded), the separator, the whole part, the sign.
    char16_t buf[48]; // 18 decimals + 4 + 20 digits + sign
    char16_t *const end = buf + std::size(buf);
    char16_t *p = end;
    if (decimals > 0) {
        qulonglong frac = qulonglong(scaled - whole * scale);
        for (int i = 0; i < decimals; ++i, frac /= 10)
            *--p = char16_t(u'0' + frac % 10);
        for (qsizetype i = point.size() - 1; i >= 0; --i)
            *--p = point[i].unicode();
    }
    qulonglong w = qulonglong(whole);
    do {
        *--p = char16_t(u'0' + w % 10);
        w /= 10;
    } while (w != 0);
    if (minus)
        *--p = u'-';
    return QString(reinterpret_cast<const QChar *>(p), end - p);
}
