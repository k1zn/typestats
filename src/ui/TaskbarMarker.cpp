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
// A red dot with a darker rim, `size` px.
QImage dot(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal rim = size / 12.0;
    p.setPen(QPen(QColor(140, 0, 0), rim));
    p.setBrush(QColor(230, 20, 20));
    p.drawEllipse(QRectF(rim / 2, rim / 2, size - rim, size - rim));
    return img;
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
    static const HICON icon = dot(32).toHICON(); // drawn at the size of a small icon
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
        QPainter p(&img);
        const int d = size / 2;
        p.drawImage(size - d, size - d, dot(d));
        p.end();
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
