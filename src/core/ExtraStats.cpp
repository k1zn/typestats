#include "ExtraStats.h"

#include "Ext80.h"
#include "NumberFormat.h"
#include "FingerZones.h"
#include "IniFile.h"

#include <QFile>

#include <algorithm>

namespace ExtraStats {

namespace {

const QString kEnter = QStringLiteral("\r");

// FUN_0043f6e8
QString shown(const QString &name)
{
    return name == kEnter ? QStringLiteral("[Enter]") : name;
}

// (length - 1) characters typed in sumMs.
float speedOf(qsizetype length, float sumMs)
{
    return float(Ext(length * 60000 - 60000) / Ext(sumMs));
}

bool isRuLower(QChar c) { return (c >= QChar(0x430) && c <= QChar(0x44F)) || c == QChar(0x451); }
bool isRuUpper(QChar c) { return (c >= QChar(0x410) && c <= QChar(0x42F)) || c == QChar(0x401); }

bool matches(const TemplateItem &t, QChar ch, bool erased, quint8 finger)
{
    static const QString punct = QStringLiteral("`~!@#$%^&*()_+|/=-?:;№\"[]{}'<>/\\.,");
    switch (t.code) {
    case TemplateItem::RuLower: return isRuLower(ch);
    case TemplateItem::RuUpper: return isRuUpper(ch);
    case TemplateItem::Any: return true;
    case TemplateItem::Digit: return ch >= u'0' && ch <= u'9';
    case TemplateItem::Punct: return !ch.isNull() && punct.contains(ch);
    case TemplateItem::Erased: return erased;
    case TemplateItem::Literal: return ch == t.ch;
    case TemplateItem::Finger: return t.ch.unicode() - u'1' == finger;
    case TemplateItem::Slash: return ch == u'/';
    case TemplateItem::Hand: return finger <= 7 && FingerZones::isLeftHand(finger) == (t.ch == u'(');
    case TemplateItem::LatLower: return ch >= u'a' && ch <= u'z';
    case TemplateItem::LatUpper: return ch >= u'A' && ch <= u'Z';
    }
    return false;
}

void collectNGrams(const TextModel &m, int b, int e, int n, const CharFilter &filter, QVector<Occurrence> &out)
{
    for (int p = b; p + n <= e; ++p) {
        QString text;
        float sum = 0;
        bool ok = true;
        for (int k = 0; k < n && ok; ++k) {
            const int i = p + k;
            if (m.erased(i) || (k != 0 && m.startsFragment(i))) {
                ok = false;
                break;
            }
            text += shown(m.names[i]);
            if (k != 0)
                sum += m.pauses[i];
        }
        if (ok && filter.pass(text))
            out.append({sum > 10.0f ? speedOf(n, sum) : 0.0f, p, text});
    }
}

void collectWords(const TextModel &m, int b, int e, Kind kind, const CharFilter &filter, QVector<Occurrence> &out)
{
    QString cur;
    int start = b;
    float sum = 0;
    bool hadErr = false, sentEnd = false;
    for (int p = b; p <= e; ++p) {
        const bool end = p >= e;
        const QString name = end ? QString() : m.names[p];
        bool sep = end, brk = end;
        if (!end) {
            sep = (name == QLatin1String(" ") || name == kEnter) && !m.erased(p);
            brk = (sep && (kind != Sentences || sentEnd)) || name == kEnter;
            if (!brk) {
                if (m.startsFragment(p))
                    brk = true;
                else if (!cur.isEmpty())
                    sum += m.pauses[p];
            }
        }
        if (brk) {
            if (kind == WordsWithErrors)
                hadErr = !hadErr;
            else if (kind == Sentences)
                hadErr = false;
            if (cur.size() > 1 && !hadErr && filter.pass(cur))
                out.append({sum > 0.01 ? speedOf(cur.size(), sum) : 0.0f, start, cur});
            hadErr = false;
            cur.clear();
        }
        if (cur.isEmpty() && !sep) {
            start = p;
            sum = 0;
        }
        if (!cur.isEmpty() || !sep) {
            if (!m.erased(p)) {
                cur += shown(name);
                sentEnd = name == QLatin1String(".") || name == QLatin1String("!") || name == QLatin1String("?");
            } else {
                hadErr = true;
            }
        }
    }
}

void collectTemplate(const TextModel &m, const QVector<quint8> &fingers, int b, int e, const QString &pattern,
                     const CharFilter &filter, QVector<Occurrence> &out)
{
    const QVector<TemplateItem> tpl = parseTemplate(pattern);
    const int L = tpl.size();
    if (L == 0)
        return;
    for (int p = b; p + L <= e; ++p) {
        QString text;
        float sum = 0;
        bool ok = true;
        for (int k = 0; k < L; ++k) {
            const int i = p + k;
            const QString &name = m.names[i];
            const bool erased = m.erased(i);
            if (name == kEnter || (k != 0 && m.startsFragment(i)) || (erased && tpl[k].code != TemplateItem::Erased)
                || !matches(tpl[k], name.size() == 1 ? name[0] : QChar(), erased,
                            i < fingers.size() ? fingers[i] : FingerZones::kNone)) {
                ok = false;
                break;
            }
            text += name;
            if (k != 0)
                sum += m.pauses[i];
        }
        if (ok && filter.pass(text))
            out.append({sum > 0.01 ? speedOf(L, sum) : 0.0f, p, text});
    }
}

} // namespace

bool CharFilter::pass(QStringView text) const
{
    bool pass = !anyOn;
    for (QChar c : text) {
        if (onlyOn && !only.contains(c))
            return false;
        if (!pass && any.contains(c))
            pass = true;
        if (excludeOn && exclude.contains(c))
            return false;
    }
    return pass;
}

QVector<TemplateItem> parseTemplate(QStringView pattern)
{
    QVector<TemplateItem> out;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        QChar c = pattern[i];
        if (c != u'/' || i + 1 == pattern.size()) {
            out.append({TemplateItem::Literal, c});
            continue;
        }
        c = pattern[++i];
        TemplateItem::Code code = TemplateItem::Literal;
        switch (c.unicode()) {
        case u'б': code = TemplateItem::RuLower; break;
        case u'Б': code = TemplateItem::RuUpper; break;
        case u'*': code = TemplateItem::Any; break;
        case u'0': code = TemplateItem::Digit; break;
        case u',': code = TemplateItem::Punct; break;
        case u'е': case u'e': code = TemplateItem::Erased; break;
        case u'с': case u'c': code = TemplateItem::LatLower; break;
        case u'С': case u'C': code = TemplateItem::LatUpper; break;
        case u'(': case u')': code = TemplateItem::Hand; break;
        case u'/': code = TemplateItem::Slash; break;
        default:
            if (c >= u'1' && c <= u'9')
                code = TemplateItem::Finger;
        }
        out.append({code, c});
    }
    return out;
}

QVector<Occurrence> collect(const TextModel &m, const QVector<quint8> &fingers, int b, int e, Kind kind,
                            const QString &pattern, const CharFilter &filter)
{
    QVector<Occurrence> out;
    b = std::max(b, 0);
    e = std::min<int>(e, m.size());
    if (b > e)
        return out;
    switch (kind) {
    case Pairs:
    case Triples:
    case Quads:
        collectNGrams(m, b, e, int(kind) + 2, filter, out);
        break;
    case Words:
    case WordsWithErrors:
    case Sentences:
        collectWords(m, b, e, kind, filter, out);
        break;
    case Template:
        collectTemplate(m, fingers, b, e, pattern, filter, out);
        break;
    default:
        break;
    }
    return out;
}

QVector<Row> rows(const QVector<Occurrence> &occ, bool averages, int sortMode, bool descending)
{
    QVector<Row> r;
    const auto byText = [](const auto &a, const auto &b) { return a.text < b.text; };
    const auto bySpeed = [](const auto &a, const auto &b) { return a.speed < b.speed; };
    if (!averages) {
        r.reserve(occ.size());
        for (const Occurrence &o : occ)
            r.append({o.speed, o.pos, o.text});
        if (sortMode == 1)
            std::stable_sort(r.begin(), r.end(), byText);
        else
            std::stable_sort(r.begin(), r.end(), bySpeed);
    } else {
        QVector<Occurrence> sorted = occ;
        std::stable_sort(sorted.begin(), sorted.end(), byText);
        for (qsizetype i = 0; i < sorted.size();) {
            float sum = 0;
            int count = 0;
            qsizetype j = i;
            for (; j < sorted.size() && sorted[j].text == sorted[i].text; ++j, ++count)
                sum += sorted[j].speed;
            r.append({float(Ext(sum) / Ext(count)), count, sorted[i].text});
            i = j;
        }
        // The groups are in text order already.
        if (sortMode == 0)
            std::stable_sort(r.begin(), r.end(), bySpeed);
        else if (sortMode == 2)
            std::stable_sort(r.begin(), r.end(), [](const Row &a, const Row &b) { return a.value < b.value; });
        else if (sortMode == 3)
            std::stable_sort(r.begin(), r.end(), [](const Row &a, const Row &b) { return a.value > b.value; });
    }
    if (descending)
        std::reverse(r.begin(), r.end());
    return r;
}

QVector<Occurrence> occurrences(const QVector<Occurrence> &occ, const QString &text)
{
    QVector<Occurrence> r;
    for (const Occurrence &o : occ)
        if (o.text == text)
            r.append(o);
    std::stable_sort(r.begin(), r.end(), [](const Occurrence &a, const Occurrence &b) { return a.speed < b.speed; });
    return r;
}

void Sort::clickColumn(int column, bool averages)
{
    if (column == 0 || column == 1) {
        // Another column keeps the direction (as the original does).
        if (mode == column)
            descending = !descending;
        else
            mode = column;
    } else if (column == 2 && averages) {
        mode = mode == 2 ? 3 : 2;
        descending = false;
    }
}

void Sort::setAverages(bool averages)
{
    if (!averages && mode > 1)
        mode = 0;
}

QStringList Sort::headers(bool averages, const QStringList &captions) const
{
    QStringList h = captions.mid(0, averages ? 3 : 2);
    const int column = std::min(mode, 2);
    if (column < h.size())
        h[column].prepend(!descending && mode != 3 ? QChar(0x25B2) : QChar(0x25BC));
    return h;
}

QString formatSpeed(float speed, const QLocale &loc)
{
    return formatFixed(speed, 2, loc);
}

QString toText(const QVector<Row> &rows, bool averages, const QLocale &loc, const QStringList &captions)
{
    QString out = captions.value(0) + QLatin1Char('\t') + captions.value(1);
    if (averages)
        out += QLatin1Char('\t') + captions.value(2);
    out += QLatin1String("\r\n");
    for (const Row &r : rows) {
        out += r.text + QLatin1Char('\t') + formatSpeed(r.speed, loc);
        if (averages)
            out += QLatin1Char('\t') + QString::number(r.value);
        out += QLatin1String("\r\n");
    }
    return out;
}

TemplateList::TemplateList(const QString &path) : m_path(path)
{
    QFile f(path);
    if (!path.isEmpty() && f.open(QIODevice::ReadOnly))
        m_items = decode(f.readAll());
}

QStringList TemplateList::decode(const QByteArray &bytes)
{
    QString text;
    if (bytes.startsWith("\xFF\xFE"))
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2), (bytes.size() - 2) / 2);
    else
        text = IniFile::decode(bytes);
    QStringList items;
    for (QStringView line : QStringView(text).split(u'\n')) {
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (!line.isEmpty())
            items << line.toString();
    }
    return items;
}

QByteArray TemplateList::encode(const QStringList &items)
{
    QString text;
    for (const QString &s : items)
        text += s + QLatin1String("\r\n");
    QByteArray bytes("\xFF\xFE");
    bytes.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    return bytes;
}

bool TemplateList::add(const QString &pattern)
{
    if (pattern.isEmpty() || m_items.contains(pattern))
        return false;
    m_items << pattern;
    save();
    return true;
}

bool TemplateList::remove(const QString &pattern)
{
    if (!m_items.removeOne(pattern))
        return false;
    save();
    return true;
}

void TemplateList::save() const
{
    QFile f(m_path);
    if (!m_path.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(encode(m_items));
}

} // namespace ExtraStats
