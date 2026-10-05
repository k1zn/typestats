#include "Recalc.h"

#include "Cp1251.h"
#include "Ext80.h"
#include "KeyName.h"

#include <algorithm>
#include <atomic>
#include <bitset>

namespace {

bool isModifierVk(quint8 vk)
{
    return vk == 0x5B || vk == 0x5C || (vk >= 0xA0 && vk <= 0xA5);
}

// The klavogram object (DAT_005b1348).
class Klavogram
{
public:
    explicit Klavogram(QVector<KlavRecord> &out) : m_out(out) {}

    bool hasPressedKeys() const { return !m_pressed.isEmpty(); }

    void addPress(const KeyRecord &r, qint64 t, bool erased, bool fragmentStart = false)
    {
        m_out.append({t, t - m_baseSeg, t - m_baseDraw, r.flags, r.ch, true, erased, fragmentStart});
        m_pressed.append(r.flags & KeyRecord::VkMask);
    }

    void addFragmentStart(const KeyRecord &r, qint64 t, bool erased)
    {
        if (m_out.isEmpty()) {
            m_baseSeg = m_baseDraw = t;
        } else {
            const qint64 last = m_out.last().t;
            m_baseSeg = last;
            m_baseDraw += (t - last) - 200000;
        }
        addPress(r, t, erased, true);
    }

    bool addRelease(const KeyRecord &r, qint64 t)
    {
        const quint32 vk = r.flags & KeyRecord::VkMask;
        if (m_pressed.removeAll(vk) == 0)
            return false;
        m_out.append({t, t - m_baseSeg, t - m_baseDraw, r.flags, r.ch, false});
        return true;
    }

private:
    QVector<KlavRecord> &m_out;
    QVector<quint32> m_pressed;
    qint64 m_baseSeg = 0;
    qint64 m_baseDraw = 0;
};

struct Segment
{
    QString text;
    quint8 style;
};

// FUN_0040c5d0: appends the collected segments as a new paragraph and returns the
// position the next paragraph will start at.
int flushParagraph(TextModel &m, QVector<Segment> &segs)
{
    if (!m.text.isEmpty())
        m.text += QLatin1Char('\n');
    for (const Segment &s : segs) {
        if (s.style && !s.text.isEmpty())
            m.runs.append({int(m.text.size()), int(s.text.size()), s.style});
        m.text += s.text;
    }
    segs.clear();
    return m.text.isEmpty() ? 0 : int(m.text.size()) + 1;
}

bool isParenthesized(const QString &c)
{
    return c.startsWith(QLatin1Char('(')) && c.endsWith(QLatin1Char(')'));
}

} // namespace

bool TextModel::startsFragment(int i) const
{
    return std::binary_search(fragmentStarts.begin(), fragmentStarts.end(), i);
}

std::pair<int, int> TextModel::fragmentAt(int i) const
{
    const auto next = std::upper_bound(fragmentStarts.begin(), fragmentStarts.end(), i);
    return {next == fragmentStarts.begin() ? 0 : *(next - 1), next == fragmentStarts.end() ? size() : *next};
}

int TextModel::lookup(int TextAnchor::*key, int x, int TextAnchor::*value) const
{
    const auto it = std::lower_bound(anchors.begin(), anchors.end(), x,
                                     [key](const TextAnchor &a, int v) { return a.*key < v; });
    if (it != anchors.end())
        return (*it).*value;
    return anchors.isEmpty() ? 0 : anchors.last().*value + 1;
}

namespace Recalc {

KeyRecords normalized(const KeyRecords &recs)
{
    KeyRecords out;
    const auto first = std::find_if(recs.begin(), recs.end(), [](const KeyRecord &r) { return r.isDown(); });
    out.reserve(recs.end() - first);

    std::bitset<256> down;
    quint64 acc = 0;
    for (auto it = first; it != recs.end(); ++it) {
        KeyRecord r = *it;
        r.flags &= ~(KeyRecord::Transient | KeyRecord::SegmentStart); // in-memory marks of the original
        acc += it == first ? 60000000u : r.dtUs;
        const quint8 vk = r.vk();
        bool keep = true;
        if (r.isUp()) {
            down.reset(vk);
        } else {
            keep = !down.test(vk) || !isModifierVk(vk);
            down.set(vk);
        }
        if (keep) {
            r.dtUs = quint32(acc);
            out.append(r);
            acc = 0;
        }
    }
    return out;
}

QVector<bool> erasedRecords(const KeyRecords &recs)
{
    QVector<bool> erased(recs.size(), false);
    static const QByteArray punct = QByteArrayLiteral("!\";%:?*(-=+\\/#@`~[]{}'<>,.");
    int bs = 0;     // pending BackSpace
    int cbs = 0;    // pending Ctrl+BackSpace
    int mode = 0;   // Ctrl+BackSpace: 0 undecided, 1 eating punctuation, 2 eating a word
    for (int i = recs.size() - 1; i >= 0; --i) {
        const KeyRecord &r = recs[i];
        if (r.isUp())
            continue;
        if (r.vk() == 0x08) {
            if (!(r.flags & KeyRecord::Ctrl)) {
                ++bs;
            } else {
                ++cbs;
                mode = 0;
            }
            continue;
        }
        const std::optional<char16_t> single = keyDisplayChar(r.flags, r.ch);
        if (!single)
            continue;
        if (cbs == 0) {
            if (bs != 0) {
                erased[i] = true;
                --bs;
            }
            continue;
        }
        const char c = Cp1251::fromUnicode(*single);
        bool stop;
        do {
            stop = false;
            if (punct.contains(c)) {
                if (mode == 0)
                    mode = 1;
                if (mode == 1)
                    erased[i] = true;
                else
                    stop = true;
            } else if (c == ' ') {
                if (mode == 0)
                    erased[i] = true;
                else
                    stop = true;
            } else {
                if (mode == 0)
                    mode = 2;
                if (mode == 2)
                    erased[i] = true;
                else
                    stop = true;
            }
            if (stop) {
                --cbs;
                mode = 0;
                bs = 0;
            }
        } while (cbs != 0 && stop);
    }
    return erased;
}

TextModel run(const KeyRecords &document, const RecalcOptions &opt)
{
    static std::atomic<quint64> serials{0};
    TextModel m;
    m.serial = ++serials;
    m.keyNames = opt.keyNames;
    m.records = normalized(document);
    m.recErased = erasedRecords(m.records);
    const KeyRecords &recs = m.records;
    // The model lives as long as the recording is open: its vectors get their sizes at once rather
    // than room left from growing. Every record gives at most one klavogram record, every press at
    // most one element.
    const qsizetype presses = std::count_if(recs.begin(), recs.end(), [](const KeyRecord &r) { return r.isDown(); });
    m.klav.reserve(recs.size());
    m.flags.reserve(presses);
    m.recIndex.reserve(presses);
    m.names.reserve(presses);
    m.pauses.reserve(presses);
    m.anchors.reserve(presses);
    Klavogram klav(m.klav);
    QVector<Segment> segs;
    const double split = opt.splitMs * 1000.0;

    int pos = 0;
    int elem = 0;
    int klavIdx = 0;
    quint32 acc = 0;          // µs since the previous element
    qint64 absT = 0;          // µs since the first record
    double sinceKlav = 0;     // µs since the previous klavogram record
    bool pendingSplit = false;
    int splitMapIdx = 0;
    int lastElemMapIdx = 0;
    bool haveComment = false;
    QString comment;

    auto pushMap = [&](int ri) { m.anchors.append({pos, elem, klavIdx, ri}); };
    auto fixPositions = [&](int from) {
        for (int k = from; k < m.anchors.size(); ++k)
            m.anchors[k].pos = pos;
    };

    for (int ri = 0; ri < recs.size(); ++ri) {
        const KeyRecord &r = recs[ri];
        const bool erased = m.recErased[ri];
        acc += r.dtUs;
        absT += r.dtUs;
        sinceKlav += r.dtUs;
        if (r.isUp()) {
            if (klav.addRelease(r, absT)) {
                sinceKlav = 0;
                ++klavIdx;
            }
            continue;
        }
        if (!(r.flags & KeyRecord::Marked) && !haveComment && !r.comment.isEmpty()) {
            haveComment = true;
            comment = r.comment;
            pushMap(ri);
        }
        QString name = keyDisplayName(r.flags, r.ch, opt.keyNames);
        if (name.size() > 1 && opt.onlyText && name != QLatin1String("[LShift]")
            && name != QLatin1String("[RShift]") && name != QLatin1String("[BackSpace]")
            && name != QLatin1String("[Ctrl+BackSpace]"))
            continue;
        pushMap(ri);
        if (sinceKlav <= split || klav.hasPressedKeys()) {
            klav.addPress(r, absT, erased);
        } else {
            klav.addFragmentStart(r, absT, erased);
            pendingSplit = true;
            splitMapIdx = m.anchors.size() - 1;
        }
        sinceKlav = 0;

        quint8 style = 0;
        if (erased) {
            style |= TextStyle::Erased;
            if (name == QLatin1String(" "))
                name = QString(QChar(0x2588));
        }
        if (r.flags & KeyRecord::Marked)
            style |= TextStyle::Marked;
        if (r.flags & KeyRecord::Injected)
            style |= TextStyle::Injected;

        if ((!opt.onlyText || name.size() == 1) && !(r.flags & KeyRecord::DeadKey)
            && (!opt.onlyInjected || (style & TextStyle::Injected))) {
            const bool startsFragment = pendingSplit;
            if (pendingSplit) {
                pendingSplit = false;
                if (pos != 0) {
                    if (!opt.byPauses) {
                        segs.append({QString(QChar(0x2021)), TextStyle::Separator});
                        pos += 1;
                    } else {
                        if (!segs.isEmpty())
                            flushParagraph(m, segs);
                        segs.append({QString(8, QChar(0x2014)), TextStyle::Separator});
                        pos = flushParagraph(m, segs);
                    }
                    fixPositions(splitMapIdx);
                    pushMap(ri);
                }
            }
            if (haveComment) {
                haveComment = false;
                const bool inl = isParenthesized(comment);
                if (!inl && !segs.isEmpty())
                    flushParagraph(m, segs);
                segs.append({comment, TextStyle::Comment});
                if (inl)
                    pos += comment.size();
                else
                    pos = flushParagraph(m, segs);
                fixPositions(lastElemMapIdx);
                pushMap(ri);
            }
            if (name != QLatin1String("\r")) {
                segs.append({name, style});
                pos += name.size();
            } else {
                pos = flushParagraph(m, segs);
            }
            lastElemMapIdx = m.anchors.size();
            if (startsFragment)
                m.fragmentStarts.append(elem);
            m.pauses.append(float(kExtMilli * acc));
            m.names.append(name);
            m.flags.append(r.flags);
            m.recIndex.append(ri);
            ++elem;
            acc = 0;
        }
        ++klavIdx;
    }
    flushParagraph(m, segs);
    m.anchors.squeeze();
    return m;
}

} // namespace Recalc
