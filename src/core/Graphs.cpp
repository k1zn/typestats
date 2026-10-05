#include "Graphs.h"

#include "Ext80.h"

#include <bit>
#include <cmath>
#include <cstdint>

// The original computes in the 80-bit x87 format and rounds to float on every store, so the same is
// done here to get identical values. Where that is proven to give the same float, the expression is
// computed in double instead (re/perf.md, "Graphs in double"): Ext is software on arm64 and MSVC, ~20
// times slower. Every such place says why; where its condition does not hold, Ext computes it as before.
using ext = Ext;

namespace {

// a + b and its rounding error: a + b == s + err exactly (Knuth's TwoSum; round to nearest, no overflow).
struct Sum
{
    double s, err;
};
inline Sum twoSum(double a, double b)
{
    const double s = a + b;
    const double bb = s - a;
    return {s, (a - (s - bb)) + (b - bb)};
}

// The exponent field of a float / a double: 0 for zero and subnormals.
inline int expField(float v)
{
    return int((std::bit_cast<std::uint32_t>(v) >> 23) & 0xFF);
}
inline int expField(double v)
{
    return int((std::bit_cast<std::uint64_t>(v) >> 52) & 0x7FF);
}

// s + err (exact) rounded to float once: rounded to odd in double first (53 >= 24 + 2 bits, Boldo and
// Melquiond), then to nearest.
inline float roundOnce(Sum v)
{
    auto bits = std::bit_cast<std::uint64_t>(v.s);
    if (v.err != 0.0 && !(bits & 1))
        bits += (v.err > 0) == (v.s > 0) ? 1 : std::uint64_t(-1);
    return float(std::bit_cast<double>(bits));
}

// float((ext(1) - alpha) * prev + ext(alpha) * x), alpha = 0.03f. keep = 1 - alpha.
inline float smoothStep(float prev, float x, const ext &keep, float alpha, double keepD)
{
    // Both products are exact in double: 1 - 0.03f has 29 significant bits, a float 24 (29 + 24 <= 53).
    // Ext rounds their sum once to 64 bits and then to float; that is the sum rounded to float once
    // unless a midpoint of floats is within 2^-64 of it and the sum is not that midpoint - impossible
    // when every nonzero term is a multiple of 2^(E-63), E the exponent of the sum, which is what the
    // exponents below say (a term is a multiple of 2^(e-52), e its float's exponent).
    const Sum s = twoSum(keepD * prev, double(alpha) * x);
    const int e = expField(s.s) - 1023 + 127 - 11; // the sum's exponent as a float's, less 11 (>= E - 11)
    const bool prevOk = prev == 0.0f || expField(prev) >= e;
    const bool xOk = x == 0.0f || expField(x) >= e;
    if (prevOk && xOk && expField(prev) != 0xFF && expField(x) != 0xFF) [[likely]]
        return roundOnce(s);
    return float(keep * prev + ext(alpha) * x);
}

void computeFragment(GraphSeries &g, int from, int to)
{
    const QVector<float> &p = g.pause;
    float sum = 0.0f;
    for (int i = from; i < to; ++i) {
        // One operation on two floats: rounding to double and then to float is rounding once (p >= 2q + 2,
        // Figueroa), as is the x87 rounding to 64 bits and then to float.
        g.curSpeed[i] = p[i] > 10.0f ? float(60000.0 / double(p[i])) : 0.0f;
        sum = float(double(p[i]) + double(sum));
    }
    Graphs::smooth(p, from, to, g.medSpeed);
    const float avg = float(ext(sum) / ext(to - from));
    const double avgD = avg;

    float cum = 0.0f;
    for (int i = from; i < to; ++i) {
        cum = float(double(p[i]) + double(cum)); // as sum above
        float r = 0.0f;
        if (avg > 0.01) {
            // |p - avg| / avg. The difference of two floats is exact in double when the error of the double
            // subtraction is 0; then Ext has it exactly too (and the double of Ext is the same). A number
            // of at most 53 bits divided by a float rounds to float through double as through 64 bits
            // (re/perf.md, lemma 1).
            const Sum t = twoSum(double(p[i]), -avgD);
            r = t.err == 0.0 ? float(std::fabs(t.s) / avgD) : float(std::fabs(double(ext(p[i]) - avg)) / ext(avg));
        }
        if (r > 1.0f)
            r = 1.0f;
        // (1 - r) * 100: for r = 0 or r >= 2^-25 both operations are exact in double (and in Ext): 1 - r is
        // a multiple of 2^-48 below 1, times 25 still below 2^53. Only the store rounds.
        g.curRhythm[i] = r == 0.0f || r >= 0x1p-25f ? float((1.0 - double(r)) * 100.0) : float((ext(1.0f) - r) * 100.0f);
        const float m = g.medSpeed[i];
        g.medSpeed[i] = m > 10.0f ? float(60000.0 / double(m)) : 0.0f; // as curSpeed
        g.arrhythmia[i] = avg > 0.01 ? float(ext(100.0f) * p[i] / avg - 100.0f) : 0.0f;
        // An integer (|n| < 2^31, wrapped as the original's 32-bit int) divided by a float: lemma 1.
        const auto k = quint32(i - from);
        g.classicSpeed[i] = cum > 10.0f ? float(double(qint32(k * 60000u + 120000u)) / double(cum)) : 0.0f;
        g.privSpeed[i] = cum > 10.0f ? float(double(qint32(k * 60000u + 60000u)) / double(cum)) : 0.0f;
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
    const ext keep = ext(1.0f) - alpha; // exact
    const double keepD = 1.0 - double(alpha); // the same, exact
    for (int i = from; i < to; ++i) {
        prev = smoothStep(prev, src[i], keep, alpha, keepD);
        dst[i] = prev;
    }
}

GraphSeries compute(const TextModel &m)
{
    GraphSeries g;
    g.pause = m.pauses;
    const int n = m.size();
    for (QVector<float> *s : {&g.curSpeed, &g.medSpeed, &g.classicSpeed, &g.privSpeed, &g.curRhythm,
                              &g.medRhythm, &g.arrhythmia})
        s->fill(0.0f, n);
    // Every fragment is computed on its own, without its first element.
    int from = 0;
    for (int start : m.fragmentStarts) {
        computeFragment(g, from, start);
        from = start + 1;
    }
    computeFragment(g, from, n);
    return g;
}

} // namespace Graphs
