#include "TaskbarMarker.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCoreApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QWidget>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shobjidl.h>
#endif

#ifdef Q_OS_MACOS
namespace TaskbarMarker::detail {
void dockBadge(bool on); // mac/TaskbarMarkerMac.mm
}
#endif

namespace {

QHash<const QWidget *, bool> &states()
{
    static QHash<const QWidget *, bool> s;
    return s;
}

#ifndef Q_OS_MACOS
// A red dot with a darker rim, `diameter` px, in the bottom right corner of `img`.
void drawDot(QImage &img, qreal diameter)
{
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal rim = std::max(1.0, diameter / 10.0);
    p.setPen(QPen(QColor(140, 0, 0), rim));
    p.setBrush(QColor(230, 20, 20));
    const qreal d = diameter - rim;
    p.drawEllipse(QRectF(img.width() - rim / 2 - d, img.height() - rim / 2 - d, d, d));
}
#endif

#ifdef Q_OS_WIN
ITaskbarList3 *taskbar()
{
    static ITaskbarList3 *list = [] {
        ITaskbarList3 *t = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t))))
            return static_cast<ITaskbarList3 *>(nullptr);
        if (FAILED(t->HrInit())) {
            t->Release();
            return static_cast<ITaskbarList3 *>(nullptr);
        }
        return t;
    }();
    return list;
}

void apply(HWND hwnd, bool on)
{
    ITaskbarList3 *list = taskbar();
    if (!list)
        return;
    if (!on) {
        list->SetOverlayIcon(hwnd, nullptr, nullptr);
        return;
    }
    // The overlay covers the bottom right quarter of the button's icon: the dot takes the corner of it.
    static const HICON icon = [] {
        QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        drawDot(img, 18);
        return img.toHICON();
    }();
    const std::wstring text = QCoreApplication::translate("TaskbarMarker", "Идёт запись").toStdWString();
    list->SetOverlayIcon(hwnd, icon, text.c_str());
}

// Explorer makes the button anew (the window shown again after the tray, Explorer restarted): its overlay is gone.
class ButtonCreated : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG")
            return false;
        static const UINT created = RegisterWindowMessageW(L"TaskbarButtonCreated");
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != created)
            return false;
        for (auto it = states().cbegin(); it != states().cend(); ++it)
            if (it.value() && HWND(it.key()->internalWinId()) == msg->hwnd)
                apply(msg->hwnd, true);
        return false;
    }
};
#endif

} // namespace

namespace TaskbarMarker {

void set(QWidget *window, bool on)
{
    if (!states().contains(window))
        QObject::connect(window, &QObject::destroyed, [window] { states().remove(window); });
    states()[window] = on;
#if defined(Q_OS_WIN)
    if (QGuiApplication::platformName() != QLatin1String("windows"))
        return; // offscreen (tests): no task bar
    static ButtonCreated *filter = [] {
        auto *f = new ButtonCreated;
        qApp->installNativeEventFilter(f);
        return f;
    }();
    Q_UNUSED(filter);
    if (const WId id = window->internalWinId()) // not created yet: the button is not there either
        apply(HWND(id), on);
#elif defined(Q_OS_MACOS)
    Q_UNUSED(window);
    if (QGuiApplication::platformName() == QLatin1String("cocoa"))
        detail::dockBadge(on);
#else
    if (!on) {
        window->setWindowIcon(QIcon()); // the application's
        return;
    }
    const QIcon base = QApplication::windowIcon();
    QIcon marked;
    for (const int size : {16, 24, 32, 48, 64}) {
        QImage img = base.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (img.isNull())
            continue;
        drawDot(img, size * 0.35);
        marked.addPixmap(QPixmap::fromImage(img));
    }
    window->setWindowIcon(marked);
#endif
}

bool isSet(const QWidget *window)
{
    return states().value(window, false);
}

} // namespace TaskbarMarker
