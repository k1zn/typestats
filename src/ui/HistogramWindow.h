#pragma once

#include "core/Histograms.h"

#include <QWidget>

class QFrame;
class QLabel;
class QToolButton;

// The bars of one page (Form4.PaintBox1), drawn as re/histograms.md describes: an axis on the left,
// a line of labels below, the mouse drags and zooms as on the main graph.
class HistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    // Shows another page, zoomed so that all the bars fit.
    void setBars(const QVector<float> &values, const QStringList &labels);
    // The bar at widget coordinate x, or -1.
    int barAt(int x) const;

signals:
    void barDoubleClicked(int index);
    // A mouse button is held over a bar (-1: over none, or the button was released).
    void barHeld(int index, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    friend class TstUi;

    QRect plotRect() const;
    int firstIndex() const;
    void clampScroll();

    QVector<float> m_values;
    QStringList m_labels;
    int m_axisWidth = 33;
    int m_stripHeight = 27;
    int m_charWidth[4] = {};
    float m_scrollX = 0.0f, m_offsetY = 0.0f;
    float m_zoomX = 8.0f, m_zoomY = 1.0f;
    float m_minZoomY = 0.4f;
    QPoint m_mouse;
};

// "Статистические гистограммы" (Form4): pages of bars with a drill-down by double click.
class HistogramWindow : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWindow(QWidget *parent = nullptr);

    // The part of the recording to analyse; the window goes back to its root page.
    void setSource(const Histograms::Source &source);
    // The list of the extra statistics window, for the page made of it.
    void setExtraRows(const QVector<ExtraStats::Row> &rows);
    // Another root page: all keys, all fingers or the extra statistics.
    void setRoot(Histograms::Node::Kind kind);

signals:
    // The window was shown and needs its source.
    void shown();
    // A bar of a single press was double-clicked: the klavogram goes to the element.
    void elementSelected(int element);
    // A bar of the extra statistics page was double-clicked.
    void extraRowSelected(int row);
    // The page of the extra statistics was asked for: it needs the rows of that window.
    void extraRequested();

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    friend class TstUi;

    void showPage();
    void back();
    void drill(int index);
    void showHint(int index, const QPoint &globalPos);

    Histograms::Source m_source;
    QVector<ExtraStats::Row> m_extraRows;
    QVector<Histograms::Node> m_stack; // the last one is shown
    Histograms::Page m_page;

    QFrame *m_toolBar;
    QToolButton *m_onTop;
    QLabel *m_title;
    HistogramWidget *m_chart;
};
