#include "LiveStatsWindow.h"

#include "Texts.h"
#include "core/NumberFormat.h"

#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

#include <cmath>

LiveStatsWindow::LiveStatsWindow(QWidget *parent) : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Оперативная статистика"));
    QFont big(QStringLiteral("Arial"));
    big.setPixelSize(37);
    big.setBold(true);
    auto label = [&](const QString &text, const QColor &background) {
        auto *l = new QLabel(text);
        l->setFont(big);
        l->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        l->setAutoFillBackground(true);
        l->setMinimumHeight(15);
        QPalette p = l->palette();
        p.setColor(QPalette::Window, background);
        l->setPalette(p);
        return l;
    };
    m_speed = label(QStringLiteral("0"), QColor(192, 192, 192));
    m_errors = label(formatFixed(0.0, 2, QLocale()) + QLatin1Char('%'), Qt::white);
    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);
    m_split->addWidget(m_speed);
    m_split->addWidget(m_errors);
    m_split->setSizes({58, 42});
    m_status = new QLabel;
    m_status->setFrameStyle(int(QFrame::Panel) | int(QFrame::Sunken));
    m_status->setFixedHeight(19);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_split, 1);
    layout->addWidget(m_status);
    resize(160, 123);
    setStats({});
}

// FUN_0042a17c: magenta - blue - cyan - green - yellow - red for t = 0..1.
QColor LiveStatsWindow::speedColor(double t)
{
    auto level = [](double x) { return int(std::pow(x, 0.4) * 255.0); };
    if (t > 0.8)
        return QColor(255, level((1.0 - std::min(t, 1.0)) * 5.0), 0);
    if (t > 0.6)
        return QColor(level(1.0 - (0.8 - t) * 5.0), 255, 0);
    if (t > 0.4)
        return QColor(0, 255, level((0.6 - t) * 5.0));
    if (t > 0.2)
        return QColor(0, level(1.0 - (0.4 - t) * 5.0), 255);
    return QColor(level((0.2 - std::max(t, 0.0)) * 5.0), 0, 255);
}

void LiveStatsWindow::setSpeedRange(int lo, int hi)
{
    m_lo = lo;
    m_hi = hi;
}

void LiveStatsWindow::setStats(const LiveStats &s)
{
    const QLocale loc;
    m_speed->setText(QString::number(int(s.speed)));
    QPalette p = m_speed->palette();
    p.setColor(QPalette::WindowText, m_hi == m_lo ? QColor(Qt::black)
                                                  : speedColor(double((s.speed - float(m_lo)) / float(m_hi - m_lo))));
    m_speed->setPalette(p);
    m_errors->setText(formatFixed(double(s.errorPercent), 2, loc) + QLatin1Char('%'));
    m_status->setText(QStringLiteral("%1  %2").arg(s.count).arg(Stats::formatTime(int(s.timeUs / 1000), 1, loc, Texts::units())));
}

void LiveStatsWindow::loadSettings()
{
    const QSettings s;
    if (s.contains(QStringLiteral("opWinLeft")))
        setGeometry(s.value(QStringLiteral("opWinLeft")).toInt(), s.value(QStringLiteral("opWinTop")).toInt(),
                    s.value(QStringLiteral("opWinWidth"), 160).toInt(), s.value(QStringLiteral("opWinHeight"), 123).toInt());
    const int lower = s.value(QStringLiteral("opSect2Height"), 42).toInt();
    m_split->setSizes({std::max(15, height() - 19 - lower - m_split->handleWidth()), lower});
    setSpeedRange(s.value(QStringLiteral("opLoSpeed"), 200).toInt(), s.value(QStringLiteral("opHiSpeed"), 500).toInt());
    setVisible(s.value(QStringLiteral("opVisible"), false).toBool());
}

void LiveStatsWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("opWinLeft"), x());
    s.setValue(QStringLiteral("opWinTop"), y());
    s.setValue(QStringLiteral("opWinWidth"), width());
    s.setValue(QStringLiteral("opWinHeight"), height());
    s.setValue(QStringLiteral("opSect2Height"), m_split->sizes().value(1));
    s.setValue(QStringLiteral("opVisible"), isVisible());
    s.setValue(QStringLiteral("opLoSpeed"), m_lo);
    s.setValue(QStringLiteral("opHiSpeed"), m_hi);
}

void LiveStatsWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    emit visibilityChanged(true);
}

void LiveStatsWindow::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    emit visibilityChanged(false);
}
