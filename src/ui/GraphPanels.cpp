#include "GraphPanels.h"

#include "Look.h"
#include "core/NumberFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

QColor captionColor()
{
#ifdef Q_OS_WIN
    const COLORREF c = GetSysColor(COLOR_ACTIVECAPTION);
    return QColor(GetRValue(c), GetGValue(c), GetBValue(c));
#else
    return QColor(153, 180, 209);
#endif
}

} // namespace

FloatingPanel::FloatingPanel(const QString &title, QWidget *parent) : QFrame(parent), m_title(title)
{
    setAutoFillBackground(true);
    hide();
}

QRect FloatingPanel::closeRect() const
{
    return QRect(width() - frame() - 16, frame() + 1, 15, 15);
}

void FloatingPanel::keepInside()
{
    if (!parentWidget())
        return;
    const int x = std::max(0, std::min(this->x(), parentWidget()->width() - width()));
    const int y = std::max(0, std::min(this->y(), parentWidget()->height() - height()));
    move(x, y);
}

void FloatingPanel::paintEvent(QPaintEvent *e)
{
    QFrame::paintEvent(e);
    QPainter p(this);
    const QRect caption(frame(), frame(), width() - 2 * frame(), kCaptionHeight);
    p.fillRect(caption, captionColor());
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(caption.adjusted(4, 0, -18, 0), Qt::AlignLeft | Qt::AlignVCenter, m_title);

    const QRect c = closeRect();
    p.fillRect(c, QColor(208, 0, 0));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 1.6));
    p.drawLine(c.topLeft() + QPoint(4, 4), c.bottomRight() - QPoint(3, 3));
    p.drawLine(c.topRight() + QPoint(-3, 4), c.bottomLeft() + QPoint(4, -3));
}

void FloatingPanel::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (e->button() != Qt::LeftButton || pos.y() >= frame() + kCaptionHeight)
        return;
    if (closeRect().contains(pos)) {
        hide();
        emit closed();
        return;
    }
    m_dragging = true;
    m_grab = pos;
    raise();
}

void FloatingPanel::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging || !(e->buttons() & Qt::LeftButton)) {
        m_dragging = false;
        return;
    }
    move(pos() + e->position().toPoint() - m_grab);
    keepInside();
}

void FloatingPanel::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (pos.y() < frame() + kCaptionHeight && !closeRect().contains(pos))
        emit captionDoubleClicked();
}

LegendPanel::LegendPanel(GraphWidget *graph, QWidget *parent)
    : FloatingPanel(tr("Легенда"), parent), m_graph(graph), m_font(QStringLiteral("Courier New"), 8)
{
    setFrameStyle(int(QFrame::Box) | int(QFrame::Plain));
    setLineWidth(1);
    updateColors();
    const QFontMetrics fm(m_font);
    m_rowHeight = fm.height();
    m_charWidth = fm.horizontalAdvance(QLatin1Char('1'));
    setMinimized(false);
    connect(this, &FloatingPanel::captionDoubleClicked, this, [this] { setMinimized(!m_minimized); });
    connect(graph, &GraphWidget::cursorChanged, this, qOverload<>(&QWidget::update));
    connect(graph, &GraphWidget::seriesVisibilityChanged, this, qOverload<>(&QWidget::update));
}

void LegendPanel::updateColors()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Look::colors().legend);
    pal.setColor(QPalette::WindowText, Look::colors().keyEdge); // the frame: black, and not light in the dark theme
    setPalette(pal);
}

void LegendPanel::setMinimized(bool on)
{
    m_minimized = on;
    setFixedSize(m_charWidth * (on ? 9 : 38) + 2 * frame(),
                 kCaptionHeight + m_rowHeight * GraphWidget::SeriesCount + 2 * frame());
    keepInside();
    update();
}

int LegendPanel::rowAt(const QPoint &pos) const
{
    const int y = pos.y() - frame() - kCaptionHeight - 2;
    const int row = y / m_rowHeight;
    return y >= 0 && row < GraphWidget::SeriesCount ? row : -1;
}

void LegendPanel::paintEvent(QPaintEvent *e)
{
    FloatingPanel::paintEvent(e);
    QPainter p(this);
    const QFontMetrics fm(m_font);
    const QLocale loc;
    const int left = frame(), top = frame() + kCaptionHeight;
    p.setClipRect(QRect(left, top, width() - 2 * left, height() - top - frame()));
    for (int s = 0; s < GraphWidget::SeriesCount; ++s) {
        // " Name<padded to 26> :", then the value under the cursor of the graph.
        QString text = QLatin1Char(' ') + GraphWidget::seriesName(s).leftJustified(26) + QLatin1Char(':');
        if (m_minimized)
            text.truncate(8);
        text += QLatin1Char(' ');
        float value = 0.0f;
        if (m_graph->valueAtCursor(s, &value)) {
            if (m_minimized)
                text = QStringLiteral(" ");
            text += formatFixed(double(value), 2, loc);
        }
        QFont f = m_font;
        f.setBold(m_graph->seriesVisible(s));
        p.setFont(f);
        p.setPen(GraphWidget::seriesColor(s));
        p.drawText(left, top + m_rowHeight * s + fm.ascent(), text);
    }
}

void LegendPanel::mousePressEvent(QMouseEvent *e)
{
    FloatingPanel::mousePressEvent(e);
    const int row = rowAt(e->position().toPoint());
    if (row >= 0 && e->position().toPoint().y() >= frame() + kCaptionHeight)
        m_graph->setSeriesVisible(row, !m_graph->seriesVisible(row));
}

bool LegendPanel::event(QEvent *e)
{
    // The minimized legend names the series in a hint.
    if (e->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(e);
        const int row = rowAt(help->pos());
        if (m_minimized && row >= 0)
            QToolTip::showText(help->globalPos(), GraphWidget::seriesName(row), this);
        else
            QToolTip::hideText();
        return true;
    }
    return FloatingPanel::event(e);
}

AxisPanel::AxisPanel(QWidget *parent) : FloatingPanel(tr("Настройка оси Y"), parent)
{
    setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    setFixedSize(205, 117);
    auto header = [this](const QString &text, int x, int y, int w) {
        auto *l = new QLabel(QLatin1Char(' ') + text, this);
        l->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
        l->setGeometry(x, y, w, 18);
    };
    header(tr("Скорость"), 43, 23, 80);
    header(tr("Ритмичность"), 123, 23, 79);
    header(tr("Макс"), 3, 41, 41);
    header(tr("Мин"), 3, 59, 41);
    m_speedMax = limitBox(43, 39, QStringLiteral("100"));
    m_speedMin = limitBox(43, 58, QStringLiteral("0"));
    m_rhythmMax = limitBox(123, 39, QStringLiteral("100"));
    m_rhythmMin = limitBox(123, 58, QStringLiteral("0"));
    m_autoRound = new QCheckBox(tr("Авто с округлением"), this);
    m_autoRound->setGeometry(8, 81, 190, 17);
    m_autoRound->setChecked(true);
    m_lockY = new QCheckBox(tr("Зафиксировать"), this);
    m_lockY->setGeometry(8, 98, 190, 17);
    connect(m_autoRound, &QCheckBox::toggled, this, &AxisPanel::settingsChanged);
    connect(m_lockY, &QCheckBox::toggled, this, &AxisPanel::lockYChanged);
}

QComboBox *AxisPanel::limitBox(int x, int y, const QString &second)
{
    auto *box = new QComboBox(this);
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->addItems({tr("Авто"), second});
    box->setGeometry(x, y, 81, 21);
    connect(box, &QComboBox::activated, this, &AxisPanel::settingsChanged);
    connect(box->lineEdit(), &QLineEdit::returnPressed, this, &AxisPanel::settingsChanged);
    return box;
}

GraphWidget::AxisSettings AxisPanel::settings() const
{
    auto limit = [](const QComboBox *box) {
        bool ok = false;
        const int v = box->currentText().trimmed().toInt(&ok);
        return ok ? v : -1;
    };
    GraphWidget::AxisSettings s;
    s.speedMin = limit(m_speedMin);
    s.speedMax = limit(m_speedMax);
    s.rhythmMin = limit(m_rhythmMin);
    s.rhythmMax = limit(m_rhythmMax);
    s.autoRound = m_autoRound->isChecked();
    return s;
}

void AxisPanel::setSettings(const GraphWidget::AxisSettings &s)
{
    auto set = [](QComboBox *box, int v) {
        const QSignalBlocker b(box);
        box->setEditText(v < 0 ? tr("Авто") : QString::number(v));
    };
    set(m_speedMin, s.speedMin);
    set(m_speedMax, s.speedMax);
    set(m_rhythmMin, s.rhythmMin);
    set(m_rhythmMax, s.rhythmMax);
    const QSignalBlocker b(m_autoRound);
    m_autoRound->setChecked(s.autoRound);
}

void AxisPanel::setAutoLimits(const GraphWidget::AutoLimits &a)
{
    auto set = [](QComboBox *box, int v) {
        const QSignalBlocker b(box);
        const QString text = box->currentText();
        box->setItemText(1, QString::number(v));
        box->setEditText(text);
    };
    set(m_speedMin, a.speedMin);
    set(m_speedMax, a.speedMax);
    set(m_rhythmMin, a.rhythmMin);
    set(m_rhythmMax, a.rhythmMax);
}

bool AxisPanel::lockY() const
{
    return m_lockY->isChecked();
}

void AxisPanel::setLockY(bool on)
{
    m_lockY->setChecked(on);
}
