#include "Histograms.h"

#include "Ext80.h"
#include "NumberFormat.h"
#include "KeyName.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <memory>

namespace Histograms {

namespace {

const Ext kMs = kExtMilli;
constexpr int kNoFinger = -1, kNoHand = -1;

QString shown(const QString &name)
{
    return name == QLatin1String("\r") ? QStringLiteral("[Enter]") : name;
}

struct Press
{
    int rec = 0;
    quint64 us = 0; // time since the previous press
    bool good = false; // within the split pause and not erased
    quint8 key = 0;
    int finger = 0;
    int hand = 0;   // 0 left, 1 right, -1 other keys
};

// Calls f for every key press among the records of the source.
template <typename F>
void forEachPress(const Source &src, F f)
{
    quint64 acc = 0;
    const KeyRecords &recs = src.model->records;
    for (int i = std::max(src.recBegin, 0); i < src.recEnd && i < recs.size(); ++i) {
        const KeyRecord &r = recs[i];
        acc += r.dtUs;
        if (r.isUp())
            continue;
        Press p;
        p.rec = i;
        p.us = acc;
        p.good = acc < src.splitUs && !src.model->recErased[i];
        p.key = r.scan();
        p.finger = src.zones.finger(r.flags);
        p.hand = p.finger < 4 ? 0 : p.finger == FingerZones::kNone ? kNoHand : 1;
        f(p);
        acc = 0;
    }
}

QString label(const Source &src, quint8 key)
{
    return shown(src.label ? src.label(key) : QString());
}

// 0x44bc8c: average pause per key, longest first (equal ones by key code).
QVector<Bar> ranked(const Source &src, const std::array<float, 256> &sumUs, const std::array<int, 256> &count)
{
    QVector<Bar> bars;
    for (int key = 0; key < 256; ++key) {
        if (count[key] == 0)
            continue;
        Bar b;
        b.value = float(Ext(sumUs[key]) / Ext(count[key] * 1000));
        if (!(b.value > 0.01))
            continue;
        b.label = label(src, quint8(key));
        b.count = count[key];
        b.key = quint8(key);
        bars.append(b);
    }
    std::stable_sort(bars.begin(), bars.end(), [](const Bar &a, const Bar &b) { return a.value > b.value; });
    return bars;
}

Bar single(const Press &p, const QString &label, int prevRec)
{
    Bar b;
    b.value = float(kMs * Ext(p.us));
    b.label = label;
    b.rec = prevRec;
    return b;
}

QVector<Bar> averages(const float *sum, const int *count, const QStringList &labels)
{
    QVector<Bar> bars;
    for (int i = 0; i < labels.size(); ++i) {
        Bar b;
        b.value = count[i] ? sum[i] / float(count[i]) : 0.0f;
        b.label = labels[i];
        b.count = count[i];
        bars.append(b);
    }
    return bars;
}

} // namespace

KeyLabel labelsFromRecords(const KeyRecords &recs)
{
    // Per key: how often every (lower-case) character was produced without Shift (what the key
    // itself gives, as the original's ToUnicodeEx without modifiers) and with it, and a record
    // without a character.
    std::array<std::array<QHash<char16_t, int>, 2>, 256> chars;
    std::array<quint32, 256> plain{};
    std::array<bool, 256> seen{};
    for (const KeyRecord &r : recs) {
        if (r.isUp())
            continue;
        const quint8 key = r.scan();
        if (r.hasChar() && r.ch != 0 && !(r.flags & (KeyRecord::Ctrl | KeyRecord::Alt)))
            ++chars[key][bool(r.flags & KeyRecord::Shift)][QChar(r.ch).toLower().unicode()];
        if (!seen[key]) {
            seen[key] = true;
            plain[key] = r.flags & (KeyRecord::ScanMask | KeyRecord::VkMask | KeyRecord::Extended);
        }
    }
    auto names = std::make_shared<std::array<QString, 256>>();
    for (int key = 0; key < 256; ++key) {
        if (!seen[key])
            continue;
        char16_t best = 0;
        int n = 0;
        const auto &counts = chars[key][chars[key][0].isEmpty()];
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            if (it.value() > n || (it.value() == n && it.key() < best)) {
                best = it.key();
                n = it.value();
            }
        // BackSpace, Tab, Enter and Esc always give a character; they may have been recorded only
        // with Ctrl or Alt (Alt+Tab).
        const quint8 vk = (plain[key] & KeyRecord::VkMask) >> 16;
        const bool control = vk == Vk::Back || vk == Vk::Tab || vk == Vk::Return || vk == Vk::Escape;
        (*names)[key] = n || control ? keyDisplayName(plain[key] | KeyRecord::HasChar, best)
                                     : keyDisplayName(plain[key] | KeyRecord::NoChar, 0);
    }
    return [names](quint8 key) { return (*names)[key]; };
}

std::pair<int, int> recordRange(const TextModel &m, int b, int e)
{
    return {m.recordOfElement(b), m.recordOfElement(e)};
}

Page build(const Source &src, const Node &node)
{
    Page page;
    const Names &n = src.names;
    switch (node.kind) {
    case Node::AllKeys:
    case Node::Key: {
        std::array<float, 256> sum{};
        std::array<int, 256> count{};
        quint8 prev = 0;
        forEachPress(src, [&](const Press &p) {
            // A key page collects the pauses of its key by the key pressed before.
            const bool mine = node.kind == Node::AllKeys || p.key == node.key;
            const quint8 slot = node.kind == Node::AllKeys ? p.key : prev;
            if (mine && p.good) {
                sum[slot] = float(Ext(p.us) + Ext(sum[slot]));
                ++count[slot];
            }
            prev = p.key;
        });
        page.bars = ranked(src, sum, count);
        if (node.kind == Node::AllKeys) {
            page.title = n.allKeys;
        } else {
            QString name = label(src, node.key);
            if (name.size() == 1)
                name = QLatin1Char('[') + name + QLatin1Char(']');
            page.title = n.key + QLatin1Char(' ') + name;
        }
        break;
    }
    case Node::Pair: {
        quint8 prev = 0;
        int prevRec = 0;
        forEachPress(src, [&](const Press &p) {
            if (p.key == node.key && prev == node.prevKey && p.good)
                page.bars.append(single(p, QString::number(page.bars.size() + 1), prevRec));
            prev = p.key;
            prevRec = p.rec;
        });
        page.title = n.pairs + QLatin1String(" [") + label(src, node.prevKey) + label(src, node.key) + QLatin1Char(']');
        break;
    }
    case Node::AllFingers: {
        float sum[9] = {};
        int count[9] = {};
        forEachPress(src, [&](const Press &p) {
            if (p.good) {
                ++count[p.finger];
                sum[p.finger] = float(kMs * Ext(p.us) + Ext(sum[p.finger]));
            }
        });
        page.bars = averages(sum, count, n.fingersShort);
        page.title = n.allFingers;
        break;
    }
    case Node::Finger: {
        float sum[4] = {};
        int count[4] = {};
        quint8 prevKey = 0;
        int prevFinger = kNoFinger, prevHand = kNoHand;
        forEachPress(src, [&](const Press &p) {
            if (!p.good) {
                prevKey = 0;
                prevFinger = kNoFinger;
                prevHand = kNoHand;
                return;
            }
            const int rel = p.key == prevKey ? 0 : p.finger == prevFinger ? 1 : p.hand == prevHand ? 2 : 3;
            prevKey = p.key;
            prevFinger = p.finger;
            prevHand = p.hand;
            if (p.finger == node.finger) {
                ++count[rel];
                sum[rel] = float(kMs * Ext(p.us) + Ext(sum[rel]));
            }
        });
        page.bars = averages(sum, count, n.relations);
        page.title = n.fingers.value(node.finger);
        break;
    }
    case Node::FingerRelation: {
        quint8 prevKey = 0;
        int prevFinger = kNoFinger, prevHand = kNoHand, prevRec = 0;
        forEachPress(src, [&](const Press &p) {
            bool match = false;
            switch (node.relation) {
            case 0: match = p.key == prevKey; break;
            case 1: match = p.finger == prevFinger && p.key != prevKey; break;
            case 2: match = p.finger != prevFinger && p.hand == prevHand; break;
            case 3: match = p.hand != prevHand; break;
            }
            if (match && p.finger == node.finger && p.good) {
                const QString name = node.relation == 0 ? label(src, p.key) : label(src, prevKey) + label(src, p.key);
                page.bars.append(single(p, name, prevRec));
            }
            prevKey = p.key;
            prevFinger = p.finger;
            prevHand = p.hand;
            prevRec = p.rec;
        });
        page.title = n.relationTitles.value(node.relation) + QLatin1String(" - ") + n.fingers.value(node.finger);
        break;
    }
    case Node::Extra:
        page.title = n.extra;
        break;
    }
    return page;
}

Page fromExtra(const QVector<ExtraStats::Row> &rows, const Names &names)
{
    Page page;
    page.title = names.extra;
    for (const ExtraStats::Row &r : rows) {
        Bar b;
        b.value = r.speed;
        b.label = r.text;
        page.bars.append(b);
    }
    return page;
}

std::optional<Node> drill(const Node &node, const Page &page, int index)
{
    if (index < 0 || index >= page.bars.size())
        return {};
    Node next = node;
    switch (node.kind) {
    case Node::AllKeys:
        next.kind = Node::Key;
        next.key = page.bars[index].key;
        return next;
    case Node::Key:
        next.kind = Node::Pair;
        next.prevKey = page.bars[index].key;
        return next;
    case Node::AllFingers:
        next.kind = Node::Finger;
        next.finger = index;
        return next;
    case Node::Finger:
        next.kind = Node::FingerRelation;
        next.relation = index;
        return next;
    default:
        return {};
    }
}

QString hint(const Bar &bar, const QLocale &loc)
{
    QString s = formatFixed(bar.value, 3, loc);
    if (bar.count >= 0)
        s += QLatin1String(" (") + QString::number(bar.count) + QLatin1Char(')');
    return s + QLatin1Char(' ') + shown(bar.label);
}

} // namespace Histograms
