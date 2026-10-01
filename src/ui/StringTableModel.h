#pragma once

#include <QAbstractTableModel>
#include <QStringList>

#include <functional>

// Rows of strings for a report-style list. The lists of Form3 hold up to hundreds of thousands of
// rows: their strings are made when the view shows them.
class StringTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;
    using Cell = std::function<QString(int row, int column)>;

    void setTable(const QStringList &headers, const QVector<QStringList> &rows)
    {
        beginResetModel();
        m_headers = headers;
        m_rows = rows;
        m_count = int(rows.size());
        m_cell = nullptr;
        endResetModel();
    }
    void setTable(const QStringList &headers, int rows, Cell cell)
    {
        beginResetModel();
        m_headers = headers;
        m_rows.clear();
        m_count = rows;
        m_cell = std::move(cell);
        endResetModel();
    }
    void setHeaders(const QStringList &headers)
    {
        m_headers = headers;
        emit headerDataChanged(Qt::Horizontal, 0, int(headers.size()) - 1);
    }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_count; }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_headers.size()); }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !index.isValid())
            return {};
        if (m_cell)
            return index.column() < m_headers.size() ? m_cell(index.row(), index.column()) : QString();
        return m_rows[index.row()].value(index.column());
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return {};
        return m_headers.value(section);
    }

private:
    QStringList m_headers;
    QVector<QStringList> m_rows;
    int m_count = 0;
    Cell m_cell;
};
