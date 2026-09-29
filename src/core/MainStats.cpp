#include "MainStats.h"

#include <cmath>

namespace {

float pauseAt(const TextModel &m, int i)
{
    return i >= 0 && i < m.pauses.size() ? m.pauses[i] : 0.0f;
}

double meanAbsDeviationPct(const QVector<float> &v, double avg, double minAvg)
{
    double sum = 0;
    for (float x : v)
        sum += std::fabs(double(x) - avg);
    if (!v.isEmpty() && avg > minAvg)
        sum /= double(v.size()) * avg * 0.01;
    return sum;
}

} // namespace

namespace Stats {

std::pair<int, int> range(const TextModel &m, int selStart, int selLen, bool byPauses)
{
    auto elemAt = [&](int pos) { return Recalc::at(m.mapElem, Recalc::lowerBound(m.mapPos, pos)); };
    const int n = m.pauses.size();
    if (selLen != 0)
        return {elemAt(selStart), elemAt(selStart + selLen)};
    if (!byPauses)
        return {0, n};
    const int cur = elemAt(selStart);
    int b = cur;
    while (b != 0 && !(pauseAt(m, b) < 0.0f))
        --b;
    int e = cur;
    if (e < n)
        ++e;
    while (e < n && !(pauseAt(m, e) < 0.0f))
        ++e;
    return {b, e};
}

void speedAndHold(const QVector<KlavRecord> &klav, int rb, int re, quint32 splitUs,
                  int *presses, int *holdAvgUs, quint64 *timeUs)
{
    qint64 pressT[256];
    std::fill(std::begin(pressT), std::end(pressT), -1);
    if (splitUs == 0)
        splitUs = 50000000;
    int n = 0;
    quint64 T = 0;
    qint64 hold = 0;
    int holdN = 0;
    qint64 prev = qint64(0xFFFFFFFF80000001ull);
    int pending = 0;
    bool shiftF = false, deadF = false;
    for (int i = rb; i < klav.size(); ++i) {
        const KlavRecord &r = klav[i];
        const quint8 vk = (r.flags >> 16) & 0xFF;
        if (!r.down) {
            if (pressT[vk] >= 0) {
                hold += r.t - pressT[vk];
                ++holdN;
                if (pending != 0) {
                    --pending;
                    if (re <= i && pending == 0)
                        break;
                }
            }
            if (vk == 0xA0 || vk == 0xA1)
                shiftF = false;
            continue;
        }
        pressT[vk] = r.t;
        if (i >= re)
            continue;
        const bool erasedPress = r.flags & KeyRecord::Erased;
        const qint64 dt = r.t - prev;
        const bool counted = !(r.flags & KeyRecord::SegmentStart) && dt < qint64(splitUs);
        if (counted)
            T += quint64(dt);
        if (vk == 0x08 || erasedPress) {
            if (erasedPress && n != 0 && (shiftF || deadF)) {
                --n;
                shiftF = deadF = false;
            }
        } else {
            if (counted)
                ++n;
            shiftF = vk == 0xA0 || vk == 0xA1;
            deadF = r.flags & KeyRecord::DeadKey;
        }
        ++pending;
        prev = r.t;
    }
    *presses = n;
    *holdAvgUs = holdN ? int(hold / holdN) : 0;
    *timeUs = T;
}

MainStats compute(const TextModel &m, int b, int e, int splitMs, bool byPauses)
{
    MainStats s;
    bool prevErased = false;
    double sumClean = 0;
    double minP = 1e20, maxP = 0;
    int nAll = 0, nOk = 0, nClean = 0;
    double accAll = 0, accClean = 0;
    QVector<float> intervalsAll, intervalsClean;
    int run = 0;

    for (int i = b; i < e; ++i) {
        const bool single = m.isChar(i);
        const bool er = m.erased(i);
        if (s.chars != 0) {
            const float p = pauseAt(m, i);
            if (p > -0.1f) {
                if (p < minP)
                    minP = p;
                if (maxP < p)
                    maxP = p;
                s.sumAll += p;
                accAll += p;
                if (single) {
                    ++nAll;
                    intervalsAll.append(float(accAll));
                    accAll = 0;
                }
                if (!er) {
                    if (single)
                        ++nOk;
                    if (!m.erased(i - 1) && !prevErased) {
                        sumClean += p;
                        accClean += p;
                        if (single) {
                            ++nClean;
                            intervalsClean.append(float(accClean));
                            accClean = 0;
                        }
                    }
                }
            }
        }
        if (er) {
            ++s.erased;
            if (!prevErased)
                ++s.erasedSeries;
        }
        if (single) {
            prevErased = er;
            ++s.chars;
            if (!er) {
                ++run;
            } else {
                s.maxRun = std::max(s.maxRun, run);
                run = 0;
            }
        }
    }
    s.maxRun = std::max(s.maxRun, run);

    s.avgAll = nAll ? s.sumAll / nAll : 0.0;
    const double avgClean = nClean ? sumClean / nClean : 1.0;
    s.arrAll = meanAbsDeviationPct(intervalsAll, s.avgAll, 0.001);
    s.arrClean = intervalsClean.isEmpty() ? 0.0 : meanAbsDeviationPct(intervalsClean, avgClean, 0.01);

    const bool haveTime = s.sumAll > 0.01;
    auto speed = [](int n, double sum, double *withOne, double *plain) {
        *withOne = double(n + 1) * 60000.0 / sum;
        *plain = double(n) * 60000.0 / sum;
    };
    if (nOk && haveTime)
        speed(nOk, s.sumAll, &s.net, &s.net0);
    if (nAll && haveTime)
        speed(nAll, s.sumAll, &s.gross, &s.gross0);
    if (nAll + s.erased && haveTime)
        speed(nAll + s.erased, s.sumAll, &s.grossPlus, &s.grossPlus0);
    if (nClean && sumClean > 0.01)
        speed(nClean, sumClean, &s.grossStar, &s.grossStar0);

    if (s.chars) {
        s.corrPct = float(s.erased * 100.0 / s.chars);
        s.seriesPct = float(s.erasedSeries * 100.0 / s.chars);
        s.maxRunPct = float(s.maxRun * 100.0 / s.chars);
    }
    s.minPause = minP > 1e19 ? 0.0 : minP;
    s.maxPause = maxP;

    // Klavogram records of the range.
    const int rb = Recalc::at(m.mapKlav, Recalc::lowerBound(m.mapElem, b));
    const int re = Recalc::at(m.mapKlav, Recalc::lowerBound(m.mapElem, e));
    int presses = 0;
    quint64 T = 0;
    speedAndHold(m.klav, rb, re, byPauses ? 0u : quint32(splitMs) * 1000u, &presses, &s.holdAvgUs, &T);
    if (T != 0) {
        s.spm = float(double(presses + 1) * 6e7 / double(T));
        s.spm0 = float(double(presses) * 6e7 / double(T));
    }

    s.loss = s.grossStar0 > 1.0 ? float((s.grossStar0 - s.net0) * 100.0 / s.grossStar) : 0.0f;
    return s;
}

QString formatTime(int ms, int digits, const QLocale &loc, const StatsUnits &u)
{
    const int minutes = ms / 60000;
    QString s = loc.toString((ms % 60000) * 0.001, 'f', digits) + u.s;
    if (minutes != 0) {
        s = QString::number(minutes % 60) + u.m + QLatin1Char(' ') + s;
        if (minutes / 60 != 0)
            s = QString::number(minutes / 60) + u.h + QLatin1Char(' ') + s;
    }
    return s;
}

QStringList format(const MainStats &s, const QLocale &locale, const StatsUnits &u)
{
    QLocale loc = locale;
    loc.setNumberOptions(QLocale::OmitGroupSeparator);
    auto f = [&](double v, int d) { return loc.toString(v, 'f', d); };
    auto pair = [&](double a, double b) { return f(a, 2) + QLatin1String(" (") + f(b, 2) + QLatin1Char(')'); };
    auto cnt = [&](int n, float pct) {
        return QString::number(n) + QLatin1String(" (") + f(pct, 2) + QLatin1String("%)");
    };
    const QString ms = QLatin1Char(' ') + u.ms;
    QStringList r;
    r << QString::number(s.chars) + QLatin1String(" (") + QString::number(s.chars - s.erased) + QLatin1Char(')')
      << formatTime(int(s.sumAll), 3, loc, u)
      << f(s.minPause, 3) + ms
      << f(s.maxPause, 3) + ms
      << f(s.avgAll, 3) + ms
      << f(s.holdAvgUs * 0.001, 3) + ms
      << pair(s.spm, s.spm0)
      << pair(0.2 * s.net, 0.2 * s.net0)
      << pair(s.net, s.net0)
      << pair(s.gross, s.gross0)
      << pair(s.grossPlus, s.grossPlus0)
      << pair(s.grossStar, s.grossStar0)
      << f(s.loss, 2) + QLatin1Char('%')
      << f(s.arrAll, 2) + QLatin1String("% (") + f(s.arrClean, 2) + QLatin1String(")%")
      << cnt(s.erased, s.corrPct)
      << cnt(s.erasedSeries, s.seriesPct)
      << cnt(s.maxRun, s.maxRunPct);
    return r;
}

QStringList rowNames()
{
    return {QStringLiteral("Символов"), QStringLiteral("Общее время"), QStringLiteral("min пауза"),
            QStringLiteral("max пауза"), QStringLiteral("Средняя пауза"),
            QStringLiteral("Среднее время удержания"), QStringLiteral("Скорость spm"),
            QStringLiteral("Скорость wpm"), QStringLiteral("Скорость нетто"),
            QStringLiteral("Скорость брутто"), QStringLiteral("Скорость брутто+"),
            QStringLiteral("Скорость брутто*"), QStringLiteral("Потери от исправлений"),
            QStringLiteral("Аритмия"), QStringLiteral("Исправлений"),
            QStringLiteral("Серий исправлений"), QStringLiteral("max без исправлений")};
}

} // namespace Stats
