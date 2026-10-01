#pragma once

#include "core/FingerZones.h"
#include "core/Recalc.h"

#include <QWidget>

// The klavogram (Form1.PaintBox3): nine tracks, one per finger, with the keys held over time.
// See re/klavogram.md.
class KlavogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KlavogramWidget(QWidget *parent = nullptr);

    void setModel(const TextModel *m);
    void setZones(const FingerZones &zones);
    void setFontSize(int points);
    // Puts the element at a text position to the left edge (FUN_00414500).
    void scrollToPosition(int textPos);
    // Set by the graph; viewChanged() is not emitted.
    void setScrollMs(float ms);
    void setZoom(float pxPerMs);
    float scrollMs() const { return m_scrollMs; }
    float zoom() const { return m_zoom; }

    // Drawing time visible on the widget, µs. The original computes it in single precision
    // (FUN_00437d98): [scroll − 10 µs, scroll − 10 µs + width / zoom]; the key list shows the presses in it.
    std::pair<double, double> visibleSpanUs() const;
    // Klavogram records [first, last) under the widget (FUN_00424e64).
    std::pair<int, int> visibleRecords() const;

signals:
    // Scroll position or zoom changed.
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    friend class TstPerf;

    int firstRecordAt(long double drawUs) const;
    qint64 absoluteTime(qint64 drawUs) const;
    float measuredSpeed(qint64 fromT, qint64 toT) const;
    void clampScroll();
    void drawRuler(QPainter &p, int top, int rowHeight);
    void drawKeys(QPainter &p, int top, int rowHeight);

    const TextModel *m_model = nullptr;
    FingerZones m_zones = FingerZones::standard();
    QFont m_font;
    float m_scrollMs = 0.0f;
    float m_zoom = 0.25f; // px per ms, 0.04..300

    // The measuring cursor (toggled by a double click; left + right buttons measure).
    bool m_cursorMode = false;
    bool m_selecting = false;
    bool m_selectionPending = false;
    int m_cursorX = -1000;
    int m_selectionX = 0;
    qint64 m_cursorT = 0, m_selectionT = 0; // absolute time, µs
    int m_lastMouseX = 0;
};
