#include "Video.h"

#include <QDir>

#include <algorithm>
#include <cmath>

namespace Video {

qint64 positionMs(const TextModel &m, float scrollMs, int shiftMs)
{
    const qint64 drawUs = qint64(std::trunc(scrollMs)) * 1000;
    qint64 us = drawUs;
    if (!m.klav.isEmpty()) {
        // The drawing time of the left edge as real time, as the klavogram's cursor does it.
        auto it = std::lower_bound(m.klav.begin(), m.klav.end(), drawUs,
                                   [](const KlavRecord &r, qint64 t) { return r.tDraw < t; });
        if (it == m.klav.end())
            --it;
        us = drawUs - it->tDraw + it->t - m.klav.first().t;
    }
    return std::max<qint64>(0, us / 1000 + shiftMs);
}

qint64 frameStartMs(qint64 ms, double fps, qint64 durationMs)
{
    if (durationMs > 0)
        ms = std::min(ms, durationMs - 1);
    if (fps <= 0)
        return ms;
    const qint64 frame = qint64(std::floor(double(ms) * fps / 1000.0 + 1e-9));
    return qint64(std::floor(double(frame) * 1000.0 / fps + 1e-9));
}

QString resolvePath(const QString &recordingDir, const QString &name)
{
    return QDir::cleanPath(QDir(recordingDir).filePath(name));
}

}
