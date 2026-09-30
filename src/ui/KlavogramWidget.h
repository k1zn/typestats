#pragma once

#include "core/Recalc.h"

#include <QWidget>

// The klavogram (Form1.PaintBox3). For now it only keeps the view state (scroll position and zoom)
// the key list depends on; the drawing itself is the next UI step (needs the RE of PaintBox3Paint).
class KlavogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KlavogramWidget(QWidget *parent = nullptr);

    void setModel(const TextModel *m);
    // Puts the element at a text position to the left edge (FUN_00414500).
    void scrollToPosition(int textPos);

    // Drawing time visible on the widget, µs. The original computes it in single precision
    // (FUN_00437d98): [scroll − 10 µs, scroll − 10 µs + width / zoom]; the key list shows the presses in it.
    std::pair<double, double> visibleSpanUs() const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    const TextModel *m_model = nullptr;
    float m_scrollMs = 0.0f;
    float m_zoom = 0.25f; // px per ms, 0.04..300
};
