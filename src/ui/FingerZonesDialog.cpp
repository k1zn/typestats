#include "FingerZonesDialog.h"

#include "Texts.h"
#include "platform/KeyboardHook.h"

#include <QMouseEvent>
#include <QPainter>

namespace {

struct Key
{
    float x, y, w; // in key units
    quint8 scan;
};

// The keys of the picture (0x59d878). Enter is drawn apart, the space bar is not drawn.
QVector<Key> makeKeys()
{
    QVector<Key> keys;
    const quint8 top[] = {0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x2B, 0x0E};
    for (int i = 0; i < 15; ++i)
        keys.append({float(i), 0.0f, 1.0f, top[i]});
    keys.append({0.0f, 1.0f, 1.5f, 0x0F});
    for (int i = 0; i < 12; ++i)
        keys.append({1.5f + float(i), 1.0f, 1.0f, quint8(0x10 + i)});
    keys.append({0.0f, 2.0f, 1.75f, 0x3A});
    for (int i = 0; i < 11; ++i)
        keys.append({1.75f + float(i), 2.0f, 1.0f, quint8(0x1E + i)});
    keys.append({0.0f, 3.0f, 2.25f, 0x2A});
    for (int i = 0; i < 10; ++i)
        keys.append({2.25f + float(i), 3.0f, 1.0f, quint8(0x2C + i)});
    keys.append({12.25f, 3.0f, 2.75f, 0x36});
    return keys;
}

constexpr float kMargin = 10.0f, kGap = 5.0f;
constexpr int kCorner = 11;
constexpr quint8 kEnter = 0x1C;

} // namespace

QColor FingerZonesDialog::fingerColor(int finger)
{
    static const QColor colors[] = {QColor(180, 240, 180), QColor(32, 209, 247), QColor(240, 165, 255),
                                    QColor(255, 200, 200), QColor(230, 40, 96),  QColor(202, 18, 248),
                                    QColor(6, 149, 255),   QColor(34, 172, 34),  QColor(255, 255, 255)};
    return colors[std::clamp(finger, 0, 8)];
}

// ---------------------------------------------------------------- the keyboard (PaintBox1)

class FingerZonesDialog::Keyboard : public QWidget
{
public:
    explicit Keyboard(FingerZonesDialog *dialog) : QWidget(dialog), m_dialog(dialog), m_keys(makeKeys())
    {
        setContextMenuPolicy(Qt::PreventContextMenu);
    }

    float unit() const { return (float(width()) - 2.0f * kMargin + kGap) / 15.0f; }

    QRect cell(float x, float y, float x2, float y2) const
    {
        const float u = unit();
        return QRect(QPoint(int(u * x + kMargin), int(u * y + kMargin)),
                     QPoint(int(x2 * u + kMargin - kGap) - 1, int(y2 * u + kMargin - kGap) - 1));
    }

    // 0x448e10: the scan code of the key at the point; 0 if there is none.
    quint8 scanAt(const QPoint &pos) const
    {
        for (const Key &k : m_keys)
            if (cell(k.x, k.y, k.x + k.w, k.y + 1.0f).contains(pos))
                return k.scan;
        if (cell(12.75f, 2.0f, 15.0f, 3.0f).contains(pos) || cell(13.5f, 1.0f, 15.0f, 3.0f).contains(pos))
            return kEnter;
        return 0;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        const FingerZones &zones = m_dialog->m_zones;
        p.setPen(Qt::black);
        p.setBrush(Qt::white);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        const QFont big(QStringLiteral("Times New Roman"), 16), small(QStringLiteral("Times New Roman"), 10);
        auto roundRect = [&p](const QRect &r, int pen) {
            p.setPen(QPen(Qt::black, pen));
            p.drawRoundedRect(r, kCorner / 2.0, kCorner / 2.0);
        };

        for (const Key &k : m_keys) {
            bool dead = false;
            QString name = KeyboardHook::layoutKeyName(k.scan, &dead);
            if (name == QLatin1String("[BackSpace]"))
                name = QStringLiteral("[BS]");
            else if (name == QLatin1String("[Unrecognized key]"))
                name.clear();
            const QRect r = cell(k.x, k.y, k.x + k.w, k.y + 1.0f);
            p.setBrush(fingerColor(zones.finger(k.scan)));
            roundRect(r, zones.isHome(k.scan) ? 4 : 1);
            if (dead) {
                p.setBrush(Qt::white);
                roundRect(r.adjusted(5, 5, -5, -5), 1);
            }
            p.setPen(Qt::black);
            p.setFont(name.size() < 2 ? big : small);
            p.drawText(r.adjusted(-20, 0, 20, 0), Qt::AlignCenter, name);
        }

        // Enter: two rounded rectangles and a patch over the joint.
        const QRect lower = cell(12.75f, 2.0f, 15.0f, 3.0f), upper = cell(13.5f, 1.0f, 15.0f, 3.0f);
        p.setBrush(fingerColor(zones.finger(kEnter)));
        roundRect(lower, 1);
        roundRect(upper, 1);
        p.fillRect(QRect(QPoint(upper.left(), lower.top()), QPoint(upper.left() + kCorner - 1, lower.bottom() - 1)),
                   fingerColor(zones.finger(kEnter)));
        const float u = unit();
        p.setFont(small);
        const QFontMetrics fm(small);
        const QString enter = QStringLiteral("[Enter]");
        p.drawText(int(u * 14.0f + kMargin) - fm.horizontalAdvance(enter) / 2,
                   int(u * 2.2f + kMargin) - fm.height() / 2 + fm.ascent(), enter);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        FingerZones &zones = m_dialog->m_zones;
        if (zones.readOnly() || (e->button() != Qt::LeftButton && e->button() != Qt::RightButton))
            return;
        const bool home = e->button() == Qt::RightButton;
        const quint8 scan = scanAt(e->position().toPoint());
        // The right button works on the ordinary keys only.
        if (scan == 0 || (home && scan == kEnter))
            return;
        zones.assign(scan, m_dialog->m_finger, home);
        update();
    }

private:
    FingerZonesDialog *m_dialog;
    QVector<Key> m_keys;
};

// ---------------------------------------------------------------- the fingers (PaintBox2)

class FingerZonesDialog::Palette : public QWidget
{
public:
    explicit Palette(FingerZonesDialog *dialog) : QWidget(dialog), m_dialog(dialog) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QStringList names = Texts::histogramNames().fingers;
        const float rowHeight = float(height()) / 9.0f;
        QFont font(QStringLiteral("Courier New"), 10);
        for (int i = 0; i < 9; ++i) {
            const int top = int(float(i) * rowHeight), bottom = int(float(i + 1) * rowHeight + 1.0f);
            p.setPen(Qt::black);
            p.setBrush(fingerColor(i));
            p.drawRect(QRect(0, top, width() - 1, bottom - top - 1));
            font.setBold(i == m_dialog->m_finger);
            p.setFont(font);
            const QFontMetrics fm(font);
            p.drawText(5, int(float(i) * rowHeight + (rowHeight - float(fm.height())) * 0.5f) + fm.ascent(), names.value(i));
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        m_dialog->m_finger = std::clamp(e->position().toPoint().y() * 9 / std::max(1, height()), 0, 8);
        update();
    }

private:
    FingerZonesDialog *m_dialog;
};

// ---------------------------------------------------------------- the dialog

FingerZonesDialog::FingerZonesDialog(const QString &name, const FingerZones &zones, QWidget *parent)
    : QDialog(parent), m_zones(zones)
{
    setWindowTitle(tr("Клавиатура") + QStringLiteral(" - ") + name);
    setFixedSize(802, 181);
    m_keyboard = new Keyboard(this);
    m_keyboard->setGeometry(1, 1, 635, 179);
    m_palette = new Palette(this);
    m_palette->setGeometry(636, 1, 165, 179);
}
