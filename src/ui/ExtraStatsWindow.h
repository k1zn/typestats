#pragma once

#include "core/ExtraStats.h"

#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QFrame;
class QGroupBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSplitter;
class QTableView;
class QToolButton;
class StringTableModel;

// "Дополнительная статистика" (Form3): n-grams, words, sentences and templates of the part of the
// recording the main window's statistics are about, with their speeds. See re/extra_stats.md.
class ExtraStatsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ExtraStatsWindow(QWidget *parent = nullptr);

    // The elements [b, e) of the model to analyse; fingers is fingerSeries() of the model.
    void setSource(const TextModel *model, const QVector<quint8> &fingers, int b, int e);

    // The rows of the list, in the order shown.
    const QVector<ExtraStats::Row> &rows() const { return m_rows; }
    // The rows are averages with a count.
    bool averages() const;
    // Selects a row, as clicking it does.
    void selectRow(int row);

    void loadSettings();
    void saveSettings() const;

signals:
    // The window was shown and needs its source.
    void shown();
    void rowsChanged();
    // An occurrence was selected: the klavogram goes to its first element.
    void elementSelected(int element);
    void exportRequested();

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    friend class TstUi;
    friend class TstPerf;

    ExtraStats::Kind kind() const;
    ExtraStats::CharFilter filter() const;
    void compute();    // unless locked
    void computeNow();
    void showRows();
    void rowSelected(int row);
    void occurrenceSelected(int row);
    void averagesToggled(bool on);
    void copy();
    void save();
    void layoutControls();

    const TextModel *m_model = nullptr;
    QVector<quint8> m_fingers;
    int m_b = 0, m_e = 0;
    bool m_placed = false; // the window was shown and has a position
    bool m_dirty = false; // the source changed while the window was locked

    // What collect() was last run on: it is not run again for the same.
    struct Collected
    {
        const TextModel *model = nullptr;
        quint64 serial = 0;
        QVector<quint8> fingers;
        int b = 0, e = 0;
        ExtraStats::Kind kind = ExtraStats::Words;
        QString pattern;
        ExtraStats::CharFilter filter;
        bool operator==(const Collected &) const = default;
    };
    std::optional<Collected> m_collected;
    int m_occVersion = 0; // changes with m_occ
    // What m_rows were made of.
    struct Sorted
    {
        int occVersion = -1;
        bool averages = false;
        int mode = 0;
        bool descending = false;
        bool operator==(const Sorted &) const = default;
    };
    Sorted m_sorted;
    // The previous result, kept for the next source: a selection and a click that drops it alternate
    // between two sources (a selection and the whole text), and the whole text is not collected anew.
    struct Stash
    {
        Collected key;
        int occVersion = 0;
        QVector<ExtraStats::Occurrence> occ;
        QVector<ExtraStats::Row> rows;
        Sorted sorted;
    };
    std::optional<Stash> m_stash;
    int m_versions = 0; // the last occVersion given

    QVector<ExtraStats::Occurrence> m_occ;   // everything found
    QVector<ExtraStats::Row> m_rows;         // the upper list
    QVector<ExtraStats::Occurrence> m_lower; // the lower list: occurrences of the selected row
    ExtraStats::Sort m_sort;
    ExtraStats::TemplateList m_templates;

    QFrame *m_toolBar;
    QToolButton *m_onTop;
    QCheckBox *m_averages, *m_lock;
    QGroupBox *m_kindBox, *m_filterBox;
    QRadioButton *m_kinds[ExtraStats::KindCount];
    QWidget *m_templatePanel;
    QComboBox *m_template;
    QToolButton *m_templateButton;
    QCheckBox *m_filterOn[3];
    QLineEdit *m_filterText[3];
    QSplitter *m_split;
    QTableView *m_list, *m_lowerList;
    StringTableModel *m_listModel, *m_lowerModel;
    QLabel *m_status;
};
