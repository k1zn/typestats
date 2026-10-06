#include "GraphWidget.h"

#include "Look.h"
#include "core/NumberFormat.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace {

const QString kStripFont = QStringLiteral("Courier New");

constexpr int kGridSteps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
constexpr float kMinZoomX = 0.5f, kMaxZoomX = 100.0f, kMinZoomY = 0.4f, kMaxZoomY = 50.0f;

int roundAway(float v)
{
    return int(v < 0.0f ? v - 0.5f : v + 0.5f);
}

// A vertical line like GDI's MoveTo/LineTo: the end point is not drawn.
void verticalLine(QPainter &p, int x, int from, int to)
{
    if (from != to)
        p.drawLine(x, from, x, to > from ? to - 1 : to + 1);
}

// Keys with long names get the symbols the klavogram uses for them.
QString stripLabel(const QString &name)
{
    if (name.size() == 1 && name != QLatin1String("\r"))
        return name;
    if (name == QLatin1String("[LShift]") || name == QLatin1String("[RShift]"))
        return QString(QChar(0x21D1));
    if (name == QLatin1String("[BackSpace]"))
        return QString(QChar(0x2190));
    if (name == QLatin1String("[Ctrl+BackSpace]"))
        return QString(QChar(0x21D0));
    if (name == QLatin1String("\r"))
        return QString(QChar(0x21B5));
    return QString(QChar(0x00B7));
}

// FUN_00439c5c: limits of an axis; the ones below zero are taken from the data.
void axisLimits(const QVector<float> &values, const QVector<bool> &skip, int min, int max, bool round,
                int *autoMin, int *autoMax, double *scale, double *offset)
{
    float lo = 2147483648.0f, hi = 0.0f;
    bool any = false;
    for (int i = 0; i < values.size(); ++i) {
        if (skip.value(i) || values[i] < -0.1f)
            continue;
        lo = std::min(lo, values[i]);
        hi = std::max(hi, values[i]);
        any = true;
    }
    if (!any) {
        *autoMin = std::max(min, 0);
        *autoMax = max < 0 ? 100 : max;
        return;
    }
    hi = std::min(hi, 1200.0f);
    *autoMin = int(lo);
    *autoMax = int(std::ceil(hi));
    if (round) {
        const int step = hi > 100.0f ? 100 : 10;
        *autoMax = ((*autoMax - 1) / step + 1) * step;
        *autoMin = (*autoMin / step) * step;
    }
    if (min < 0)
        min = *autoMin;
    if (max < 0)
        max = *autoMax;
    *offset = min;
    *scale = max > min ? 100.0 / (max - min) : 100.0;
}

} // namespace

GraphWidget::GraphWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(20);
    setMouseTracking(true); // the cursor line and the ruler follow the mouse
    setContextMenuPolicy(Qt::PreventContextMenu);
    m_axisWidth = int(float(fontMetrics().horizontalAdvance(QLatin1Char('0'))) * 5.5f);
    for (int k = 0; k < int(m_charWidth.size()); ++k)
        m_charWidth[k] = QFontMetrics(Look::pointFont(kStripFont, k + 8)).horizontalAdvance(QLatin1Char('1'));
    m_scale[Arrhythmia] = {0.25, -100.0};
}

QString GraphWidget::seriesName(int series)
{
    switch (series) {
    case CurSpeed: return tr("Мгновенная скорость");
    case MedSpeed: return tr("Средняя скорость");
    case ClassicSpeed: return tr("Классическая скорость");
    case PrivSpeed: return tr("Приведённая скорость");
    case CurRhythm: return tr("Мгновенная ритмичность");
    case MedRhythm: return tr("Средняя ритмичность");
    case Arrhythmia: return tr("Гистограмма аритмии");
    case Pause: return tr("Гистограмма длительностей");
    }
    return {};
}

QColor GraphWidget::seriesColor(int series)
{
    return Look::colors().series[series];
}

const QVector<float> &GraphWidget::values(int series) const
{
    switch (series) {
    case CurSpeed: return m_series.curSpeed;
    case MedSpeed: return m_series.medSpeed;
    case ClassicSpeed: return m_series.classicSpeed;
    case PrivSpeed: return m_series.privSpeed;
    case CurRhythm: return m_series.curRhythm;
    case MedRhythm: return m_series.medRhythm;
    case Arrhythmia: return m_series.arrhythmia;
    default: return m_series.pause;
    }
}

void GraphWidget::setModel(const TextModel *m)
{
    m_model = m;
    m_series = m ? Graphs::compute(*m) : GraphSeries();
    m_fragmentStart.fill(false, m ? m->size() : 0);
    if (m)
        for (int start : m->fragmentStarts)
            if (start < m_fragmentStart.size())
                m_fragmentStart[start] = true;
    m_visFrom = m_visTo = 0;
    m_scrollX = 0.0f;
    m_offsetY = 0.0f;
    rescale();
    update();
}

void GraphWidget::rescale()
{
    const AutoLimits before = m_auto;
    axisLimits(m_series.medSpeed, m_fragmentStart, m_axis.speedMin, m_axis.speedMax, m_axis.autoRound,
               &m_auto.speedMin, &m_auto.speedMax, &m_scale[MedSpeed].scale, &m_scale[MedSpeed].offset);
    m_scale[CurSpeed] = m_scale[ClassicSpeed] = m_scale[PrivSpeed] = m_scale[MedSpeed];
    axisLimits(m_series.medRhythm, m_fragmentStart, m_axis.rhythmMin, m_axis.rhythmMax, m_axis.autoRound,
               &m_auto.rhythmMin, &m_auto.rhythmMax, &m_scale[MedRhythm].scale, &m_scale[MedRhythm].offset);
    m_scale[CurRhythm] = m_scale[MedRhythm];

    // Key durations: 40 units for the mean pause rounded up to hundreds (FUN_004398a8).
    if (!m_series.pause.isEmpty()) {
        float sum = 0.0f;
        int n = 0;
        for (int i = 0; i < m_series.pause.size(); ++i)
            if (!marker(i)) {
                sum += m_series.pause[i];
                ++n;
            }
        const float mean = n ? sum / float(n) : 0.0f;
        m_scale[Pause].scale = double(40.0f / std::max(float((int(mean) / 100 + 1) * 100), 0.001f));
    }
    if (m_series.medSpeed.size() > 1)
        resetView();
    if (before.speedMin != m_auto.speedMin || before.speedMax != m_auto.speedMax
        || before.rhythmMin != m_auto.rhythmMin || before.rhythmMax != m_auto.rhythmMax)
        emit autoLimitsChanged();
}

void GraphWidget::resetView()
{
    m_zoomY = std::clamp(0.01f * float(graphRect().height()), kMinZoomY, kMaxZoomY);
    m_scrollX = 1.0f;
    m_offsetY = 0.0f;
}

void GraphWidget::setSeriesVisible(int series, bool on)
{
    m_visible[series] = on;
    if (on && series == Arrhythmia)
        m_visible[Pause] = false;
    if (on && series == Pause)
        m_visible[Arrhythmia] = false;
    emit seriesVisibilityChanged();
    update();
}

void GraphWidget::setMode(Mode mode)
{
    m_mode = mode;
    update();
}

void GraphWidget::setAxisSettings(const AxisSettings &s)
{
    m_axis = s;
    rescale();
    update();
}

int GraphWidget::stripHeight() const
{
    return QFontMetrics(Look::pointFont(kStripFont, 18)).height();
}

QRect GraphWidget::graphRect() const
{
    return QRect(m_axisWidth, 0, std::max(1, width() - m_axisWidth), std::max(1, height() - stripHeight()));
}

int GraphWidget::xOf(int i) const
{
    return int(std::floor((float(i) + m_scrollX) * m_zoomX + 0.5f));
}

int GraphWidget::firstIndex() const
{
    return std::max(0, int(std::floor(-m_scrollX)));
}

int GraphWidget::elementAt(int x) const
{
    const float v = float(x) / m_zoomX - m_scrollX + 0.5f;
    return v < 0.5f ? -1 : int(v);
}

bool GraphWidget::valueAtCursor(int series, float *value) const
{
    if (m_cursorHidden)
        return false;
    const int e = elementAt(m_mouse.x());
    const QVector<float> &v = values(series);
    if (e < 0 || e >= v.size() || marker(e))
        return false;
    *value = v[e];
    return true;
}

std::array<int, 3> GraphWidget::axisSeries() const
{
    return {MedSpeed, MedRhythm, m_visible[Pause] ? Pause : Arrhythmia};
}

void GraphWidget::clampScroll()
{
    const int quarter = graphRect().width() / 4;
    const int n = m_model ? m_model->size() : 0;
    const float lo = std::min(1.0f, float(quarter) / m_zoomX - float(n));
    const float hi = float(quarter) * 3.0f / m_zoomX;
    m_scrollX = std::min(std::max(m_scrollX, lo), hi);
}

GraphWidget::ScrollParams GraphWidget::scrollParams() const
{
    const int w = graphRect().width();
    const int quarter = w / 4;
    const int n = m_model ? m_model->size() : 0;
    ScrollParams s;
    s.pageStep = std::max(1, int(float(w) / m_zoomX));
    s.singleStep = std::max(1, s.pageStep / 20);
    s.min = int(-(3.0f * float(quarter) / m_zoomX)) + 1;
    s.max = std::max(0, int(-(float(quarter) / m_zoomX - float(n))));
    s.value = int(1.0f - m_scrollX);
    return s;
}

void GraphWidget::setScrollValue(int value)
{
    m_scrollX = float(1 - value);
    update();
}

void GraphWidget::setKlavogramRange(int from, int to)
{
    m_visFrom = from;
    m_visTo = to;
    // FUN_0043a918, the klavogram leads: the graph follows when the range leaves it.
    const float w = float(graphRect().width());
    const int right = int(-m_scrollX + w / m_zoomX);
    int delta = 0, lo = m_visFrom;
    if (right < m_visTo) {
        delta = right - m_visTo;
        lo += delta;
    }
    if (-m_scrollX >= float(lo))
        delta = int(-m_scrollX - float(m_visFrom));
    if (delta < 0)
        m_scrollX = w / m_zoomX - float(m_visTo);
    else if (delta > 0)
        m_scrollX = float(-m_visFrom);
    update();
}

bool GraphWidget::pullKlavogramRange()
{
    // FUN_0043a918, the graph leads: the range is dragged back into sight.
    const int right = int(-m_scrollX + float(graphRect().width()) / m_zoomX);
    int delta = 0, lo = m_visFrom;
    if (right < m_visTo) {
        delta = right - m_visTo;
        lo += delta;
    }
    if (-m_scrollX >= float(lo))
        delta = int(-m_scrollX - float(m_visFrom));
    if (delta == 0)
        return false;
    m_visFrom = std::max(0, m_visFrom + delta);
    m_visTo += delta;
    return true;
}

void GraphWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    const QSize was = e->oldSize();
    const QRect g = graphRect();
    if (!was.isValid()) {
        m_zoomY = std::clamp(0.009f * float(g.height()), kMinZoomY, kMaxZoomY);
        return;
    }
    const int oldW = std::max(1, was.width() - m_axisWidth), oldH = std::max(1, was.height() - stripHeight());
    m_zoomX = std::clamp(float(g.width()) * m_zoomX / float(oldW), kMinZoomX, kMaxZoomX);
    m_zoomY = std::clamp(float(g.height()) * m_zoomY / float(oldH), kMinZoomY, kMaxZoomY);
}

void GraphWidget::updateRuler()
{
    if (m_rulerFollows)
        m_rulerY = float(graphRect().height() - 1 - m_mouse.y()) / m_zoomY - m_offsetY;
}

void GraphWidget::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const QRect g = graphRect();
    if (pos.x() < m_axisWidth) {
        if (e->button() == Qt::RightButton)
            emit axisMenuRequested(e->globalPosition().toPoint());
        return;
    }
    if (!g.contains(pos))
        return;
    m_mouse = pos - g.topLeft();
    if (e->button() == Qt::RightButton) {
        if (m_hasRuler && m_rulerBox.contains(m_mouse)) {
            m_hasRuler = m_rulerFollows = false;
        } else if (m_rightClick.isValid() && m_rightClick.elapsed() < 500) {
            // The second right click in a row: a ruler that follows the mouse until the next one.
            m_hasRuler = m_rulerFollows = true;
            updateRuler();
        } else {
            m_rightClick.start();
            m_rulerFollows = false;
        }
    } else if (e->button() == Qt::LeftButton) {
        emit elementClicked(elementAt(m_mouse.x()));
    }
    update();
}

void GraphWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        mousePressEvent(e);
        return;
    }
    if (!graphRect().contains(e->position().toPoint()))
        return;
    m_cursorHidden = !m_cursorHidden;
    emit cursorChanged();
    update();
}

void GraphWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QRect g = graphRect();
    const QPoint pos = e->position().toPoint() - g.topLeft();
    const int dx = pos.x() - m_mouse.x(), dy = pos.y() - m_mouse.y();
    const bool left = e->buttons() & Qt::LeftButton, right = e->buttons() & Qt::RightButton;
    const bool middle = e->buttons() & Qt::MiddleButton;
    const bool inside = pos.x() >= 0 && pos.y() >= 0 && pos.x() < g.width() && pos.y() < g.height();
    if (!inside && !left && !right && !middle)
        return;
    m_mouse = pos;

    if (left && right)
        emit klavogramSpanRequested(elementAt(pos.x()));
    updateRuler();
    if (right && !left) {
        const float oldZoom = m_zoomX, oldScroll = m_scrollX;
        m_zoomX = std::clamp(float(dx) * m_zoomX / 100.0f + m_zoomX, kMinZoomX, kMaxZoomX);
        if (!m_lockY)
            m_zoomY = std::clamp(m_zoomY - float(dy) * m_zoomY / 100.0f, kMinZoomY, kMaxZoomY);
        // The start of the part shown on the klavogram stays where it is.
        m_scrollX = (float(m_visFrom) + oldScroll - 1.0f) * oldZoom / m_zoomX + 1.0f - float(m_visFrom);
    }
    if (left && !right) {
        m_scrollX += float(dx) / m_zoomX;
        if (!m_lockY)
            m_offsetY -= float(dy) / m_zoomY;
    }
    if (middle && !(left && right))
        m_scrollX += float(dx * 50) / m_zoomX;
    clampScroll();

    if (left || right || middle)
        emit viewChanged();
    if (!m_cursorHidden)
        emit cursorChanged();
    update();
}

void GraphWidget::drawGrid(QPainter &p, const QRect &g, int stripFont)
{
    const int w = g.width(), h = g.height();
    const QFontMetrics fm(font());
    const int cy = fm.height() / 2;
    QPen dotted(Look::colors().grid, 1);
    dotted.setDashPattern({3, 3}); // PS_DOT of GDI
    const QString point = QLocale().decimalPoint(); // once per frame: for the system locale it is a call to the OS

    // Vertical lines at the spaces of the text.
    if (stripFont > 0 && m_model) {
        p.setPen(dotted);
        for (int i = firstIndex(); i < m_model->size(); ++i) {
            if (m_model->names[i] != QLatin1String(" "))
                continue;
            const int x = int(std::floor((float(i) + m_scrollX + 0.5f) * m_zoomX + 0.5f));
            if (x >= w)
                break;
            p.drawLine(x, 0, x, h - 1);
        }
    }

    // Horizontal lines at round values of the leading series; every line is labelled for the speed,
    // the rhythm and the histogram.
    const std::array<int, 3> axis = axisSeries();
    const Scale &lead = m_scale[axis[m_mode]];
    const float threshold = float(cy * 7) / m_zoomY;
    int k = 0;
    while (k < 8 && !(double(kGridSteps[k]) * lead.scale > double(threshold)))
        ++k;
    const int step = kGridSteps[k];
    const float stepUnits = float(double(step) * lead.scale);
    float a = float(roundAway(-m_offsetY / stepUnits)) * stepUnits;
    const int v = roundAway(float(double(a) / lead.scale + lead.offset));
    int d = (v / step) * step - v;
    if (v % step != 0)
        d += v > 0 ? step : -step;
    a = float(lead.scale * double(d) + double(a));
    const float top = float(h) / m_zoomY - m_offsetY;

    int decimals[3] = {0, 0, 0};
    for (int idx = 0; idx < 3; ++idx) {
        const float r = float(double(stepUnits) / m_scale[axis[idx]].scale);
        if (std::fabs(r) < 0.21f)
            decimals[idx] = 2;
        else if (std::fabs(r) < 4.0f && std::fabs(r - float(roundAway(r))) > 0.1f)
            decimals[idx] = 1;
    }

    for (; a < top; a += stepUnits) {
        const float py = (m_offsetY + a) * m_zoomY;
        if (!(py > -2.0f))
            continue;
        if (py > 0.1f) {
            const int y = int(float(h) - py);
            p.setClipRect(QRect(0, 0, w, h));
            p.setPen(dotted);
            p.drawLine(0, y, w - 1, y);
        }
        p.setClipRect(QRect(-m_axisWidth + 1, 0, m_axisWidth - 2, height()));
        for (int idx = 0; idx < 3; ++idx) {
            const Scale &s = m_scale[axis[idx]];
            const QString label = formatFixed(double(a) / s.scale + s.offset, decimals[idx], point);
            const int y = int(float(h) - py - float((3 - 2 * idx) * cy));
            p.setPen(seriesColor(axis[idx]));
            p.drawText(-fm.horizontalAdvance(label) - 3, y + fm.ascent(), label);
        }
    }
    p.setClipRect(QRect(0, 0, w, h));
}

void GraphWidget::drawSeries(QPainter &p, const QRect &g)
{
    const int w = g.width(), h = g.height();
    const int first = firstIndex();
    for (int s = 0; s < SeriesCount; ++s) {
        if (!m_visible[s])
            continue;
        const QVector<float> &v = values(s);
        const float scale = float(m_scale[s].scale), offset = float(m_scale[s].offset);
        p.setPen(seriesColor(s));
        if (s == Pause) {
            for (int i = first; i < v.size(); ++i) {
                const int x = xOf(i);
                if (x >= w)
                    break;
                if (!marker(i))
                    verticalLine(p, x, h - int(v[i] * m_zoomY * scale + m_offsetY * m_zoomY) - 1, h);
            }
        } else if (s == Arrhythmia) {
            // Bars grow from the line of the mean pause (0 %).
            const int base = h - roundAway(m_offsetY * m_zoomY + 100.0f * m_zoomY * scale);
            for (int i = first; i < v.size(); ++i) {
                if (marker(i))
                    continue;
                const int x = roundAway((float(i) + m_scrollX) * m_zoomX);
                if (x >= w)
                    break;
                verticalLine(p, x, h - roundAway(m_offsetY * m_zoomY + (v[i] - offset) * m_zoomY * scale) - 1, base);
            }
            if (base > 0)
                p.drawLine(0, base, w - 1, base);
        } else {
            QPolygon line;
            for (int i = first; i < v.size(); ++i) {
                if (marker(i)) { // a new fragment starts a new line
                    p.drawPolyline(line);
                    line.clear();
                    continue;
                }
                const int x = xOf(i);
                line.append(QPoint(x, h - int(((v[i] - offset) * scale + m_offsetY) * m_zoomY) - 1));
                if (x >= w)
                    break;
            }
            p.drawPolyline(line);
        }
    }

    // Fragment borders.
    p.setPen(QPen(Look::colors().ink, 2));
    for (int i = first; i < m_fragmentStart.size(); ++i) {
        if (!m_fragmentStart[i])
            continue;
        const int x = xOf(i);
        if (x >= w)
            break;
        p.drawLine(x, 0, x, h);
    }
}

void GraphWidget::drawRuler(QPainter &p, const QRect &g)
{
    const int w = g.width(), h = g.height();
    const QFontMetrics fm(font());
    const QLocale loc;
    const std::array<int, 3> axis = axisSeries();
    const int y = h - roundAway((m_rulerY + m_offsetY) * m_zoomY);
    QString text[3];
    int tw = 0;
    for (int idx = 0; idx < 3; ++idx) {
        const Scale &s = m_scale[axis[idx]];
        text[idx] = formatFixed(double(m_rulerY) / s.scale + s.offset, 1, loc);
        tw = std::max(tw, fm.horizontalAdvance(text[idx]));
    }
    const int th = fm.height();
    const int left = w - 6 - tw, top = y - th * 3 / 2;
    m_rulerBox = QRect(QPoint(left, top), QPoint(w + 1, y * 2 - top + 3));
    p.setPen(Look::colors().ink);
    p.setBrush(Look::colors().rulerBox);
    p.drawRect(m_rulerBox.adjusted(0, 0, -1, -1));
    p.setBrush(Qt::NoBrush);
    for (int idx = 0; idx < 3; ++idx) {
        p.setPen(seriesColor(axis[idx]));
        p.drawText(left + 2, top + 1 + th * idx + fm.ascent(), text[idx]);
    }
    p.setPen(Look::colors().ink);
    p.drawLine(0, y, left - 1, y);
}

void GraphWidget::drawStrip(QPainter &p, const QRect &g, int stripFont)
{
    if (stripFont <= 0 || !m_model)
        return;
    const int w = g.width();
    const int half = m_charWidth[stripFont - 8] / 2;
    const QFont f = Look::pointFont(kStripFont, stripFont);
    const QFontMetrics fm(f);
    p.setFont(f);
    for (int i = firstIndex(); i < m_model->size(); ++i) {
        const int x = int(std::floor((float(i) + m_scrollX + 0.5f) * m_zoomX + 0.5f)) - half;
        if (x >= w)
            break;
        p.setPen((m_model->flags[i] & KeyRecord::Injected) ? Look::colors().injected
                 : m_model->erased(i)                       ? Look::colors().erased
                                                            : Look::colors().ink);
        p.drawText(x, g.height() + 1 + fm.ascent(), stripLabel(m_model->names[i]));
    }
    p.setFont(font());
}

void GraphWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect g = graphRect();
    const int w = g.width(), h = g.height();

    // The axis and the line of text: yellow panels with black borders.
    p.fillRect(QRect(0, 0, m_axisWidth, height()), Look::colors().axis);
    p.fillRect(QRect(m_axisWidth, h, w, height() - h), Look::colors().axis);
    p.setPen(Look::colors().ink);
    p.drawLine(0, 0, 0, height() - 1);
    p.drawLine(m_axisWidth - 1, 0, m_axisWidth - 1, height() - 1);
    p.drawLine(m_axisWidth, h, width() - 1, h);

    p.translate(g.topLeft());
    p.setClipRect(QRect(0, 0, w, h));
    p.fillRect(QRect(0, 0, w, h), Look::colors().pane);
    const int x1 = xOf(m_visFrom), x2 = xOf(m_visTo);
    if (x2 > x1)
        p.fillRect(QRect(x1, 0, x2 - x1, h), Look::colors().paneMark);

    // The largest font of the text line whose characters fit into one element.
    int stripFont = -1;
    for (int k = int(m_charWidth.size()) - 1; k >= 0; --k)
        if (float(m_charWidth[k]) < m_zoomX) {
            stripFont = k + 8;
            break;
        }

    drawGrid(p, g, stripFont);
    drawSeries(p, g);
    p.setPen(Look::colors().ink);
    if (!m_cursorHidden)
        p.drawLine(m_mouse.x(), 0, m_mouse.x(), h - 1);
    p.setPen(Look::colors().dimInk);
    p.drawLine(x1, 0, x1, h - 1);
    p.drawLine(x2, 0, x2, h - 1);
    if (m_hasRuler)
        drawRuler(p, g);

    p.setClipRect(QRect(0, h + 1, w, height() - h - 1));
    drawStrip(p, g, stripFont);
}
