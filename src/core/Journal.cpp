#include "Journal.h"

#include "Cp1251.h"

#include <QDir>
#include <QtEndian>

namespace {

constexpr quint32 kDtMask = 0x554973;
constexpr qsizetype kFixedSize = 13; // dt, flags, character, comment length

} // namespace

namespace Journal {

QString fileName(const QDate &date)
{
    return QStringLiteral("%1_%2.tsj").arg(date.year()).arg(date.month());
}

bool isJournal(const QString &path)
{
    return path.endsWith(QLatin1String(".tsj"), Qt::CaseInsensitive);
}

QByteArray encode(const KeyRecord &r)
{
    const QByteArray comment = Cp1251::encode(r.comment).left(255);
    QByteArray out(kFixedSize, Qt::Uninitialized);
    uchar *p = reinterpret_cast<uchar *>(out.data());
    qToLittleEndian<quint32>(r.dtUs ^ kDtMask, p);
    qToLittleEndian<quint32>(r.flags, p + 4);
    qToLittleEndian<quint32>(r.ch, p + 8);
    p[12] = uchar(comment.size());
    return out + comment;
}

KeyRecords decode(const QByteArray &bytes)
{
    KeyRecords recs;
    const uchar *p = reinterpret_cast<const uchar *>(bytes.constData());
    const uchar *const end = p + bytes.size();
    while (end - p >= kFixedSize) {
        const int n = p[12];
        if (end - p < kFixedSize + n)
            break;
        KeyRecord r;
        r.dtUs = qFromLittleEndian<quint32>(p) ^ kDtMask;
        r.flags = qFromLittleEndian<quint32>(p + 4);
        r.ch = char16_t(qFromLittleEndian<quint32>(p + 8));
        if (n != 0)
            r.comment = Cp1251::decode(QByteArray(reinterpret_cast<const char *>(p) + kFixedSize, n));
        recs.append(r);
        p += kFixedSize + n;
    }
    return recs;
}

bool read(const QString &path, KeyRecords &recs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    recs = decode(f.readAll());
    return true;
}

} // namespace Journal

QString JournalWriter::path(const QDate &date) const
{
    return QDir(m_dir).filePath(Journal::fileName(date));
}

bool JournalWriter::append(const KeyRecord &r, const QDate &date)
{
    const QString file = path(date);
    if (m_file.fileName() != file || !m_file.isOpen()) {
        m_file.close();
        m_file.setFileName(file);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
            return false;
    }
    const QByteArray bytes = Journal::encode(r);
    return m_file.write(bytes) == bytes.size() && m_file.flush();
}
