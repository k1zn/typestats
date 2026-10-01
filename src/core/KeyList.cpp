#include "KeyList.h"

#include "KeyName.h"
#include "NumberFormat.h"

#include <algorithm>

namespace KeyList {

float scrollForPosition(const TextModel &m, int selStart)
{
    if (m.klav.isEmpty())
        return 0.0f;
    const int k = m.klavAt(selStart);
    // The original checks size < k only, and reads one record past the end for k == size.
    const qint64 t = k < m.klav.size() ? m.klav[k].tDraw : m.klav.last().tDraw;
    return float(0.001L * (t - 100));
}

QVector<KeyListRow> rows(const QVector<KlavRecord> &klav, const QLocale &loc, double fromUs, double toUs, int limit,
                         int digits)
{
    QVector<KeyListRow> out;
    int pressRow[256];
    qint64 pressT[256] = {};
    std::fill(std::begin(pressRow), std::end(pressRow), -1);
    qint64 prev = 0;
    // Drawing times do not decrease: the window starts at the first record not before fromUs.
    const auto first = std::lower_bound(klav.begin(), klav.end(), fromUs,
                                        [](const KlavRecord &r, double v) { return double(r.tDraw) < v; });
    for (auto it = first; it != klav.end(); ++it) {
        const KlavRecord &r = *it;
        const double t = double(r.tDraw);
        if (t > toUs)
            break;
        const quint8 scan = r.flags & KeyRecord::ScanMask; // indexed by scan code, not VK
        if (!r.down) {
            if (pressRow[scan] >= 0) {
                out[pressRow[scan]].duration = formatFixed(double(0.001L * (r.t - pressT[scan])), digits, loc);
                pressRow[scan] = -1;
            }
            continue;
        }
        pressRow[scan] = out.size();
        pressT[scan] = r.t;
        KeyListRow row;
        const float pause = float(0.001L * (r.t - prev));
        if (pause <= 59000.0f)
            row.pause = formatFixed(double(pause), digits, loc);
        row.key = keyDisplayName(r.flags, r.ch);
        if (row.key == QLatin1String("\r"))
            row.key = QStringLiteral("[Enter]");
        out.append(row);
        if (limit >= 0 && out.size() >= limit)
            break;
        prev = r.t;
    }
    if (!out.isEmpty())
        out[0].pause.clear(); // FUN_00404c44
    return out;
}

} // namespace KeyList
