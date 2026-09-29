#pragma once

#include "KeyRecord.h"

#include <QStringList>

// Contents of a .tsf file (see re/tsf_format.md).
struct TsfDocument
{
    KeyRecords records;
    int version = 1;
    QString author;          // "autor"
    QString comment;
    QString date;
    QString fingerZonesName; // empty = not stored
    QStringList fingers;     // Finger0..Finger7
    QString attachedVideo;
    int videoTimeShiftMs = 0;
    bool signed_ = false;     // file had a signature line
    bool signatureValid = true;
};

namespace Tsf {
constexpr int kCurrentVersion = 1;

enum class ReadError { None, CannotOpen, NewerVersion };

// Parses a .tsf file. NewerVersion is reported but the data is still loaded,
// as the original does after its warning.
ReadError read(const QString &path, TsfDocument &doc);
bool write(const QString &path, const TsfDocument &doc, bool sign);

// Text-level helpers, exposed for tests.
TsfDocument parse(const QStringList &lines, ReadError *err = nullptr);
QStringList serialize(const TsfDocument &doc, bool sign);
QString dataLine(const KeyRecord &r);
}
