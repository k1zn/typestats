#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

// "Экспортировать в Excel": the original hands a table to Excel over OLE (Dexcel::ExcelGraph) and, for
// the key list, adds a chart of its numeric columns. The port writes a file instead: .xlsx or .csv.
namespace TableExport {

struct Table
{
    QStringList header;
    QVector<QVariantList> rows; // numbers as double or int, texts as QString, empty cells as invalid
};

// CSV for the spreadsheet of this locale: UTF-8 with a signature, ";" between the cells where the
// decimal separator is a comma.
QByteArray toCsv(const Table &table, const QLocale &locale);

// Writes by the suffix of the path: .csv or, for anything else, .xlsx (with a line chart of the columns
// after the first one if asked). Returns false if the file cannot be written.
bool write(const QString &path, const Table &table, bool chart, const QLocale &locale);

}
