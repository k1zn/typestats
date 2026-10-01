#include "KlavogramWidget.h"

#include "core/KeyList.h"
#include "core/KeyName.h"
#include "core/MainStats.h"
#include "core/NumberFormat.h"
#include "Texts.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kTracks = 9;
const QColor kBackground(255, 255, 220);
const QColor kMeasured(255, 255, 180);
const QColor kPopup(255, 230, 230);
const QColor kGrid(175, 175, 175);

// By the number of keys already held when the key went down; {the only key held, several held}.
const QColor kHeldColors[5][2] = {
    {QColor(180, 240, 180), QColor(34, 172, 34)},  {QColor(32, 209, 247), QColor(6, 149, 179)},
    {QColor(233, 154, 252), QColor(202, 18, 248)}, {QColor(250, 139, 148), QColor(211, 10, 24)},
    {QColor(254, 180, 100), QColor(224, 118, 1)},
};

constexpr int kRulerSteps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 60000};
constexpr int kRulerDigits[] = {3, 3, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0};

struct HeldKey
{
    quint32 vk = 0;
    const KlavRecord *press = nullptr;
    int level = 0;
};

// The label of a key on its track; keys with long names get a symbol.
QString trackLabel(const KlavRecord &r)
{
    const QString name = keyDisplayName(r.flags, r.ch);
    if (name == QLatin1String("[LShift]") || name == QLatin1String("[RShift]"))
        return QString(QChar(0x21D1));
    if (name == QLatin1String("[BackSpace]"))
        return QString(QChar(0x2190));
    if (name == QLatin1String("[Ctrl+BackSpace]"))
        return QString(QChar(0x21D0));
    if (name == QLatin1String("\r"))
        return QString(QChar(0x21B5));
    return name;
}

qint64 xOf(const KlavRecord &r, float scrollMs, float zoom)
{
    return qint64((0.001L * r.tDraw - (long double)scrollMs) * (long double)zoom);
}

} // namespace

KlavogramWidget::KlavogramWidget(QWidget *parent) : QWidget(parent), m_font(QStringLiteral("Arial"), 9)
{
    setMinimumHeight(20);
    setMouseTracking(true); // the cursor of the cursor mode follows the mouse
    setContextMenuPolicy(Qt::PreventContextMenu);
}

void KlavogramWidget::setFontSize(int points)
{
    m_font.setPointSize(points);
    update();
}

void KlavogramWidget::setModel(const TextModel *m)
{
    m_model = m;
    m_scrollMs = 0.0f;
    m_selecting = false;
    update();
}

void KlavogramWidget::setZones(const FingerZones &zones)
{
    m_zones = zones;
    update();
}

void KlavogramWidget::scrollToPosition(int textPos)
{
    if (!m_model)
        return;
    m_scrollMs = KeyList::scrollForPosition(*m_model, textPos);
    update();
}

void KlavogramWidget::setScrollMs(float ms)
{
    m_scrollMs = ms;
    update();
}

void KlavogramWidget::setZoom(float pxPerMs)
{
    m_zoom = std::clamp(pxPerMs, 0.04f, 300.0f);
    update();
}

std::pair<double, double> KlavogramWidget::visibleSpanUs() const
{
    const float start = m_scrollMs * 1000.0f - 10.0f;
    const float end = float(double(float(width() * 1000)) / double(m_zoom) + double(start));
    return {double(start), double(end)};
}

int KlavogramWidget::firstRecordAt(long double drawUs) const
{
    const QVector<KlavRecord> &klav = m_model->klav;
    const qint64 t = qint64(drawUs);
    return int(std::lower_bound(klav.begin(), klav.end(), t,
                                [](const KlavRecord &r, qint64 v) { return r.tDraw < v; })
               - klav.begin());
}

std::pair<int, int> KlavogramWidget::visibleRecords() const
{
    if (!m_model)
        return {0, 0};
    return {firstRecordAt((long double)m_scrollMs * 1000.0L),
            firstRecordAt(((long double)width() / m_zoom + m_scrollMs) * 1000.0L)};
}

qint64 KlavogramWidget::absoluteTime(qint64 drawUs) const
{
    const QVector<KlavRecord> &klav = m_model->klav;
    if (klav.isEmpty())
        return drawUs;
    const int i = std::min<int>(firstRecordAt(drawUs), klav.size() - 1);
    return drawUs - klav[i].tDraw + klav[i].t;
}

float KlavogramWidget::measuredSpeed(qint64 fromT, qint64 toT) const
{
    if (fromT > toT)
        std::swap(fromT, toT);
    qint64 first = 0, last = 0;
    int n = 0;
    const QVector<KlavRecord> &klav = m_model->klav;
    const auto from = std::lower_bound(klav.begin(), klav.end(), fromT, [](const KlavRecord &r, qint64 v) { return r.t < v; });
    for (auto it = from; it != klav.end(); ++it) {
        const KlavRecord &r = *it;
        if (r.t < fromT || !r.down || r.erased || !keyDisplayChar(r.flags, r.ch))
            continue;
        if (r.t > toT)
            break;
        last = r.t;
        if (n++ == 0)
            first = last;
    }
    return last - first < 10 ? 0.0f : float(double(n - 1) * 6e7 / double(last - first));
}

void KlavogramWidget::clampScroll()
{
    const float minScroll = float(-width()) / (m_zoom * 4.0f) * 3.0f;
    m_scrollMs = std::max(m_scrollMs, minScroll);
    float maxScroll = m_model && !m_model->klav.isEmpty() ? float(0.001L * m_model->klav.last().tDraw) : 0.0f;
    maxScroll = std::max(maxScroll, 0.0f);
    m_scrollMs = std::min(m_scrollMs, maxScroll);
}

void KlavogramWidget::mousePressEvent(QMouseEvent *e)
{
    m_lastMouseX = e->position().toPoint().x();
    if (e->button() != Qt::MiddleButton && (e->buttons() & Qt::LeftButton) && (e->buttons() & Qt::RightButton)) {
        if (!m_selecting)
            m_selectionPending = true;
        m_selecting = true;
        update();
    }
}

void KlavogramWidget::mouseMoveEvent(QMouseEvent *e)
{
    const int x = e->position().toPoint().x();
    const bool left = e->buttons() & Qt::LeftButton, right = e->buttons() & Qt::RightButton;
    const bool selecting = left && right;
    if (!selecting)
        m_selecting = false;
    else if (!m_selecting)
        m_selecting = m_selectionPending = true;
    if (x > 0)
        m_cursorX = x;

    bool changed = false;
    if (!selecting) {
        if (left) {
            m_scrollMs += float(m_lastMouseX - x) / m_zoom;
            changed = true;
        }
        if (right) {
            m_zoom = float((long double)m_zoom - 0.01L * (long double)(m_lastMouseX - x) * (long double)m_zoom);
            m_zoom = std::clamp(m_zoom, 0.04f, 300.0f);
            changed = true;
        }
    }
    if (e->buttons() & Qt::MiddleButton) {
        m_scrollMs += float((m_lastMouseX - x) * 50) / m_zoom;
        changed = true;
    }
    m_lastMouseX = x;
    if (changed) {
        clampScroll();
        emit viewChanged();
    }
    if (changed || selecting || m_cursorMode)
        update();
}

void KlavogramWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!((e->buttons() & Qt::LeftButton) && (e->buttons() & Qt::RightButton)) && m_selecting) {
        m_selecting = false;
        update();
    }
}

void KlavogramWidget::mouseDoubleClickEvent(QMouseEvent *)
{
    m_cursorMode = !m_cursorMode;
    update();
}

void KlavogramWidget::wheelEvent(QWheelEvent *e)
{
    m_scrollMs -= float(e->angleDelta().y()) / (m_zoom * 2.0f);
    clampScroll();
    emit viewChanged();
    update();
    e->accept();
}

void KlavogramWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    emit viewChanged();
}

void KlavogramWidget::drawRuler(QPainter &p, int top, int rowHeight)
{
    const QFontMetrics fm(m_font);
    const QLocale loc;
    const StatsUnits units = Texts::units();
    const int w = width();
    // The first step whose labels do not run into each other.
    int step = kRulerSteps[0], digits = kRulerDigits[0], labelWidth = 0;
    for (int i = 0; i < int(std::size(kRulerSteps)); ++i) {
        step = kRulerSteps[i];
        digits = kRulerDigits[i];
        const QString widest = Stats::formatTime(int(float(w) / m_zoom + m_scrollMs), digits, loc, units) + QLatin1Char('W');
        labelWidth = fm.horizontalAdvance(widest);
        if (step > int(float(labelWidth) / m_zoom))
            break;
    }
    const int half = labelWidth / 2;
    float t = float(int((m_scrollMs - float(half)) / float(step) - 1.0f) * step) + (float(step) - m_scrollMs);

    p.setPen(QPen(kGrid, 1, Qt::DotLine));
    for (;; t += float(step)) {
        const qint64 x = qint64(t * m_zoom);
        if (x >= w + half)
            break;
        const float v = t + m_scrollMs;
        const int ms = int(v < 0.0f ? v - 0.5f : v + 0.5f);
        if (ms < 0)
            continue;
        const QString label = Stats::formatTime(ms, digits, loc, units);
        const int lw = fm.horizontalAdvance(label);
        p.setPen(Qt::black);
        p.drawText(QRect(int(x) - lw / 2, top - fm.height() - 1, lw, fm.height()), Qt::AlignCenter | Qt::TextDontClip, label);
        p.setPen(QPen(kGrid, 1, Qt::DotLine));
        p.drawLine(int(x), top, int(x), top + rowHeight * kTracks);
    }
}

void KlavogramWidget::drawKeys(QPainter &p, int top, int rowHeight)
{
    const QVector<KlavRecord> &klav = m_model->klav;
    const QFontMetrics fm(m_font);
    const int w = width();
    const int bottom = top + rowHeight * kTracks;
    auto track = [&](const KlavRecord &r) { return int(m_zones.finger(r.flags)); };

    // Go back until every track has been seen: that restores the keys held at the left edge.
    int start = firstRecordAt((long double)m_scrollMs * 1000.0L);
    std::array<bool, kTracks> seen{};
    for (; start > 0; --start) {
        if (start >= klav.size())
            continue;
        seen[track(klav[start])] = true;
        if (std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }))
            break;
    }

    std::array<QVector<HeldKey>, kTracks> held; // the latest press first
    std::array<qint64, kTracks> segmentX{}, lastX{};
    int heldCount = 0;
    const QPen black(Qt::black, 1);
    for (int i = start; i < klav.size(); ++i) {
        const KlavRecord &r = klav[i];
        qint64 x = xOf(r, m_scrollMs, m_zoom);
        const int several = heldCount > 1;
        for (int f = 0; f < kTracks; ++f) {
            if (held[f].isEmpty())
                continue;
            if (x >= 0)
                p.fillRect(QRect(QPoint(int(segmentX[f]), top + rowHeight * f), QPoint(int(x) - 1, top + rowHeight * (f + 1) - 1)),
                           kHeldColors[held[f].first().level][several]);
            segmentX[f] = x;
        }
        const int f = track(r);
        const bool hadKeys = !held[f].isEmpty();
        if (x >= 0 && hadKeys) {
            const KlavRecord &key = *held[f].first().press;
            const int y = top + rowHeight * f;
            const QString label = trackLabel(key);
            const int lw = fm.horizontalAdvance(label);
            if (lw < x - lastX[f] - 1 && fm.height() < rowHeight) {
                p.setPen(black);
                p.drawText(QRect(int((lastX[f] + x - lw) / 2), y + (rowHeight - fm.height()) / 2, lw, fm.height()),
                           Qt::AlignCenter | Qt::TextDontClip, label);
            }
            if (!key.erased && !(key.flags & KeyRecord::Injected)) {
                p.setPen(black);
                p.drawLine(int(lastX[f]), y, int(lastX[f]), y + rowHeight - 1);
                p.drawLine(int(x) - 1, y, int(x) - 1, y + rowHeight - 1);
            } else {
                // Erased keys get a red frame, injected ones a blue one.
                QPen frame((key.flags & KeyRecord::Injected) ? QColor(0, 0, 255) : QColor(255, 0, 0), 3);
                frame.setJoinStyle(Qt::MiterJoin);
                p.setPen(frame);
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRect(QPoint(int(lastX[f]) + 1, y), QPoint(int(x) - 3, y + rowHeight - 3)));
            }
        }
        const quint32 vk = r.flags & KeyRecord::VkMask;
        if (!r.down) {
            held[f].removeIf([vk](const HeldKey &k) { return k.vk == vk; });
        } else if (x <= w) {
            segmentX[f] = x;
            if (vk == quint32(Vk::Space) << 16) {
                p.setPen(QPen(Qt::black, 1, Qt::DashLine));
                p.drawLine(int(x), top, int(x), bottom);
            }
            if (std::none_of(held[f].begin(), held[f].end(), [vk](const HeldKey &k) { return k.vk == vk; }))
                held[f].prepend({vk, &r, std::min(heldCount, 4)});
        }
        if (r.fragmentStart) {
            p.setPen(QPen(Qt::black, 2));
            p.drawLine(int(x), top, int(x), bottom);
        }
        heldCount = 0;
        for (const QVector<HeldKey> &keys : held)
            heldCount += keys.size();
        if (hadKeys)
            --x;
        lastX[f] = x;
        if (x > w && heldCount == 0)
            break;
    }
}

void KlavogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setFont(m_font);
    const QFontMetrics fm(m_font);
    const int w = width(), h = height();
    const int rowHeight = std::max(1, (h - fm.height()) / kTracks);
    const int top = h - rowHeight * kTracks;
    p.fillRect(rect(), kBackground);

    // The measuring cursor sticks to the nearest key event within 8 px.
    int cursorX = -1;
    if (m_cursorMode && m_model) {
        float best = 1e30f;
        qint64 bestDraw = 0;
        const QVector<KlavRecord> &klav = m_model->klav;
        auto pxOf = [this](const KlavRecord &r) { return (float(0.001L * r.tDraw) - m_scrollMs) * m_zoom; };
        // x does not decrease along the records, so the nearest one is next to the first record at
        // the cursor or beyond the widget: the search starts at the first of the records before it
        // that share its x (the earliest of equally near records wins).
        auto start = std::partition_point(klav.begin(), klav.end(), [&](const KlavRecord &r) {
            const float px = pxOf(r);
            return px < float(m_cursorX) && !(px > float(w));
        });
        if (start != klav.begin()) {
            --start;
            const float px = pxOf(*start);
            while (start != klav.begin() && pxOf(*(start - 1)) == px)
                --start;
        }
        for (auto it = start; it != klav.end(); ++it) {
            const KlavRecord &r = *it;
            const float px = pxOf(r);
            const float distance = std::fabs(px - float(m_cursorX));
            if (distance < best) {
                best = distance;
                bestDraw = r.tDraw;
                cursorX = int(px);
            } else if (distance > best) {
                break;
            }
            if (px > float(w))
                break;
        }
        qint64 drawUs = bestDraw;
        if (best > 8.0f) {
            cursorX = m_cursorX;
            drawUs = qint64((float(m_cursorX) / m_zoom + m_scrollMs) * 1000.0f);
        }
        m_cursorT = absoluteTime(drawUs);
        if (m_selectionPending) {
            m_selectionPending = false;
            m_selectionT = m_cursorT;
            m_selectionX = cursorX;
        }
        if (m_selecting)
            p.fillRect(QRect(QPoint(std::min(m_selectionX, cursorX), top), QPoint(std::max(m_selectionX, cursorX) - 1, h - 1)),
                       kMeasured);
    }

    drawRuler(p, top, rowHeight);
    if (m_model)
        drawKeys(p, top, rowHeight);

    for (int i = 0; i <= kTracks; ++i) {
        const int y = top + rowHeight * i - 1;
        p.setPen(QPen(Qt::black, i == 0 || i == 4 || i == 8 ? 2 : 1));
        p.drawLine(0, y, w, y);
    }

    if (m_cursorMode && m_model) {
        p.setPen(QPen(Qt::black, 1));
        p.drawLine(cursorX, top - 1, cursorX, top + rowHeight * kTracks - 1);
        if (m_selecting) {
            p.drawLine(m_selectionX, top - 1, m_selectionX, top + rowHeight * kTracks - 1);
            const QLocale loc;
            const StatsUnits units = Texts::units();
            const QString time = formatFixed(std::fabs(double(0.001L * (m_cursorT - m_selectionT))), 3, loc)
                                 + QLatin1Char(' ') + units.ms;
            const QString speed = formatFixed(measuredSpeed(m_selectionT, m_cursorT), 2, loc) + QLatin1Char(' ') + tr("зн/мин");
            const int tw = std::max(fm.horizontalAdvance(time), fm.horizontalAdvance(speed));
            const QRect box(cursorX, top + 2, tw + 6, fm.height() * 2 + 4);
            p.setBrush(kPopup);
            p.drawRect(box);
            p.drawText(cursorX + 2, top + 4, tw, fm.height(), Qt::AlignLeft | Qt::AlignVCenter, time);
            p.drawText(cursorX + 2, top + 4 + fm.height(), tw, fm.height(), Qt::AlignLeft | Qt::AlignVCenter, speed);
        }
    }
}
