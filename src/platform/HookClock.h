#pragma once

#include <chrono>

#include <QtGlobal>

// The time of hook events: std::chrono::steady_clock, in microseconds (evdev events are stamped in
// the same scale, CLOCK_MONOTONIC).
inline qint64 hookNowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
