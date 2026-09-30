#include "Editing.h"

#include "KeyName.h"

#include <algorithm>
#include <bitset>

namespace {

const QChar kErasedSpace(0x2588);

// Rewrites the records in place: keeps the ones the caller appends and hands the time of the
// dropped ones to the next record kept.
class Compactor
{
public:
    Compactor(KeyRecords &recs, int writeAt) : m_recs(recs), m_write(writeAt) {}

    void skip(int i) { m_time += m_recs[i].dtUs; }
    void keep(int i)
    {
        m_time += m_recs[i].dtUs;
        KeyRecord r = m_recs[i];
        r.dtUs = m_time;
        m_recs[m_write++] = r;
        m_time = 0;
    }
    // Takes back the last record kept.
    void dropLast() { m_time += m_recs[--m_write].dtUs; }
    void finish() { m_recs.resize(m_write); }

private:
    KeyRecords &m_recs;
    int m_write;
    quint32 m_time = 0;
};

QString plainName(const QString &name)
{
    return name == kErasedSpace ? QStringLiteral(" ") : name;
}

} // namespace

namespace Editing {

std::pair<int, int> recordRange(const TextModel &m, int selStart, int selLength)
{
    if (selLength == 0)
        return {0, 0};
    const auto byPos = [](const TextAnchor &a, int pos) { return a.pos < pos; };
    const auto first = std::lower_bound(m.anchors.begin(), m.anchors.end(), selStart, byPos);
    const int from = first == m.anchors.begin() ? 0 : (first - 1)->rec + 1;
    int to = int(m.records.size());
    if (selStart + selLength < m.text.size()) {
        const auto last = std::lower_bound(m.anchors.begin(), m.anchors.end(), selStart + selLength, byPos);
        to = last != m.anchors.end() ? last->rec : (m.anchors.isEmpty() ? 0 : m.anchors.last().rec + 1);
    }
    return {from, to};
}

void deleteRange(KeyRecords &recs, int from, int to)
{
    from = std::clamp<int>(from, 0, recs.size());
    to = std::clamp<int>(to, from, recs.size());
    std::bitset<256> dropped; // presses deleted, by VK
    Compactor out(recs, from);
    for (int i = from; i < to; ++i) {
        const quint8 vk = recs[i].vk();
        if (recs[i].isDown()) {
            dropped.set(vk);
            out.skip(i);
        } else if (!dropped.test(vk)) {
            out.keep(i); // the release of a press made before the range
        } else {
            dropped.reset(vk);
            out.skip(i);
        }
    }
    for (int i = to; i < recs.size(); ++i) {
        const quint8 vk = recs[i].vk();
        if (recs[i].isDown() || !dropped.test(vk)) {
            out.keep(i);
        } else {
            dropped.reset(vk);
            out.skip(i);
        }
    }
    out.finish();
}

void removeNonText(KeyRecords &recs, int from, int to)
{
    from = std::clamp<int>(from, 0, recs.size());
    to = std::clamp<int>(to, from, recs.size());
    std::bitset<256> dropped, kept;
    quint8 lastShift = 0; // a Shift that was the last press kept
    Compactor out(recs, from);

    auto release = [&](int i) {
        const quint8 vk = recs[i].vk();
        if (dropped.test(vk)) {
            dropped.reset(vk);
            out.skip(i);
            return;
        }
        kept.reset(vk);
        if (lastShift == vk && kept.none()) {
            // Nothing was typed under this Shift: it goes together with its release.
            out.skip(i);
            out.dropLast();
        } else {
            out.keep(i);
        }
        lastShift = 0;
    };

    for (int i = from; i < to; ++i) {
        if (!recs[i].isDown()) {
            release(i);
            continue;
        }
        const quint8 vk = recs[i].vk();
        const QString name = keyDisplayName(recs[i].flags, recs[i].ch);
        const bool shift = name == QLatin1String("[LShift]") || name == QLatin1String("[RShift]");
        if (name.size() == 1 || shift || name == QLatin1String("[BackSpace]")) {
            out.keep(i);
            lastShift = shift ? vk : 0;
            kept.set(vk);
        } else {
            dropped.set(vk);
            out.skip(i);
        }
    }
    for (int i = to; i < recs.size(); ++i) {
        if (recs[i].isDown())
            out.keep(i);
        else
            release(i);
    }
    out.finish();
}

int labelStart(const KeyRecords &recs, int i)
{
    int start = -1;
    if (i < 0 || i >= recs.size())
        return start;
    do {
        if (recs[i].flags & KeyRecord::Marked)
            start = i;
        else if (recs[i].isDown())
            break;
    } while (--i >= 1); // the very first record is looked at only when the walk starts there
    return start;
}

int markRange(KeyRecords &recs, int from, int to)
{
    from = std::clamp<int>(from, 0, recs.size());
    to = std::clamp<int>(to, from, recs.size());
    QString label;
    for (int i = from; i < to; ++i) {
        if (!recs[i].isDown())
            continue;
        recs[i].flags |= KeyRecord::Marked;
        if (!recs[i].comment.isEmpty()) {
            label = recs[i].comment;
            recs[i].comment.clear();
        }
    }
    // A label right after the run now belongs to it.
    for (int i = to; i < recs.size(); ++i) {
        if (!recs[i].isDown())
            continue;
        if (!recs[i].comment.isEmpty()) {
            label = recs[i].comment;
            recs[i].comment.clear();
        }
        break;
    }
    const int start = labelStart(recs, to - 1);
    if (start <= 0)
        return -1;
    recs[start].comment = label;
    return start;
}

void removeLabel(KeyRecords &recs, int i)
{
    if (i < 0 || i >= recs.size())
        return;
    const int start = labelStart(recs, i);
    if (start < 0) {
        recs[i].comment.clear();
        return;
    }
    for (int k = start; k < recs.size(); ++k) {
        if (!recs[k].isDown())
            continue;
        recs[k].comment.clear();
        if (!(recs[k].flags & KeyRecord::Marked))
            break;
        recs[k].flags &= ~quint32(KeyRecord::Marked);
    }
}

QString copyText(const TextModel &m, int b, int e, bool skipErased)
{
    QString out;
    for (int i = std::max(b, 0); i < e && i < m.size(); ++i)
        if (!skipErased || !m.erased(i))
            out += plainName(m.names[i]);
    return out;
}

QString copyTagged(const TextModel &m, int b, int e, const TagOptions &opt)
{
    QString out;
    bool inErased = false, afterErased = false;
    auto open = [&] {
        if (opt.color)
            out += QStringLiteral("[color=\"#ff4444\"]");
        if (opt.strike)
            out += QStringLiteral("[s]");
    };
    auto close = [&] {
        if (opt.strike)
            out += QStringLiteral("[/s]");
        if (opt.color)
            out += QStringLiteral("[/color]");
    };
    for (int i = std::max(b, 0); i < e && i < m.size(); ++i) {
        if (!m.erased(i)) {
            if (inErased) {
                inErased = false;
                close();
                afterErased = true;
            }
        } else if (!inErased) {
            inErased = true;
            open();
        }
        // A struck-through space needs no block character to be seen.
        const QString name = opt.strike ? plainName(m.names[i]) : m.names[i];
        if (afterErased && opt.colorNext) {
            out += QStringLiteral("[color=\"#aa4444\"]") + name + QStringLiteral("[/color]");
            afterErased = false;
        } else {
            out += name;
        }
    }
    if (inErased)
        close();
    return out;
}

}
