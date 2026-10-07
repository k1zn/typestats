#include "TsfFile.h"

#include "Cp1251.h"
#include "Keyboard.h"
#include "TsfSignature.h"
#include "Z85.h"

#include <QCryptographicHash>
#include <QFile>
#include <QMap>

#include <array>
#include <optional>

namespace {

// Mimics TStringList::LoadFromFile line splitting (CR, LF or CRLF). The lines point into text.
QList<QStringView> splitLines(QStringView text)
{
    QList<QStringView> lines;
    qsizetype start = 0;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text[i];
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

// The header keys, read in one pass over the lines.
enum Key {
    TsfVersion, Autor, Comment, Date, FingerZonesName, Finger0, AttachedVideo = Finger0 + 8, VideoTimeShift, Signature, Platform,
    KeyCount
};

const std::array<QString, KeyCount> &keyNames()
{
    static const std::array<QString, KeyCount> names = [] {
        std::array<QString, KeyCount> n;
        n[TsfVersion] = QStringLiteral("tsfVersion");
        n[Autor] = QStringLiteral("autor");
        n[Comment] = QStringLiteral("comment");
        n[Date] = QStringLiteral("date");
        n[FingerZonesName] = QStringLiteral("FingerZonesName");
        for (int i = 0; i < 8; ++i)
            n[Finger0 + i] = QStringLiteral("Finger%1").arg(i);
        n[AttachedVideo] = QStringLiteral("AttachedVideo");
        n[VideoTimeShift] = QStringLiteral("VideoTimeShift");
        n[Signature] = QStringLiteral("signature");
        n[Platform] = QStringLiteral("Platform");
        return n;
    }();
    return names;
}

// TStrings::Values[name] of every key: the first "name=value" line, name compared case-insensitively.
std::array<QString, KeyCount> values(const QList<QStringView> &lines)
{
    const std::array<QString, KeyCount> &names = keyNames();
    std::array<QString, KeyCount> v;
    std::array<bool, KeyCount> found{};
    for (QStringView l : lines) {
        const qsizetype eq = l.indexOf(u'=');
        if (eq <= 0)
            continue;
        for (int k = 0; k < KeyCount; ++k)
            if (!found[k] && eq == names[k].size() && l.left(eq).compare(names[k], Qt::CaseInsensitive) == 0) {
                found[k] = true;
                v[k] = l.mid(eq + 1).toString();
            }
    }
    return v;
}

// A hex digit as sscanf and QString::toUInt see it: QChar::isDigit() takes other scripts' digits too,
// which toUInt does not convert (they count as 0). -1: not a digit.
int hexDigit(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9')
        return u - u'0';
    if (u >= u'a' && u <= u'f')
        return u - u'a' + 10;
    if (u >= u'A' && u <= u'F')
        return u - u'A' + 10;
    if (c.isDigit())
        return int(QStringView(&c, 1).toUInt(nullptr, 16));
    return -1;
}

// sscanf(line, "%x %s", &a, buf) == 2
bool scanHexAndWord(QStringView l, quint32 &a, QStringView &word)
{
    qsizetype i = 0;
    const qsizetype n = l.size();
    while (i < n && l[i].isSpace()) ++i;
    if (i + 1 < n && l[i] == u'0' && (l[i + 1] == u'x' || l[i + 1] == u'X') && i + 2 < n && hexDigit(l[i + 2]) >= 0)
        i += 2;
    const qsizetype s = i;
    quint32 v = 0;
    for (int d; i < n && (d = hexDigit(l[i])) >= 0; ++i)
        v = v * 16 + quint32(d);
    if (i == s)
        return false;
    while (i < n && l[i].isSpace()) ++i;
    const qsizetype w = i;
    while (i < n && !l[i].isSpace()) ++i;
    if (i == w)
        return false;
    a = v;
    word = l.mid(w, i - w);
    return true;
}

// sscanf(line, "%d %d", &a, &b) == 2
bool scanTwoInts(QStringView l, qint64 &a, qint64 &b)
{
    qsizetype i = 0;
    const qsizetype n = l.size();
    auto readInt = [&](qint64 &out) {
        while (i < n && l[i].isSpace()) ++i;
        const qsizetype s = i;
        if (i < n && (l[i] == u'-' || l[i] == u'+')) ++i;
        const qsizetype d = i;
        while (i < n && l[i].isDigit()) ++i;
        if (i == d)
            return false;
        out = l.mid(s, i - s).toLongLong();
        return true;
    };
    return readInt(a) && readInt(b);
}

// TryStrToInt64("0x" + word)
bool parseHex64(QStringView word, quint64 &out)
{
    if (word.isEmpty() || word.size() > 16)
        return false;
    quint64 v = 0;
    bool ascii = true;
    for (QChar c : word) {
        const char16_t u = c.unicode();
        int d;
        if (u >= u'0' && u <= u'9')
            d = u - u'0';
        else if (u >= u'a' && u <= u'f')
            d = u - u'a' + 10;
        else if (u >= u'A' && u <= u'F')
            d = u - u'A' + 10;
        else if (c.isDigit())
            d = 0, ascii = false;
        else
            return false;
        v = v * 16 + quint64(d);
    }
    if (ascii) {
        out = v;
        return true;
    }
    bool ok = false; // digits of other scripts: as toULongLong takes them
    out = word.toULongLong(&ok, 16);
    return ok;
}

// The parts of a stamp's chunk not in the document (re/stamps.md, "Скрытые части"): "R<k>", "U<k>", "H<z85>",
// "L<count>:<duration>:<hex>", "V<k>", "W<hex>".
std::optional<StampPart> parsePart(QStringView word)
{
    if (word.size() < 2)
        return std::nullopt;
    const QStringView rest = word.mid(1);
    StampPart p;
    bool ok = true;
    switch (word[0].unicode()) {
    case u'R': p.kind = StampPart::Records; p.count = rest.toInt(&ok); break;
    case u'U': p.kind = StampPart::Unstamped; p.count = rest.toInt(&ok); break;
    case u'V': p.kind = StampPart::Packets; p.count = rest.toInt(&ok); break;
    case u'H': p.kind = StampPart::Hidden; p.data = Z85::decode(rest, &ok); break;
    case u'W': p.kind = StampPart::HiddenPacket; p.data = QByteArray::fromHex(rest.toLatin1()); ok = p.data.size() == 32; break;
    case u'L': {
        const QList<QStringView> f = rest.split(u':');
        bool okCount = false, okDuration = false;
        if (f.size() != 3)
            return std::nullopt;
        p.kind = StampPart::Leaf;
        p.count = f[0].toInt(&okCount);
        p.durationUs = f[1].toULongLong(&okDuration);
        p.data = QByteArray::fromHex(f[2].toLatin1());
        ok = okCount && okDuration && p.data.size() == 32;
        break;
    }
    default:
        return std::nullopt;
    }
    if (!ok || p.count < 0)
        return std::nullopt;
    return p;
}

QString partText(const StampPart &p)
{
    switch (p.kind) {
    case StampPart::Records: return QStringLiteral("R%1").arg(p.count);
    case StampPart::Unstamped: return QStringLiteral("U%1").arg(p.count);
    case StampPart::Packets: return QStringLiteral("V%1").arg(p.count);
    case StampPart::Hidden: return QLatin1Char('H') + Z85::encode(p.data);
    case StampPart::HiddenPacket: return QLatin1Char('W') + QString::fromLatin1(p.data.toHex());
    case StampPart::Leaf:
        return QStringLiteral("L%1:%2:").arg(p.count).arg(p.durationUs) + QString::fromLatin1(p.data.toHex());
    }
    return {};
}

// The time stamps (re/stamps.md): "Stamp<i>=<end> <delay ms> <flags> <token, base64>[ <media end>]",
// "StampHidden<i>=<stamp> <previous imprint, hex, or -> <parts>" and "StampCert<i>=<certificate, base64>", in the
// order of i. Flags: 1 - voided, 2 - the chain v2 (then the media end follows).
void readStamps(const QList<QStringView> &lines, TsfDocument &doc)
{
    QMap<int, Stamp> stamps;
    QMap<int, QByteArray> certs;
    QMap<int, QStringView> hidden;
    static const QString stampKey = QStringLiteral("Stamp"), certKey = QStringLiteral("StampCert"),
                         hiddenKey = QStringLiteral("StampHidden");
    for (QStringView l : lines) {
        if (!l.startsWith(stampKey))
            continue;
        const qsizetype eq = l.indexOf(u'=');
        if (eq < 0)
            continue;
        if (l.startsWith(hiddenKey)) {
            bool ok = false;
            const int index = l.mid(hiddenKey.size(), eq - hiddenKey.size()).toInt(&ok);
            if (ok)
                hidden.insert(index, l.mid(eq + 1));
            continue;
        }
        const bool cert = l.startsWith(certKey);
        bool ok = false;
        const int index = l.mid(cert ? certKey.size() : stampKey.size(), eq - (cert ? certKey.size() : stampKey.size())).toInt(&ok);
        if (!ok)
            continue;
        const QStringView value = l.mid(eq + 1);
        if (cert) {
            certs.insert(index, QByteArray::fromBase64(value.toLatin1()));
            continue;
        }
        const QList<QStringView> parts = value.split(u' ', Qt::SkipEmptyParts);
        if (parts.size() != 4 && parts.size() != 5)
            continue;
        Stamp s;
        bool okEnd = false, okDelay = false, okFlags = false, okMedia = true;
        s.end = parts[0].toInt(&okEnd);
        s.delayMs = parts[1].toUInt(&okDelay);
        const int flags = parts[2].toInt(&okFlags);
        s.voided = flags & 1;
        s.version = flags & 2 ? 2 : 1;
        s.token = QByteArray::fromBase64(parts[3].toLatin1());
        if (s.version == 2)
            s.mediaEnd = parts.size() == 5 ? parts[4].toInt(&okMedia) : -1;
        if (okEnd && okDelay && okFlags && okMedia && s.mediaEnd >= 0 && !s.token.isEmpty())
            stamps.insert(index, s);
    }
    // The hidden parts of the stamps they name (the i of their Stamp<i> line).
    for (QStringView h : hidden) {
        const QList<QStringView> words = h.split(u' ', Qt::SkipEmptyParts);
        bool ok = false;
        const int which = words.size() >= 2 ? words[0].toInt(&ok) : 0;
        if (!ok || !stamps.contains(which))
            continue;
        Stamp &s = stamps[which];
        if (words[1] != u"-")
            s.previous = QByteArray::fromHex(words[1].toLatin1());
        for (qsizetype i = 2; i < words.size(); ++i)
            if (const std::optional<StampPart> p = parsePart(words[i]))
                s.parts.append(*p);
            else
                s.parts.append({StampPart::Leaf, -1, 0, {}}); // unreadable: the stamp will not check
    }
    doc.stamps = stamps.values();
    doc.stampCertificates = certs.values();
}

// The webcam recording (re/webcam.md): "Webcam=1 <bytes> <SHA-256>" and "WebcamData<i>=<Z85>", in the order of i.
void readWebcam(const QList<QStringView> &lines, TsfDocument &doc)
{
    static const QString key = QStringLiteral("Webcam"), dataKey = QStringLiteral("WebcamData");
    QMap<int, QStringView> chunks;
    QStringView head;
    for (QStringView l : lines) {
        if (!l.startsWith(key))
            continue;
        const qsizetype eq = l.indexOf(u'=');
        if (eq < 0)
            continue;
        if (eq == key.size()) {
            head = l.mid(eq + 1);
        } else if (l.startsWith(dataKey)) {
            bool ok = false;
            const int index = l.mid(dataKey.size(), eq - dataKey.size()).toInt(&ok);
            if (ok)
                chunks.insert(index, l.mid(eq + 1));
        }
    }
    if (head.isEmpty())
        return;
    doc.webcamDamaged = true;
    const QList<QStringView> parts = head.split(u' ', Qt::SkipEmptyParts);
    bool okSize = false;
    const qsizetype size = parts.size() == 3 && parts[0] == u"1" ? parts[1].toLongLong(&okSize) : -1;
    if (!okSize || size < 0)
        return;
    QString text;
    for (QStringView c : chunks)
        text += c;
    bool ok = false;
    QByteArray bytes = Z85::decode(text, &ok);
    if (!ok || bytes.size() < size)
        return;
    bytes.truncate(size);
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != parts[2].toLatin1())
        return;
    doc.webcam = bytes;
    doc.webcamDamaged = false;
}

TsfDocument parseLines(const QList<QStringView> &lines, Tsf::ReadError *err)
{
    const std::array<QString, KeyCount> header = values(lines);
    TsfDocument doc;
    doc.version = header[TsfVersion].toInt();
    if (err)
        *err = doc.version > Tsf::kCurrentVersion ? Tsf::ReadError::NewerVersion : Tsf::ReadError::None;

    doc.records.reserve(lines.size());
    for (QStringView l : lines) {
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
            QStringView word;
            quint64 packed;
            if (!scanHexAndWord(l, t, word) || !parseHex64(word, packed))
                continue;
            r.dtUs = t;
            r.flags = quint32(packed);
            r.ch = char16_t(packed >> 32);
        }
        const qsizetype semi = l.indexOf(u';');
        if (semi >= 0)
            r.comment = l.mid(semi + 1).toString();
        doc.records.append(r);
    }

    doc.author = header[Autor];
    doc.comment = header[Comment];
    doc.date = header[Date];
    doc.fingerZonesName = header[FingerZonesName];
    if (!doc.fingerZonesName.isEmpty())
        for (int i = 0; i < 8; ++i)
            doc.fingers.append(header[Finger0 + i]);
    doc.platform = keyPlatformFromName(header[Platform]);
    doc.attachedVideo = header[AttachedVideo];
    if (!doc.attachedVideo.isEmpty())
        doc.videoTimeShiftMs = header[VideoTimeShift].toInt();

    readStamps(lines, doc);
    readWebcam(lines, doc);

    const QString &sig = header[Signature];
    doc.signed_ = !sig.isEmpty();
    doc.signatureValid = sig == TsfSignature::compute(doc);
    return doc;
}

} // namespace

namespace Tsf {

constexpr qsizetype kWebcamLine = 65535; // characters of Z85 per line (a multiple of 5)

QString dataLine(const KeyRecord &r)
{
    // "%08X %012X": dt, then the character over the flags.
    static constexpr char16_t kHex[] = u"0123456789ABCDEF";
    const quint64 packed = (quint64(r.ch) << 32) | (r.flags & ~quint32(KeyRecord::Transient));
    QString line(21, Qt::Uninitialized);
    char16_t *p = reinterpret_cast<char16_t *>(line.data());
    for (int i = 0; i < 8; ++i)
        p[i] = kHex[(r.dtUs >> (28 - 4 * i)) & 0xF];
    p[8] = u' ';
    for (int i = 0; i < 12; ++i)
        p[9 + i] = kHex[(packed >> (44 - 4 * i)) & 0xF];
    if (!r.comment.isEmpty())
        line += QStringLiteral("\t;") + r.comment;
    return line;
}

TsfDocument parse(const QStringList &lines, ReadError *err)
{
    return parseLines(QList<QStringView>(lines.begin(), lines.end()), err);
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
    // The port's own key (the original skips unknown lines of the header; not signed).
    if (doc.platform != KeyPlatform::Windows)
        lines.append(QStringLiteral("Platform=") + keyPlatformName(doc.platform));
    // The time stamps (the port's own, re/stamps.md).
    for (int i = 0; i < doc.stamps.size(); ++i) {
        const Stamp &s = doc.stamps[i];
        const int flags = (s.voided ? 1 : 0) | (s.version >= 2 ? 2 : 0);
        QString line = QStringLiteral("Stamp%1=%2 %3 %4 ").arg(i + 1).arg(s.end).arg(s.delayMs).arg(flags)
                       + QString::fromLatin1(s.token.toBase64());
        if (s.version >= 2)
            line += QStringLiteral(" %1").arg(s.mediaEnd);
        lines.append(line);
    }
    for (int i = 0, n = 0; i < doc.stamps.size(); ++i) {
        const Stamp &s = doc.stamps[i];
        if (s.parts.isEmpty() && s.previous.isEmpty())
            continue;
        QString line = QStringLiteral("StampHidden%1=%2 ").arg(++n).arg(i + 1)
                       + (s.previous.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(s.previous.toHex()));
        for (const StampPart &p : s.parts)
            line += QLatin1Char(' ') + partText(p);
        lines.append(line);
    }
    for (int i = 0; i < doc.stampCertificates.size(); ++i)
        lines.append(QStringLiteral("StampCert%1=").arg(i + 1) + QString::fromLatin1(doc.stampCertificates[i].toBase64()));
    // The webcam (the port's own, re/webcam.md). The key must not start with a hexadecimal digit: the original
    // reads every line with sscanf("%x %s") into a buffer on its stack.
    if (!doc.webcam.isEmpty()) {
        lines.append(QStringLiteral("Webcam=1 %1 ").arg(doc.webcam.size())
                     + QString::fromLatin1(QCryptographicHash::hash(doc.webcam, QCryptographicHash::Sha256).toHex()));
        const QString text = Z85::encode(doc.webcam);
        for (qsizetype i = 0, n = 1; i < text.size(); i += kWebcamLine, ++n)
            lines.append(QStringLiteral("WebcamData%1=").arg(n) + text.mid(i, kWebcamLine));
    }
    return lines;
}

ReadError read(const QString &path, TsfDocument &doc)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return ReadError::CannotOpen;
    ReadError err;
    const QString text = Cp1251::decode(f.readAll());
    doc = parseLines(splitLines(text), &err);
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
