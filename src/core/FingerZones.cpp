#include "FingerZones.h"

#include "Recalc.h"

namespace {
bool isHexDigit(QChar c) { return c.isDigit() || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F'); }

// 0x59e204: the built-in scheme, home key first.
const std::array<QVector<quint8>, FingerZones::kFingers> kStandard = {{
    {0x1E, 0x2C, 0x10, 0x29, 0x02, 0x2A},
    {0x1F, 0x2D, 0x11, 0x03},
    {0x20, 0x2E, 0x12, 0x04},
    {0x21, 0x2F, 0x30, 0x22, 0x13, 0x14, 0x05, 0x06, 0x07},
    {0x24, 0x31, 0x32, 0x23, 0x15, 0x16, 0x08},
    {0x25, 0x33, 0x17, 0x09},
    {0x26, 0x34, 0x18, 0x0A},
    {0x27, 0x35, 0x28, 0x19, 0x1A, 0x1B, 0x0B, 0x0C, 0x0D, 0x2B, 0x36},
}};
}

void FingerZones::add(int finger, int scan)
{
    m_keys[finger].append(quint8(scan));
    m_table[scan] = quint8(finger);
}

FingerZones FingerZones::standard()
{
    FingerZones z;
    for (int f = 0; f < kFingers; ++f)
        for (quint8 s : kStandard[f])
            z.add(f, s);
    z.m_readOnly = true;
    return z;
}

FingerZones FingerZones::fromStrings(const QStringList &fingers)
{
    FingerZones z;
    for (int f = 0; f < kFingers && f < fingers.size(); ++f) {
        const QString &s = fingers[f];
        for (int i = 0; i + 1 < s.size(); i += 2) {
            // StrToIntDef("0x" + pair, 0): anything but two hex digits gives 0.
            const bool ok = isHexDigit(s[i]) && isHexDigit(s[i + 1]);
            const int scan = ok ? QStringView(s).mid(i, 2).toInt(nullptr, 16) : 0;
            if (scan < 0x80)
                z.add(f, scan);
        }
    }
    return z;
}

QString FingerZones::toString(int finger) const
{
    QString s;
    for (quint8 k : m_keys[finger])
        s += QStringLiteral("%1").arg(k, 2, 16, QLatin1Char('0')).toUpper();
    return s;
}

QStringList FingerZones::toStrings() const
{
    QStringList r;
    for (int f = 0; f < kFingers; ++f)
        r << toString(f);
    return r;
}

bool FingerZones::isHome(int scan) const
{
    const quint8 f = finger(scan);
    return f < kFingers && !m_keys[f].isEmpty() && m_keys[f].first() == scan;
}

void FingerZones::assign(int scan, int finger, bool home)
{
    if (scan < 0 || scan >= 0x80)
        return;
    const quint8 old = m_table[scan];
    if (old < kFingers) {
        if (old == finger && !home)
            return;
        m_keys[old].removeOne(quint8(scan));
    }
    if (finger < kFingers) {
        if (home)
            m_keys[finger].prepend(quint8(scan));
        else
            m_keys[finger].append(quint8(scan));
        m_table[scan] = quint8(finger);
    } else {
        m_table[scan] = kNone;
    }
}

bool FingerZones::operator==(const FingerZones &o) const
{
    if (m_table != o.m_table)
        return false;
    for (int f = 0; f < kFingers; ++f)
        if (!m_keys[f].isEmpty() && (o.m_keys[f].isEmpty() || m_keys[f].first() != o.m_keys[f].first()))
            return false;
    return true;
}

QVector<quint8> fingerSeries(const TextModel &m, const FingerZones &zones)
{
    QVector<quint8> r;
    r.reserve(m.size());
    for (quint32 f : m.flags)
        r << zones.finger(f);
    return r;
}

// ---------------------------------------------------------------- schemes

FingerZoneSchemes::FingerZoneSchemes(const QString &iniPath, const QString &standardName)
    : m_path(iniPath), m_standardName(standardName)
{
    if (!m_path.isEmpty())
        m_ini.load(m_path);
}

QStringList FingerZoneSchemes::names() const
{
    return QStringList{m_standardName} + m_ini.sections();
}

bool FingerZoneSchemes::contains(const QString &name) const
{
    return names().contains(name, Qt::CaseInsensitive); // CB_FINDSTRINGEXACT
}

FingerZones FingerZoneSchemes::zones(const QString &name) const
{
    if (name == m_standardName)
        return FingerZones::standard();
    QStringList fingers;
    for (int f = 0; f < FingerZones::kFingers; ++f)
        fingers << m_ini.value(name, QStringLiteral("Finger%1").arg(f));
    return FingerZones::fromStrings(fingers);
}

void FingerZoneSchemes::store(const QString &name, const FingerZones &zones)
{
    if (name == m_standardName)
        return;
    for (int f = 0; f < FingerZones::kFingers; ++f)
        m_ini.setValue(name, QStringLiteral("Finger%1").arg(f), zones.toString(f));
    if (!m_path.isEmpty())
        m_ini.save(m_path);
}

void FingerZoneSchemes::remove(const QString &name)
{
    if (name == m_standardName)
        return;
    m_ini.removeSection(name);
    if (!m_path.isEmpty())
        m_ini.save(m_path);
}

QString FingerZoneSchemes::adopt(const QString &nameInFile, const FingerZones &z)
{
    for (const QString &n : names())
        if (zones(n) == z)
            return n;
    QString name = nameInFile;
    while (contains(name))
        name += u'_';
    store(name, z);
    return name;
}
