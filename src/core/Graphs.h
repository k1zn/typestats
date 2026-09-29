#pragma once

#include <QVector>

// Graph series of the main window (PaintBox1), port of FUN_00403868 + FUN_0043d4cc.
// One value per text element, aligned with TextModel::pauses; the first element of every fragment
// holds kFragmentStart in every series. See re/graphs.md.

struct GraphSeries
{
    QVector<float> pause;        // DAT_005b1408 "Гистограмма длительностей": TextModel::pauses, ms
    QVector<float> curSpeed;     // DAT_005b13ec "Мгновенная скорость", chars/min
    QVector<float> medSpeed;     // DAT_005b13f0 "Средняя скорость": from the smoothed pause
    QVector<float> classicSpeed; // DAT_005b13fc "Классическая скорость"
    QVector<float> privSpeed;    // DAT_005b1400 "Приведённая скорость"
    QVector<float> curRhythm;    // DAT_005b13f4 "Мгновенная ритмичность", %
    QVector<float> medRhythm;    // DAT_005b13f8 "Средняя ритмичность", %
    QVector<float> arrhythmia;   // DAT_005b140c "Гистограмма аритмии": deviation from the mean pause, %
};

namespace Graphs {
GraphSeries compute(const QVector<float> &pauses);

// FUN_0043d4cc on src[from, to): exponential smoothing with alpha 0.03, started from the mean of
// the first min(to − from, 15) values.
void smooth(const QVector<float> &src, int from, int to, QVector<float> &dst);
}
