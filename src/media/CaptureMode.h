#pragma once

#include <QSize>

#include <cmath>
#include <limits>

namespace CaptureMode {

// Prefer a mode that sustains the requested frame rate over a larger but slow one.
// Once the rate is met, choose the smallest sufficient frame (or the largest smaller one).
inline double score(QSize available, double rate, QSize wanted, int fps)
{
    if (available.width() < 2 || available.height() < 2 || !std::isfinite(rate) || rate <= 0)
        return std::numeric_limits<double>::infinity();
    const double pixels = double(available.width()) * available.height();
    const bool large = available.width() >= wanted.width() && available.height() >= wanted.height();
    const double resolution = large ? pixels : 1e12 - pixels;
    // Fractional 29.97 fps counts as 30. If no mode meets the rate, prefer the fastest.
    const double speed = rate + 0.05 >= fps ? 0 : 1e15 + (fps - rate) * 1e13;
    return speed + resolution + std::abs(rate - fps) * 1000;
}

} // namespace CaptureMode
