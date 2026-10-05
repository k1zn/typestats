#include "IniFile.h"

#include "Cp1251.h"

#include <QFile>
#include <QStringDecoder>

QString IniFile::decode(const QByteArray &bytes)
{
    QByteArray b = bytes;
    if (b.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(b.mid(3));
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString s = utf8(b);
    return utf8.hasError() ? Cp1251::decode(b) : s;
}

bool IniFile::load(const QString &path)
{
    m_sections.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    parse(decode(f.readAll()));
    return true;
}

bool IniFile::save(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // The original reads INI files as ANSI: cp1251 unless some character has no place in it.
    const QString text = toString();
    const QByteArray ansi = Cp1251::encode(text);
    return f.write(Cp1251::decode(ansi) == text ? ansi : text.toUtf8()) >= 0;
}

void IniFile::parse(const QString &text)
{
    m_sections.clear();
    Section *cur = nullptr;
    for (QStringView line : QStringView(text).split(u'\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';'))
            continue;
        if (line.startsWith(u'[')) {
            const qsizetype close = line.indexOf(u']');
            const QString name = line.mid(1, close < 0 ? -1 : close - 1).trimmed().toString();
            cur = const_cast<Section *>(find(name));
            if (!cur) {
                m_sections.append({name, {}});
                cur = &m_sections.last();
            }
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (!cur || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed().toString();
        QString v = line.mid(eq + 1).trimmed().toString();
        // GetPrivateProfileString strips one pair of surrounding quotes.
        if (v.size() >= 2 && (v.front() == u'"' || v.front() == u'\'') && v.back() == v.front())
            v = v.mid(1, v.size() - 2);
        bool found = false;
        for (auto &kv : cur->values)
            if (kv.first.compare(key, Qt::CaseInsensitive) == 0) {
                found = true; // the first occurrence wins, as in GetPrivateProfileString
                break;
            }
        if (!found)
            cur->values.append({key, v});
    }
}

QString IniFile::toString() const
{
    QString out;
    for (const Section &s : m_sections) {
        out += QLatin1Char('[') + s.name + QLatin1String("]\r\n");
        for (const auto &[k, v] : s.values)
            out += k + QLatin1Char('=') + v + QLatin1String("\r\n");
    }
    return out;
}

const IniFile::Section *IniFile::find(const QString &name) const
{
    for (const Section &s : m_sections)
        if (s.name.compare(name, Qt::CaseInsensitive) == 0)
            return &s;
    return nullptr;
}

QStringList IniFile::sections() const
{
    QStringList r;
    for (const Section &s : m_sections)
        r << s.name;
    return r;
}

bool IniFile::hasSection(const QString &section) const
{
    return find(section);
}

QStringList IniFile::keys(const QString &section) const
{
    QStringList r;
    if (const Section *s = find(section))
        for (const auto &kv : s->values)
            r << kv.first;
    return r;
}

QString IniFile::value(const QString &section, const QString &key, const QString &def) const
{
    if (const Section *s = find(section))
        for (const auto &[k, v] : s->values)
            if (k.compare(key, Qt::CaseInsensitive) == 0)
                return v;
    return def;
}

void IniFile::setValue(const QString &section, const QString &key, const QString &value)
{
    auto *s = const_cast<Section *>(find(section));
    if (!s) {
        m_sections.append({section, {}});
        s = &m_sections.last();
    }
    for (auto &kv : s->values)
        if (kv.first.compare(key, Qt::CaseInsensitive) == 0) {
            kv.second = value;
            return;
        }
    s->values.append({key, value});
}

void IniFile::removeSection(const QString &section)
{
    m_sections.removeIf([&](const Section &s) { return s.name.compare(section, Qt::CaseInsensitive) == 0; });
}
