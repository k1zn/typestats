#include "TableExport.h"

#include "xlsxcellrange.h"
#include "xlsxchart.h"
#include "xlsxdocument.h"

#include <QFile>
#include <QLocale>

namespace TableExport {

namespace {

bool isNumber(const QVariant &v)
{
    return v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float || v.typeId() == QMetaType::Int;
}

QString csvCell(const QVariant &v, const QLocale &locale, QChar separator)
{
    if (!v.isValid())
        return {};
    if (v.typeId() == QMetaType::Int)
        return QString::number(v.toInt());
    if (isNumber(v)) {
        QString s = QString::number(v.toDouble(), 'f', 6);
        while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
            s.chop(1);
        return s.replace(QLatin1Char('.'), locale.decimalPoint());
    }
    QString s = v.toString();
    if (s.contains(separator) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))
        || s.contains(QLatin1Char('\r')) || s.startsWith(QLatin1Char(' ')) || s.endsWith(QLatin1Char(' ')))
        s = QLatin1Char('"') + s.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"');
    return s;
}

} // namespace

QByteArray toCsv(const Table &table, const QLocale &locale)
{
    const QChar separator = locale.decimalPoint() == QLatin1String(",") ? QLatin1Char(';') : QLatin1Char(',');
    QString out;
    QStringList cells;
    for (const QString &h : table.header)
        cells << csvCell(h, locale, separator);
    out += cells.join(separator) + QLatin1String("\r\n");
    for (const QVariantList &row : table.rows) {
        cells.clear();
        for (const QVariant &v : row)
            cells << csvCell(v, locale, separator);
        out += cells.join(separator) + QLatin1String("\r\n");
    }
    return QByteArray("\xEF\xBB\xBF") + out.toUtf8();
}

bool write(const QString &path, const Table &table, bool chart, const QLocale &locale)
{
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(toCsv(table, locale)) >= 0;
    }
    QXlsx::Document doc;
    QXlsx::Format bold;
    bold.setFontBold(true);
    for (int c = 0; c < table.header.size(); ++c)
        doc.write(1, c + 1, table.header[c], bold);
    for (int r = 0; r < table.rows.size(); ++r)
        for (int c = 0; c < table.rows[r].size(); ++c)
            if (table.rows[r][c].isValid())
                doc.write(r + 2, c + 1, table.rows[r][c]);
    if (!table.header.isEmpty())
        doc.setColumnWidth(1, int(table.header.size()), 16.0);
    if (chart && !table.rows.isEmpty() && table.header.size() > 1) {
        const int columns = int(table.header.size()), rows = int(table.rows.size());
        QXlsx::Chart *graph = doc.insertChart(1, columns + 1, QSize(720, 360));
        graph->setChartType(QXlsx::Chart::CT_LineChart);
        graph->setChartLegend(QXlsx::Chart::Right);
        for (int c = 2; c <= columns; ++c)
            graph->addSeries(QXlsx::CellRange(1, c, rows + 1, c), nullptr, true);
    }
    return doc.saveAs(path);
}

} // namespace TableExport
