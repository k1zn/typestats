#pragma once

#include <QAbstractTableModel>
#include <QStringList>

// Rows of ready-made strings for a report-style list (the lists of Form3 hold thousands of rows).
class StringTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;

    void setTable(const QStringList &headers, const QVector<QStringList> &rows)
    {
        beginResetModel();
        m_headers = headers;
        m_rows = rows;
        endResetModel();
    }
    void setHeaders(const QStringList &headers)
    {
        m_headers = headers;
        emit headerDataChanged(Qt::Horizontal, 0, int(headers.size()) - 1);
    }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_headers.size()); }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !index.isValid())
            return {};
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
};
