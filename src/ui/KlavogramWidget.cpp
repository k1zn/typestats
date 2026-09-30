#include "KlavogramWidget.h"

#include "core/KeyList.h"

#include <QPainter>

KlavogramWidget::KlavogramWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(20);
}

void KlavogramWidget::setModel(const TextModel *m)
{
    m_model = m;
    m_scrollMs = 0.0f;
    update();
}

void KlavogramWidget::scrollToPosition(int textPos)
{
    if (!m_model)
        return;
    m_scrollMs = KeyList::scrollForPosition(*m_model, textPos);
    update();
}

std::pair<double, double> KlavogramWidget::visibleSpanUs() const
{
    const float start = m_scrollMs * 1000.0f - 10.0f;
    const float end = float(double(float(width() * 1000)) / double(m_zoom) + double(start));
    return {double(start), double(end)};
}

void KlavogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(255, 255, 225));
}
