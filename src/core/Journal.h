#pragma once

#include "KeyRecord.h"

#include <QDate>
#include <QFile>

// The journal (.tsj): every captured event, appended to one file per month. See re/journal.md.

namespace Journal {
// "<year>_<month>.tsj", the month without a leading zero.
QString fileName(const QDate &date);
bool isJournal(const QString &path);

// One record: dt ^ 0x554973, flags, character (3 × uint32 LE), comment length (1 byte), comment (cp1251).
QByteArray encode(const KeyRecord &r);
// All complete records; an unfinished one at the end is dropped.
KeyRecords decode(const QByteArray &bytes);

bool read(const QString &path, KeyRecords &recs);
}

// Appends events to the journal of the month they happen in.
class JournalWriter
{
public:
    explicit JournalWriter(const QString &dir) : m_dir(dir) {}

    bool append(const KeyRecord &r, const QDate &date = QDate::currentDate());
    // Path of the journal of a month.
    QString path(const QDate &date = QDate::currentDate()) const;
    // Lets the file go (before it is read or replaced); the next append opens it again.
    void close() { m_file.close(); }

private:
    QString m_dir;
    QFile m_file;
};
