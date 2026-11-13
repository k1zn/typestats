#pragma once

#include <QSize>
#include <QtTypes>

#include <algorithm>
#include <cmath>

// Adapt the spatial resolution to measured encoding cost. Keep the requested frame rate and
// timestamps; isolated key frames do not trigger a downgrade. A session only scales down.
class VideoEncodeBudget
{
public:
    QSize observe(QSize size, int fps, qint64 elapsedUs, bool key)
    {
        if (key)
            return size;
        m_totalUs += std::max<qint64>(0, elapsedUs);
        if (++m_frames < 8)
            return size;
        const double averageUs = double(m_totalUs) / m_frames;
        m_totalUs = 0;
        m_frames = 0;
        const double budgetUs = 750000.0 / std::max(1, fps); // room for capture, audio and other work
        if (averageUs <= budgetUs || size.isEmpty())
            return size;
        const double minimum = std::max(160.0 / size.width(), 120.0 / size.height());
        const double factor = std::max(minimum, std::clamp(std::sqrt(budgetUs / averageUs), 0.5, 0.85));
        if (factor >= 1)
            return size;
        return QSize(std::max(160, int(size.width() * factor) & ~1),
                     std::max(120, int(size.height() * factor) & ~1));
    }

private:
    qint64 m_totalUs = 0;
    int m_frames = 0;
};
