#pragma once

#include "Recalc.h"

#include <QLocale>
#include <QStringList>

// Main statistics panel (ListView2), port of 0x4255a8; see re/metrics.md.

struct MainStats
{
    enum Row {
        Chars, TotalTime, MinPause, MaxPause, AvgPause, AvgHold, SpeedSpm, SpeedWpm, SpeedNet,
        SpeedGross, SpeedGrossPlus, SpeedGrossStar, CorrectionLoss, Arrhythmia, Corrections,
        CorrectionSeries, MaxWithoutCorrections, RowCount
    };

    int chars = 0;           // characters (names of length 1) in the range
    int erased = 0;          // erased elements
    int erasedSeries = 0;    // runs of erased characters
    int maxRun = 0;          // longest run of characters without an erased one
    double sumAll = 0;       // ms
    double minPause = 0;     // ms
    double maxPause = 0;     // ms
    double avgAll = 0;       // ms per character
    double arrAll = 0;       // %
    double arrClean = 0;     // %
    double net = 0, net0 = 0, gross = 0, gross0 = 0, grossPlus = 0, grossPlus0 = 0;
    double grossStar = 0, grossStar0 = 0;
    float corrPct = 0, seriesPct = 0, maxRunPct = 0, loss = 0;
    float spm = 0, spm0 = 0;
    int holdAvgUs = 0;
};

struct StatsUnits
{
    QString ms = QStringLiteral("мс");
    QString h = QStringLiteral("ч");
    QString m = QStringLiteral("м");
    QString s = QStringLiteral("с");
};

namespace Stats {
// FUN_004252fc: element range [b, e) for the current selection. selLen == 0 means no selection:
// the whole text, or with byPauses the fragment around selStart.
std::pair<int, int> range(const TextModel &m, int selStart, int selLen, bool byPauses);

MainStats compute(const TextModel &m, int b, int e, int splitMs, bool byPauses);

// FUN_00438304 on klavogram records [rb, re).
void speedAndHold(const QVector<KlavRecord> &klav, int rb, int re, quint32 splitUs,
                  int *presses, int *holdAvgUs, quint64 *timeUs);

// FUN_0040332c
QString formatTime(int ms, int digits, const QLocale &loc, const StatsUnits &u = {});
// The same with the decimal separator given (formatFixed).
QString formatTime(int ms, int digits, QStringView point, const StatsUnits &u = {});
// One string per MainStats::Row, as the original shows them.
QStringList format(const MainStats &s, const QLocale &loc, const StatsUnits &u = {});
// Row captions (Form8.CheckListBox1).
QStringList rowNames();
}
