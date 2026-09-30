#pragma once

#include <QLocale>
#include <QString>

// A number with a fixed count of decimals, halves rounded away from zero (QLocale rounds them to
// even), the locale's decimal separator and no group separators - as the original shows numbers.
QString formatFixed(double v, int decimals, const QLocale &loc);
