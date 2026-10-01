#include "TsfSignature.h"

#include "Cp1251.h"
#include "TsfFile.h"

#include <QCryptographicHash>
#include <QtEndian>

// MD5 over (see FUN_00415480 / FUN_004160ac):
//   for each record: dt (u32 LE), flags & ~0x100 (u32 LE), ch (u32 LE)
//   author (cp1251 bytes), date (cp1251 bytes), "TypingStatistics"
// rendered as 32 lowercase hex digits. The description ("comment") is not covered.
QString TsfSignature::compute(const TsfDocument &doc)
{
    QByteArray data(doc.records.size() * 12, Qt::Uninitialized);
    uchar *p = reinterpret_cast<uchar *>(data.data());
    for (const KeyRecord &r : doc.records) {
        qToLittleEndian<quint32>(r.dtUs, p);
        qToLittleEndian<quint32>(r.flags & ~quint32(KeyRecord::Transient), p + 4);
        qToLittleEndian<quint32>(r.ch, p + 8);
        p += 12;
    }
    QCryptographicHash md5(QCryptographicHash::Md5);
    md5.addData(data);
    md5.addData(Cp1251::encode(doc.author));
    md5.addData(Cp1251::encode(doc.date));
    md5.addData(QByteArrayView("TypingStatistics"));
    return QString::fromLatin1(md5.result().toHex());
}
