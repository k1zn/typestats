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
    QCryptographicHash md5(QCryptographicHash::Md5);
    for (const KeyRecord &r : doc.records) {
        quint32 le[3] = {qToLittleEndian(r.dtUs),
                         qToLittleEndian(r.flags & ~quint32(KeyRecord::Transient)),
                         qToLittleEndian(quint32(r.ch))};
        md5.addData(QByteArrayView(reinterpret_cast<const char *>(le), sizeof le));
    }
    md5.addData(Cp1251::encode(doc.author));
    md5.addData(Cp1251::encode(doc.date));
    md5.addData(QByteArrayView("TypingStatistics"));
    return QString::fromLatin1(md5.result().toHex());
}
