#include "Recalc.h"

#include "Cp1251.h"
#include "KeyName.h"

#include <algorithm>
#include <bitset>
#include <list>

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

    bool hasPressedKeys() const { return !m_pressed.empty(); }

    void addPress(const KeyRecord &r, qint64 t, quint32 extraFlags = 0)
    {
        m_out.append({t, t - m_baseSeg, t - m_baseDraw, r.flags | extraFlags, r.ch, true});
        m_pressed.push_back(r.flags & KeyRecord::VkMask);
    }

    void addSegmentStart(const KeyRecord &r, qint64 t)
    {
        if (m_out.isEmpty()) {
            m_baseSeg = m_baseDraw = t;
        } else {
            const qint64 last = m_out.last().t;
            m_baseSeg = last;
            m_baseDraw += (t - last) - 200000;
        }
        addPress(r, t, KeyRecord::SegmentStart);
    }

    bool addRelease(const KeyRecord &r, qint64 t)
    {
        const quint32 vk = r.flags & KeyRecord::VkMask;
        const auto before = m_pressed.size();
        m_pressed.remove(vk);
        if (m_pressed.size() == before)
            return false;
        m_out.append({t, t - m_baseSeg, t - m_baseDraw, r.flags, r.ch, false});
        return true;
    }

private:
    QVector<KlavRecord> &m_out;
    std::list<quint32> m_pressed;
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

namespace Recalc {

void normalize(KeyRecords &recs)
{
    auto first = std::find_if(recs.begin(), recs.end(), [](const KeyRecord &r) { return r.isDown(); });
    recs.erase(recs.begin(), first);
    if (recs.isEmpty())
        return;
    recs[0].dtUs = 60000000;

    std::bitset<256> down;
    quint64 acc = 0;
    int out = 0;
    for (int i = 0; i < recs.size(); ++i) {
        KeyRecord r = recs[i];
        acc += r.dtUs;
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
            recs[out++] = r;
            acc = 0;
        }
    }
    recs.resize(out);
}

void markErased(KeyRecords &recs)
{
    static const QByteArray punct = QByteArrayLiteral("!\";%:?*(-=+\\/#@`~[]{}'<>,.");
    int bs = 0;     // pending BackSpace
    int cbs = 0;    // pending Ctrl+BackSpace
    int mode = 0;   // Ctrl+BackSpace: 0 undecided, 1 eating punctuation, 2 eating a word
    for (KeyRecord &r : recs)
        r.flags &= ~(KeyRecord::Erased | KeyRecord::SegmentStart);
    for (int i = recs.size() - 1; i >= 0; --i) {
        KeyRecord &r = recs[i];
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
        const QByteArray name = Cp1251::encode(keyDisplayName(r.flags, r.ch));
        if (name.size() != 1)
            continue;
        if (cbs == 0) {
            if (bs != 0) {
                r.flags |= KeyRecord::Erased;
                --bs;
            }
            continue;
        }
        const char c = name[0];
        bool stop;
        do {
            stop = false;
            if (punct.contains(c)) {
                if (mode == 0)
                    mode = 1;
                if (mode == 1)
                    r.flags |= KeyRecord::Erased;
                else
                    stop = true;
            } else if (c == ' ') {
                if (mode == 0)
                    r.flags |= KeyRecord::Erased;
                else
                    stop = true;
            } else {
                if (mode == 0)
                    mode = 2;
                if (mode == 2)
                    r.flags |= KeyRecord::Erased;
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
}

TextModel build(KeyRecords &recs, const RecalcOptions &opt)
{
    TextModel m;
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

    auto pushMap = [&](int ri) {
        m.mapPos.append(pos);
        m.mapElem.append(elem);
        m.mapKlav.append(klavIdx);
        m.mapRec.append(ri);
    };
    auto fixPositions = [&](int from) {
        for (int k = from; k < m.mapPos.size(); ++k)
            m.mapPos[k] = pos;
    };

    for (int ri = 0; ri < recs.size(); ++ri) {
        KeyRecord &r = recs[ri];
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
        QString name = keyDisplayName(r.flags, r.ch);
        if (name.size() > 1 && opt.onlyText && name != QLatin1String("[LShift]")
            && name != QLatin1String("[RShift]") && name != QLatin1String("[BackSpace]")
            && name != QLatin1String("[Ctrl+BackSpace]"))
            continue;
        pushMap(ri);
        if (sinceKlav <= split || klav.hasPressedKeys()) {
            klav.addPress(r, absT);
        } else {
            klav.addSegmentStart(r, absT);
            pendingSplit = true;
            splitMapIdx = m.mapPos.size() - 1;
        }
        sinceKlav = 0;

        quint8 style = 0;
        if (r.flags & KeyRecord::Erased) {
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
            if (pendingSplit) {
                pendingSplit = false;
                r.flags |= KeyRecord::SegmentStart;
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
            lastElemMapIdx = m.mapPos.size();
            m.pauses.append((r.flags & KeyRecord::SegmentStart) ? kFragmentStart : float(0.001L * acc));
            m.names.append(name);
            m.flags.append(r.flags);
            m.recIndex.append(ri);
            ++elem;
            acc = 0;
        }
        ++klavIdx;
    }
    flushParagraph(m, segs);
    return m;
}

TextModel run(KeyRecords &recs, const RecalcOptions &opt)
{
    normalize(recs);
    markErased(recs);
    return build(recs, opt);
}

int lowerBound(const QVector<int> &v, int x)
{
    return int(std::lower_bound(v.begin(), v.end(), x) - v.begin());
}

int at(const QVector<int> &v, int i)
{
    if (v.isEmpty())
        return 0;
    return i < v.size() ? v[i] : v.last() + 1;
}

} // namespace Recalc
