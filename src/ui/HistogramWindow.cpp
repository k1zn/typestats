#include "HistogramWindow.h"

#include "Texts.h"
#include "core/NumberFormat.h"

#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>

namespace {

const QColor kBackground(255, 255, 220);
const QColor kAxis(240, 240, 32);
const QColor kGrid(175, 175, 175);
const QColor kBar(180, 240, 180);
const QString kLabelFont = QStringLiteral("Courier New");

constexpr float kGridSteps[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f};
constexpr float kMinZoomX = 0.5f, kMaxZoomX = 100.0f, kMinZoomY = 0.4f, kMaxZoomY = 50.0f;

// Keys with long names get the symbols of the graph's text line; other long labels stay as they are.
QString barLabel(const QString &name)
{
    if (name == QLatin1String("[LShift]") || name == QLatin1String("[RShift]"))
        return QString(QChar(0x21D1));
    if (name == QLatin1String("[BackSpace]"))
        return QString(QChar(0x2190));
    if (name == QLatin1String("[Ctrl+BackSpace]"))
        return QString(QChar(0x21D0));
    if (name == QLatin1String("\r") || name == QLatin1String("[Enter]"))
        return QString(QChar(0x21B5));
    return name;
}

QToolButton *toolButton(QWidget *parent, int n, int x, const QString &hint)
{
    auto *b = new QToolButton(parent);
    b->setIcon(QIcon(QStringLiteral(":/icons/Form4_SpeedButton%1.png").arg(n)));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(hint);
    b->setFocusPolicy(Qt::NoFocus);
    b->setGeometry(x, 3, 23, 22);
    return b;
}

} // namespace

// ---------------------------------------------------------------- the bars

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent)
{
    setContextMenuPolicy(Qt::PreventContextMenu);
    m_axisWidth = int(float(QFontMetrics(font()).horizontalAdvance(QLatin1Char('0'))) * 5.5f);
    m_stripHeight = QFontMetrics(QFont(kLabelFont, 18)).height();
    for (int k = 0; k < 4; ++k)
        m_charWidth[k] = QFontMetrics(QFont(kLabelFont, k + 8)).horizontalAdvance(QLatin1Char('1'));
}

QRect HistogramWidget::plotRect() const
{
    return QRect(m_axisWidth, 0, std::max(1, width() - m_axisWidth), std::max(1, height() - m_stripHeight));
}

void HistogramWidget::setBars(const QVector<float> &values, const QStringList &labels)
{
    m_values = values;
    m_labels = labels;
    if (!values.isEmpty()) {
        // FormShow: everything fits, a bar is at most 100 px wide. The pan is kept.
        const QRect g = plotRect();
        float max = 1.0f;
        for (float v : values)
            max = std::max(max, v);
        m_zoomY = std::min(float(g.height()) / max, kMaxZoomY);
        m_minZoomY = std::min(kMinZoomY, m_zoomY);
        m_zoomX = std::clamp(float(g.width()) / float(values.size()), kMinZoomX, kMaxZoomX);
    }
    update();
}

int HistogramWidget::firstIndex() const
{
    return std::max(0, int(std::floor(-m_scrollX)));
}

int HistogramWidget::barAt(int x) const
{
    const float v = float(x - m_axisWidth) / m_zoomX - m_scrollX;
    if (!(v > 0.0f) || !(v < float(m_values.size())))
        return -1;
    return int(v);
}

void HistogramWidget::clampScroll()
{
    const int q = plotRect().width() / 4;
    const float lo = std::min(1.0f, float(q) / m_zoomX - float(m_values.size()));
    m_scrollX = std::clamp(m_scrollX, lo, std::max(lo, float(3 * q) / m_zoomX));
    if (m_offsetY > 0.0f)
        m_offsetY = 0.0f; // the bars do not leave the bottom line
}

void HistogramWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    // The picture stretches with the window.
    const QSize old = e->oldSize();
    if (old.width() - m_axisWidth > 0 && old.height() - m_stripHeight > 0) {
        const QRect g = plotRect();
        m_zoomX *= float(g.width()) / float(old.width() - m_axisWidth);
        const float factor = float(g.height()) / float(old.height() - m_stripHeight);
        m_zoomY *= factor;
        m_minZoomY = std::min(kMinZoomY, m_minZoomY * factor);
    }
}

void HistogramWidget::mousePressEvent(QMouseEvent *e)
{
    m_mouse = e->position().toPoint();
    emit barHeld(barAt(m_mouse.x()), e->globalPosition().toPoint());
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->buttons() == Qt::NoButton)
        emit barHeld(-1, {});
}

void HistogramWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int index = barAt(e->position().toPoint().x());
    if (index >= 0)
        emit barDoubleClicked(index);
}

void HistogramWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const int dx = pos.x() - m_mouse.x(), dy = pos.y() - m_mouse.y();
    m_mouse = pos;
    const bool left = e->buttons() & Qt::LeftButton, right = e->buttons() & Qt::RightButton;
    const bool middle = e->buttons() & Qt::MiddleButton;
    if (!left && !right && !middle)
        return;
    if (right && !left) {
        const float oldZoom = m_zoomX;
        m_zoomX = std::clamp(float(dx) * m_zoomX / 100.0f + m_zoomX, kMinZoomX, kMaxZoomX);
        m_zoomY = std::clamp(m_zoomY - float(dy) * m_zoomY / 100.0f, m_minZoomY, kMaxZoomY);
        m_scrollX = (m_scrollX - 1.0f) * oldZoom / m_zoomX + 1.0f;
    }
    if (left && !right) {
        m_scrollX += float(dx) / m_zoomX;
        m_offsetY -= float(dy) / m_zoomY;
    }
    if (middle && !(left && right))
        m_scrollX += float(dx * 50) / m_zoomX;
    clampScroll();
    emit barHeld(barAt(pos.x()), e->globalPosition().toPoint());
    update();
}

void HistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect g = plotRect();
    const int w = g.width(), h = g.height();

    // The axis and the line of labels: yellow panels with black borders.
    p.fillRect(QRect(0, 0, m_axisWidth, height()), kAxis);
    p.fillRect(QRect(m_axisWidth, h, w, height() - h), kAxis);
    p.setPen(Qt::black);
    p.drawLine(0, 0, 0, height() - 1);
    p.drawLine(m_axisWidth - 1, 0, m_axisWidth - 1, height() - 1);
    p.drawLine(m_axisWidth, h, width() - 1, h);
    p.fillRect(g, kBackground);
    if (m_values.isEmpty())
        return;

    // The largest font of the labels whose characters are narrower than a bar.
    int labelFont = -1;
    for (int k = 3; k >= 0; --k)
        if (float(m_charWidth[k]) < m_zoomX) {
            labelFont = k + 8;
            break;
        }

    // The grid and the numbers of the axis.
    const QFontMetrics fm(font());
    const int cy = fm.height() / 2;
    float step = kGridSteps[std::size(kGridSteps) - 1];
    for (float s : kGridSteps)
        if (float(7 * cy) / m_zoomY < s) {
            step = s;
            break;
        }
    const float top = float(h) / m_zoomY - m_offsetY;
    QPen dots(kGrid, 1);
    dots.setDashPattern({3, 3}); // PS_DOT of GDI
    for (float a = float(int(-m_offsetY / step)) * step; a < top; a += step) {
        const float py = (m_offsetY + a) * m_zoomY;
        if (!(py > -2.0f))
            continue;
        if (py > 0.1f) {
            p.setPen(dots);
            const int y = int(float(h) - py);
            p.drawLine(g.left(), y, g.right(), y);
        }
        p.setPen(Qt::black);
        p.setClipRect(QRect(1, 0, m_axisWidth - 2, height()));
        p.drawText(2, int(float(h) - py - float(cy)) + fm.ascent(), formatFixed(double(a), 0, QLocale()));
        p.setClipping(false);
    }

    // The bars: neighbours share a border.
    p.setClipRect(g);
    p.translate(g.topLeft());
    p.setPen(Qt::black);
    p.setBrush(kBar);
    const int first = firstIndex();
    for (int i = first; i < m_values.size(); ++i) {
        const int x1 = int(std::floor((float(i) + m_scrollX) * m_zoomX + 0.5f));
        if (x1 >= w)
            break;
        const int x2 = int(std::floor((float(i) + m_scrollX + 1.0f) * m_zoomX + 0.5f));
        const int barHeight = int(m_values[i] * m_zoomY + m_offsetY * m_zoomY);
        p.drawRect(QRect(QPoint(x1, h - barHeight), QPoint(x2, h)).normalized().adjusted(0, 0, -1, -1));
    }
    p.setBrush(Qt::NoBrush);
    p.resetTransform();
    p.setClipping(false);

    // The labels, each centred under its bar; one that would run into the previous one is left out.
    if (labelFont > 0) {
        const QFont f(kLabelFont, labelFont);
        const QFontMetrics lfm(f);
        p.setFont(f);
        p.setClipRect(QRect(m_axisWidth, h + 1, w, height() - h - 1));
        int last = -1000;
        for (int i = first; i < m_labels.size(); ++i) {
            const QString label = barLabel(m_labels[i]);
            const int tw = lfm.horizontalAdvance(label);
            const int x = int(std::floor((float(i) + m_scrollX + 0.5f) * m_zoomX + 0.5f) - float(tw / 2));
            if (x >= w)
                break;
            if (last <= x) {
                p.drawText(m_axisWidth + x, h + 1 + lfm.ascent(), label);
                last = x + tw;
            }
        }
    }
}

// ---------------------------------------------------------------- the window

HistogramWindow::HistogramWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Статистические гистограммы"));
    m_toolBar = new QFrame;
    m_toolBar->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    m_toolBar->setFixedHeight(27);
    QToolButton *backButton = toolButton(m_toolBar, 1, 5, tr("Назад"));
    QToolButton *keysButton = toolButton(m_toolBar, 2, 28, tr("Клавиши"));
    QToolButton *fingersButton = toolButton(m_toolBar, 3, 51, tr("Пальцы"));
    QToolButton *extraButton = toolButton(m_toolBar, 12, 74, tr("Дополнительная статистика"));
    m_onTop = toolButton(m_toolBar, 4, 610, tr("Поверх всех окон"));
    m_onTop->setCheckable(true);
    m_title = new QLabel(m_toolBar);
    m_title->move(104, 8);
    m_chart = new HistogramWidget;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toolBar);
    layout->addWidget(m_chart, 1);

    connect(backButton, &QToolButton::clicked, this, &HistogramWindow::back);
    connect(keysButton, &QToolButton::clicked, this, [this] { setRoot(Histograms::Node::AllKeys); });
    connect(fingersButton, &QToolButton::clicked, this, [this] { setRoot(Histograms::Node::AllFingers); });
    connect(extraButton, &QToolButton::clicked, this, [this] {
        emit extraRequested();
        setRoot(Histograms::Node::Extra);
    });
    connect(m_onTop, &QToolButton::toggled, this, [this](bool on) {
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show(); // changing the flags hides the window
    });
    connect(m_chart, &HistogramWidget::barDoubleClicked, this, &HistogramWindow::drill);
    connect(m_chart, &HistogramWidget::barHeld, this, &HistogramWindow::showHint);

    m_stack = {Histograms::Node{}};
    resize(636, 473);
}

void HistogramWindow::setSource(const Histograms::Source &source)
{
    m_source = source;
    m_stack.resize(1); // back to the root (FUN_00451b88)
    showPage();
}

void HistogramWindow::setExtraRows(const QVector<ExtraStats::Row> &rows)
{
    m_extraRows = rows;
    if (m_stack.last().kind == Histograms::Node::Extra)
        showPage();
}

void HistogramWindow::setRoot(Histograms::Node::Kind kind)
{
    Histograms::Node root;
    root.kind = kind;
    m_stack = {root};
    showPage();
}

void HistogramWindow::back()
{
    if (m_stack.size() > 1) {
        m_stack.removeLast();
        showPage();
    }
}

void HistogramWindow::showPage()
{
    if (!isVisible())
        return; // filled when shown
    const Histograms::Node &node = m_stack.last();
    if (node.kind == Histograms::Node::Extra)
        m_page = Histograms::fromExtra(m_extraRows, m_source.names);
    else if (m_source.model)
        m_page = Histograms::build(m_source, node);
    else
        m_page = {};
    m_title->setText(m_page.title);
    m_title->adjustSize();
    QVector<float> values;
    QStringList labels;
    for (const Histograms::Bar &bar : m_page.bars) {
        values << bar.value;
        labels << bar.label;
    }
    m_chart->setBars(values, labels);
}

void HistogramWindow::drill(int index)
{
    if (index < 0 || index >= m_page.bars.size())
        return;
    const Histograms::Node node = m_stack.last();
    if (node.kind == Histograms::Node::Extra) {
        emit extraRowSelected(index);
    } else if (const std::optional<Histograms::Node> next = Histograms::drill(node, m_page, index)) {
        m_stack << *next;
        showPage();
    } else if (m_page.bars[index].rec >= 0 && m_source.model) {
        emit elementSelected(m_source.model->elementOfRecord(m_page.bars[index].rec));
    }
}

void HistogramWindow::showHint(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_page.bars.size())
        QToolTip::hideText();
    else
        QToolTip::showText(globalPos, Histograms::hint(m_page.bars[index], QLocale()), m_chart);
}

void HistogramWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    emit shown();
    showPage();
}

void HistogramWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_onTop->move(width() - 26, 3);
}
