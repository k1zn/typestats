#include "NumberFormat.h"

#include <cmath>

QString formatFixed(double v, int decimals, const QLocale &loc)
{
    const long double scale = std::pow(10.0L, decimals);
    const long double scaled = std::floor(std::fabs((long double)v) * scale + 0.5L);
    const long double whole = std::floor(scaled / scale);
    QString s = QString::number(qulonglong(whole));
    if (decimals > 0)
        s += loc.decimalPoint() + QString::number(qulonglong(scaled - whole * scale)).rightJustified(decimals, u'0');
    return v < 0 && scaled != 0 ? u'-' + s : s;
}
