#pragma once

#include "GraphWidget.h"

#include <QFrame>

class QCheckBox;
class QComboBox;

// A panel floating over the main window (Form1.Panel2, Panel9): a caption that drags it within
// the window and a red close button.
class FloatingPanel : public QFrame
{
    Q_OBJECT
public:
    FloatingPanel(const QString &title, QWidget *parent);

    // Keeps the panel inside its parent (FUN_0041967c).
    void keepInside();

signals:
    void closed();
    void captionDoubleClicked();

protected:
    static constexpr int kCaptionHeight = 17;
    int frame() const { return frameWidth(); }

    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    QRect closeRect() const;

    QString m_title;
    QPoint m_grab;
    bool m_dragging = false;
};

// "Легенда": the series of the graph; a click shows or hides one, the values are those under the
// cursor line of the graph.
class LegendPanel : public FloatingPanel
{
    Q_OBJECT
public:
    LegendPanel(GraphWidget *graph, QWidget *parent);

    bool minimized() const { return m_minimized; }
    void setMinimized(bool on);
    // The background of the theme (Look::colors).
    void updateColors();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;

private:
    int rowAt(const QPoint &pos) const;

    GraphWidget *m_graph;
    QFont m_font;
    int m_rowHeight = 0, m_charWidth = 0;
    bool m_minimized = false;
};

// "Настройка оси Y": limits of the speed and rhythm axes.
class AxisPanel : public FloatingPanel
{
    Q_OBJECT
public:
    explicit AxisPanel(QWidget *parent);

    GraphWidget::AxisSettings settings() const;
    void setSettings(const GraphWidget::AxisSettings &s);
    // The second item of every list is the limit found in the data.
    void setAutoLimits(const GraphWidget::AutoLimits &a);
    bool lockY() const;
    void setLockY(bool on);

signals:
    void settingsChanged();
    void lockYChanged(bool on);

private:
    QComboBox *limitBox(int x, int y, const QString &second);

    QComboBox *m_speedMax, *m_speedMin, *m_rhythmMax, *m_rhythmMin;
    QCheckBox *m_autoRound, *m_lockY;
};
