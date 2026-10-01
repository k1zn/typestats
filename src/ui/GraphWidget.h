#pragma once

#include "core/Graphs.h"

#include <QElapsedTimer>
#include <QWidget>

#include <array>

// The graph of the main window (Form1.PaintBox1): the Y axis on the left, the graph itself and
// the line of text under it. See re/graph_paint.md.
class GraphWidget : public QWidget
{
    Q_OBJECT
public:
    enum Series { CurSpeed, MedSpeed, ClassicSpeed, PrivSpeed, CurRhythm, MedRhythm, Arrhythmia, Pause, SeriesCount };
    enum Mode { SpeedMode, RhythmMode, HistogramMode }; // which series the grid of the Y axis follows

    // "Настройка оси Y": a limit below zero is picked from the data.
    struct AxisSettings
    {
        int speedMin = 0, speedMax = -1;
        int rhythmMin = 0, rhythmMax = 100;
        bool autoRound = true;
    };
    struct AutoLimits
    {
        int speedMin = 0, speedMax = 100, rhythmMin = 0, rhythmMax = 100;
    };
    struct ScrollParams
    {
        int min = 0, max = 0, singleStep = 1, pageStep = 1, value = 0;
    };

    explicit GraphWidget(QWidget *parent = nullptr);

    void setModel(const TextModel *m);

    static QString seriesName(int series);
    static QColor seriesColor(int series);
    bool seriesVisible(int series) const { return m_visible[series]; }
    // The two histograms exclude each other.
    void setSeriesVisible(int series, bool on);
    // The value under the cursor line; false when the cursor is hidden or outside the series.
    bool valueAtCursor(int series, float *value) const;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    const AxisSettings &axisSettings() const { return m_axis; }
    void setAxisSettings(const AxisSettings &s);
    const AutoLimits &autoLimits() const { return m_auto; }
    bool lockY() const { return m_lockY; }
    void setLockY(bool on) { m_lockY = on; }

    // Elements [from, to) shown on the klavogram; the graph scrolls to keep them in sight.
    void setKlavogramRange(int from, int to);
    int klavogramFrom() const { return m_visFrom; }
    // After the graph was moved: shifts the range above into sight; true if it had to.
    bool pullKlavogramRange();

    ScrollParams scrollParams() const;
    void setScrollValue(int value);

signals:
    // The user moved or zoomed the graph.
    void viewChanged();
    // A left click: the klavogram should start at this element.
    void elementClicked(int element);
    // Left + right buttons: the klavogram should span from klavogramFrom() to this element.
    void klavogramSpanRequested(int toElement);
    void axisMenuRequested(const QPoint &globalPos);
    // The cursor line moved or was toggled: the legend shows the values under it.
    void cursorChanged();
    void seriesVisibilityChanged();
    void autoLimitsChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    friend class TstPerf;

    struct Scale
    {
        double scale = 1.0, offset = 0.0; // units (percent of the height) = (value − offset) · scale
    };

    QRect graphRect() const;
    int stripHeight() const;
    const QVector<float> &values(int series) const;
    bool marker(int i) const { return m_fragmentStart.value(i); }
    int xOf(int i) const;
    int firstIndex() const;
    int elementAt(int x) const;
    void rescale();
    void resetView();
    void clampScroll();
    void updateRuler();
    std::array<int, 3> axisSeries() const;

    void drawGrid(QPainter &p, const QRect &g, int stripFont);
    void drawSeries(QPainter &p, const QRect &g);
    void drawRuler(QPainter &p, const QRect &g);
    void drawStrip(QPainter &p, const QRect &g, int stripFont);

    const TextModel *m_model = nullptr;
    GraphSeries m_series;
    QVector<bool> m_fragmentStart;
    std::array<Scale, SeriesCount> m_scale;
    std::array<bool, SeriesCount> m_visible{false, true, false, false, false, true, true, false};
    Mode m_mode = SpeedMode;
    AxisSettings m_axis;
    AutoLimits m_auto;
    bool m_lockY = false;

    int m_axisWidth = 33;
    std::array<int, 11> m_charWidth{}; // "1" in Courier New 8..18 pt
    float m_scrollX = 0.0f;            // in elements: x(i) = (i + scrollX) · zoomX
    float m_offsetY = 0.0f;            // in units
    float m_zoomX = 8.0f;              // px per element, 0.5..100
    float m_zoomY = 1.0f;              // px per unit, 0.4..50
    int m_visFrom = 0, m_visTo = 0;

    bool m_cursorHidden = true;
    QPoint m_mouse; // relative to the graph
    float m_rulerY = -1e9f;
    bool m_hasRuler = false, m_rulerFollows = false;
    QRect m_rulerBox;
    QElapsedTimer m_rightClick;
};
