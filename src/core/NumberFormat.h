#pragma once

#include <QLocale>
#include <QString>

// A number with a fixed count of decimals, halves rounded away from zero (QLocale rounds them to
// even), the locale's decimal separator and no group separators - as the original shows numbers.
QString formatFixed(double v, int decimals, const QLocale &loc);
// The same with the separator given. For the system locale QLocale asks the OS on every decimalPoint()
// (Windows GetLocaleInfoEx ~0.16 us, macOS CFLocale ~1.5-3 us): who formats many numbers takes it once.
QString formatFixed(double v, int decimals, QStringView point);
