#include "TsfFile.h"

#include "Cp1251.h"
#include "Keyboard.h"
#include "TsfSignature.h"

#include <QFile>
#include <cctype>

namespace {

// Mimics TStringList::LoadFromFile line splitting (CR, LF or CRLF).
QStringList splitLines(const QString &text)
{
    QStringList lines;
    int start = 0;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        QChar c = text[i];
        if (c == u'\r' || c == u'\n') {
            lines.append(text.mid(start, i - start));
            if (c == u'\r' && i + 1 < n && text[i + 1] == u'\n')
                ++i;
            start = i + 1;
        }
    }
    if (start < n)
        lines.append(text.mid(start));
    return lines;
}

// TStrings::Values[name]: first "name=value" line, name compared case-insensitively.
QString value(const QStringList &lines, const QString &name)
{
    for (const QString &l : lines) {
        int eq = l.indexOf(u'=');
        if (eq == name.size() && l.left(eq).compare(name, Qt::CaseInsensitive) == 0)
            return l.mid(eq + 1);
    }
    return {};
}

bool isHex(QChar c) { return c.isDigit() || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F'); }

// sscanf(line, "%x %s", &a, buf) == 2
bool scanHexAndWord(const QString &l, quint32 &a, QString &word)
{
    int i = 0, n = l.size();
    while (i < n && l[i].isSpace()) ++i;
    if (i + 1 < n && l[i] == u'0' && (l[i + 1] == u'x' || l[i + 1] == u'X') && i + 2 < n && isHex(l[i + 2]))
        i += 2;
    int s = i;
    quint32 v = 0;
    while (i < n && isHex(l[i])) {
        v = v * 16 + QStringView(l).mid(i, 1).toUInt(nullptr, 16);
        ++i;
    }
    if (i == s)
        return false;
    while (i < n && l[i].isSpace()) ++i;
    int w = i;
    while (i < n && !l[i].isSpace()) ++i;
    if (i == w)
        return false;
    a = v;
    word = l.mid(w, i - w);
    return true;
}

// sscanf(line, "%d %d", &a, &b) == 2
bool scanTwoInts(const QString &l, qint64 &a, qint64 &b)
{
    int i = 0, n = l.size();
    auto readInt = [&](qint64 &out) {
        while (i < n && l[i].isSpace()) ++i;
        int s = i;
        if (i < n && (l[i] == u'-' || l[i] == u'+')) ++i;
        int d = i;
        while (i < n && l[i].isDigit()) ++i;
        if (i == d)
            return false;
        out = QStringView(l).mid(s, i - s).toLongLong();
        return true;
    };
    return readInt(a) && readInt(b);
}

// TryStrToInt64("0x" + word)
bool parseHex64(const QString &word, quint64 &out)
{
    if (word.isEmpty() || word.size() > 16)
        return false;
    for (QChar c : word)
        if (!isHex(c))
            return false;
    bool ok = false;
    out = word.toULongLong(&ok, 16);
    return ok;
}

} // namespace

namespace Tsf {

QString dataLine(const KeyRecord &r)
{
    quint64 packed = (quint64(r.ch) << 32) | (r.flags & ~quint32(KeyRecord::Transient));
    QString line = QStringLiteral("%1 %2")
                       .arg(r.dtUs, 8, 16, QLatin1Char('0'))
                       .arg(packed, 12, 16, QLatin1Char('0'))
                       .toUpper();
    if (!r.comment.isEmpty())
        line += QStringLiteral("\t;") + r.comment;
    return line;
}

TsfDocument parse(const QStringList &lines, ReadError *err)
{
    TsfDocument doc;
    doc.version = value(lines, QStringLiteral("tsfVersion")).toInt();
    if (err)
        *err = doc.version > kCurrentVersion ? ReadError::NewerVersion : ReadError::None;

    for (const QString &l : lines) {
        KeyRecord r;
        if (doc.version == 0) {
            qint64 t, key;
            if (!scanTwoInts(l, t, key))
                continue;
            quint32 k = quint32(key);
            r.dtUs = quint32(t);
            r.ch = Cp1251::toUnicode(k & 0xFF);
            r.flags = (k & 0xFFFFFF00u) | Keyboard::vkToScan((k >> 16) & 0xFF);
        } else {
            quint32 t;
            QString word;
            quint64 packed;
            if (!scanHexAndWord(l, t, word) || !parseHex64(word, packed))
                continue;
            r.dtUs = t;
            r.flags = quint32(packed);
            r.ch = char16_t(packed >> 32);
        }
        int semi = l.indexOf(u';');
        if (semi >= 0)
            r.comment = l.mid(semi + 1);
        doc.records.append(r);
    }

    doc.author = value(lines, QStringLiteral("autor"));
    doc.comment = value(lines, QStringLiteral("comment"));
    doc.date = value(lines, QStringLiteral("date"));
    doc.fingerZonesName = value(lines, QStringLiteral("FingerZonesName"));
    if (!doc.fingerZonesName.isEmpty())
        for (int i = 0; i < 8; ++i)
            doc.fingers.append(value(lines, QStringLiteral("Finger%1").arg(i)));
    doc.attachedVideo = value(lines, QStringLiteral("AttachedVideo"));
    if (!doc.attachedVideo.isEmpty())
        doc.videoTimeShiftMs = value(lines, QStringLiteral("VideoTimeShift")).toInt();

    const QString sig = value(lines, QStringLiteral("signature"));
    doc.signed_ = !sig.isEmpty();
    doc.signatureValid = sig == TsfSignature::compute(doc);
    return doc;
}

QStringList serialize(const TsfDocument &doc, bool sign)
{
    QStringList lines;
    lines.reserve(doc.records.size() + 16);
    for (const KeyRecord &r : doc.records)
        lines.append(dataLine(r));
    lines.append(QStringLiteral("tsfVersion=%1").arg(kCurrentVersion));
    if (!doc.author.isEmpty())
        lines.append(QStringLiteral("autor=") + doc.author);
    if (!doc.comment.isEmpty()) {
        QString c = doc.comment;
        c.remove(u'\r');
        c.remove(u'\n');
        lines.append(QStringLiteral("comment=") + c);
    }
    if (!doc.date.isEmpty())
        lines.append(QStringLiteral("date=") + doc.date);
    if (sign)
        lines.append(QStringLiteral("signature=") + TsfSignature::compute(doc));
    if (!doc.fingerZonesName.isEmpty()) {
        lines.append(QStringLiteral("FingerZonesName=") + doc.fingerZonesName);
        for (int i = 0; i < 8; ++i)
            lines.append(QStringLiteral("Finger%1=%2").arg(i).arg(doc.fingers.value(i)));
    }
    if (!doc.attachedVideo.isEmpty()) {
        lines.append(QStringLiteral("AttachedVideo=") + doc.attachedVideo);
        if (doc.videoTimeShiftMs != 0)
            lines.append(QStringLiteral("VideoTimeShift=%1").arg(doc.videoTimeShiftMs));
    }
    return lines;
}

ReadError read(const QString &path, TsfDocument &doc)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return ReadError::CannotOpen;
    ReadError err;
    doc = parse(splitLines(Cp1251::decode(f.readAll())), &err);
    return err;
}

bool write(const QString &path, const TsfDocument &doc, bool sign)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QString text = serialize(doc, sign).join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
    return f.write(Cp1251::encode(text)) >= 0;
}

} // namespace Tsf
