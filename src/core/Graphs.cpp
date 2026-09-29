#include "Graphs.h"

#include "Recalc.h"

#include <cmath>

// long double is the 80-bit x87 format with MinGW; the original computes in it and rounds to float
// on every store, so the same is done here to get identical values.
using ext = long double;

namespace {

void computeFragment(GraphSeries &g, int from, int to)
{
    const QVector<float> &p = g.pause;
    float sum = 0.0f;
    for (int i = from; i < to; ++i) {
        g.curSpeed[i] = p[i] > 10.0f ? float(ext(60000.0f) / p[i]) : 0.0f;
        sum = float(ext(p[i]) + sum);
    }
    Graphs::smooth(p, from, to, g.medSpeed);
    const float avg = float(ext(sum) / ext(to - from));

    float cum = 0.0f;
    for (int i = from; i < to; ++i) {
        cum = float(ext(p[i]) + cum);
        float r = avg > 0.01 ? float(std::fabs(double(ext(p[i]) - avg)) / ext(avg)) : 0.0f;
        if (r > 1.0f)
            r = 1.0f;
        g.curRhythm[i] = float((ext(1.0f) - r) * 100.0f);
        const float m = g.medSpeed[i];
        g.medSpeed[i] = m > 10.0f ? float(ext(60000.0f) / m) : 0.0f;
        g.arrhythmia[i] = avg > 0.01 ? float(ext(100.0f) * p[i] / avg - 100.0f) : 0.0f;
        const int k = i - from;
        g.classicSpeed[i] = cum > 10.0f ? float(ext(k * 60000 + 120000) / cum) : 0.0f;
        g.privSpeed[i] = cum > 10.0f ? float(ext(k * 60000 + 60000) / cum) : 0.0f;
    }
    Graphs::smooth(g.curRhythm, from, to, g.medRhythm);
}

} // namespace

namespace Graphs {

void smooth(const QVector<float> &src, int from, int to, QVector<float> &dst)
{
    const int n = to - from;
    if (n <= 0)
        return;
    const float alpha = 0.03f;
    const int head = std::min(n, 15);
    float prev = 0.0f;
    for (int i = 0; i < head; ++i)
        prev = float(ext(src[from + i]) + prev);
    prev = float(ext(prev) / head);
    for (int i = from; i < to; ++i) {
        prev = float((ext(1.0f) - alpha) * prev + ext(alpha) * src[i]);
        dst[i] = prev;
    }
}

GraphSeries compute(const QVector<float> &pauses)
{
    GraphSeries g;
    g.pause = pauses;
    const int n = pauses.size();
    for (QVector<float> *s : {&g.curSpeed, &g.medSpeed, &g.classicSpeed, &g.privSpeed, &g.curRhythm,
                              &g.medRhythm, &g.arrhythmia})
        s->fill(0.0f, n);
    // Recalculate calls FUN_00403868 for every fragment when the next one starts (and at the end);
    // the marker element itself only gets kFragmentStart in every series.
    int from = 0;
    for (int i = 0; i <= n; ++i) {
        if (i == n || pauses[i] == kFragmentStart) {
            computeFragment(g, from, i);
            if (i < n)
                for (QVector<float> *s : {&g.curSpeed, &g.medSpeed, &g.classicSpeed, &g.privSpeed, &g.curRhythm,
                                          &g.medRhythm, &g.arrhythmia})
                    (*s)[i] = kFragmentStart;
            from = i + 1;
        }
    }
    return g;
}

} // namespace Graphs
