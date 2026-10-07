// Linux: the camera through V4L2 (mmap buffers, frames timed by the driver on CLOCK_MONOTONIC - steady_clock), the
// microphone through PulseAudio's simple API (re/webcam.md).

#include "media/Capture.h"
#include "platform/HookClock.h"
#include "PulseSimple.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do
        r = ioctl(fd, request, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

// A capture device: /dev/videoN that captures video (not the metadata nodes UVC cameras add).
bool isCamera(int fd, QString *name)
{
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return false;
    const quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING))
        return false;
    if (name)
        *name = QString::fromUtf8(reinterpret_cast<const char *>(cap.card));
    return true;
}

QStringList videoNodes()
{
    QStringList out;
    const QStringList names = QDir(QStringLiteral("/dev")).entryList({QStringLiteral("video*")}, QDir::System, QDir::Name);
    for (const QString &n : names)
        out.append(QStringLiteral("/dev/") + n);
    return out;
}

} // namespace

// --- camera ---

struct Camera::Impl
{
    Camera *q = nullptr;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> active{false};

    void run(QString id, QSize want, int fps);
};

void Camera::Impl::run(QString id, QSize want, int fps)
{
    const auto fail = [this](const QString &reason) {
        emit q->failed(reason);
        active = false;
    };
    QString path = id;
    if (path.isEmpty()) {
        const QList<CaptureDevice> all = Camera::devices();
        if (all.isEmpty())
            return fail(QCoreApplication::translate("Capture", "Устройство не найдено"));
        path = all.first().id;
    }
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return fail(QCoreApplication::translate("Capture", "Устройство занято или недоступно") + QStringLiteral(" (")
                    + QString::fromLocal8Bit(std::strerror(errno)) + u')');
    // YUYV is what every UVC camera gives; NV12 some others; MJPEG - cameras that have nothing else at this size.
    v4l2_format fmt{};
    quint32 chosen = 0;
    for (quint32 pixel : {quint32(V4L2_PIX_FMT_YUYV), quint32(V4L2_PIX_FMT_NV12), quint32(V4L2_PIX_FMT_MJPEG)}) {
        fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = quint32(want.width());
        fmt.fmt.pix.height = quint32(want.height());
        fmt.fmt.pix.pixelformat = pixel;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == pixel) {
            chosen = pixel;
            break;
        }
    }
    if (!chosen) {
        ::close(fd);
        return fail(QCoreApplication::translate("Capture", "Камера не отдаёт кадры в нужном формате"));
    }
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe = {1, quint32(fps)};
    xioctl(fd, VIDIOC_S_PARM, &parm); // a wish: the camera may keep its own rate

    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    struct Mapped
    {
        void *data = MAP_FAILED;
        size_t length = 0;
    };
    QList<Mapped> buffers;
    bool ok = xioctl(fd, VIDIOC_REQBUFS, &req) == 0 && req.count >= 2;
    for (quint32 i = 0; ok && i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        ok = xioctl(fd, VIDIOC_QUERYBUF, &b) == 0;
        if (!ok)
            break;
        Mapped m;
        m.length = b.length;
        m.data = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        ok = m.data != MAP_FAILED && xioctl(fd, VIDIOC_QBUF, &b) == 0;
        buffers.append(m);
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ok = ok && xioctl(fd, VIDIOC_STREAMON, &type) == 0;
    if (!ok) {
        for (const Mapped &m : buffers)
            if (m.data != MAP_FAILED)
                munmap(m.data, m.length);
        ::close(fd);
        return fail(QCoreApplication::translate("Capture", "Камера не отдаёт кадры в нужном формате"));
    }
    const int w = int(fmt.fmt.pix.width), h = int(fmt.fmt.pix.height), stride = int(fmt.fmt.pix.bytesperline);
    emit q->started(QSize(w, h));
    while (!stop) {
        pollfd p{fd, POLLIN, 0};
        const int r = poll(&p, 1, 200);
        if (r < 0 && errno != EINTR) {
            emit q->failed(QCoreApplication::translate("Capture", "Камера отключилась"));
            break;
        }
        if (r <= 0)
            continue;
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN)
                continue;
            emit q->failed(QCoreApplication::translate("Capture", "Камера отключилась"));
            break;
        }
        // The driver's time of the frame, when it is on the monotonic clock (steady_clock's); else now.
        qint64 at = hookNowUs();
        if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
            const qint64 t = qint64(b.timestamp.tv_sec) * 1000000 + b.timestamp.tv_usec;
            if (t <= at && at - t < 2000000)
                at = t;
        }
        const uchar *data = static_cast<const uchar *>(buffers[int(b.index)].data);
        I420Frame frame;
        if (chosen == V4L2_PIX_FMT_YUYV)
            frame = Yuv::fromYuyv(data, stride ? stride : w * 2, w, h);
        else if (chosen == V4L2_PIX_FMT_NV12)
            frame = Yuv::fromNv12(data, stride ? stride : w, data + qsizetype(stride ? stride : w) * h, stride ? stride : w, w, h);
        else
            frame = Yuv::fromImage(QImage::fromData(data, int(b.bytesused), "JPEG"));
        xioctl(fd, VIDIOC_QBUF, &b);
        if (!frame.isNull())
            emit q->frame(frame, at);
    }
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (const Mapped &m : buffers)
        munmap(m.data, m.length);
    ::close(fd);
    active = false;
}

Camera::Camera(QObject *parent) : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
}

Camera::~Camera()
{
    stop();
}

QList<CaptureDevice> Camera::devices()
{
    QList<CaptureDevice> out;
    for (const QString &path : videoNodes()) {
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;
        QString name;
        if (isCamera(fd, &name))
            out.append({path, name});
        ::close(fd);
    }
    return out;
}

void Camera::start(const QString &deviceId, QSize size, int fps)
{
    stop();
    d->stop = false;
    d->active = true;
    d->thread = std::thread([this, deviceId, size, fps] { d->run(deviceId, size, fps); });
}

void Camera::stop()
{
    d->stop = true;
    if (d->thread.joinable())
        d->thread.join();
    d->active = false;
}

bool Camera::isActive() const
{
    return d->active;
}

// --- microphone ---

struct Microphone::Impl
{
    Microphone *q = nullptr;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> active{false};

    void run(QString id);
};

void Microphone::Impl::run(QString id)
{
    const Pulse::Api &pa = Pulse::Api::get();
    if (!pa.ok) {
        emit q->failed(QCoreApplication::translate("Capture", "Нет PulseAudio (libpulse-simple)"));
        active = false;
        return;
    }
    constexpr int kRate = 48000, kChunk = 960; // 20 ms
    const Pulse::SampleSpec spec{Pulse::Float32LE, kRate, 1};
    const Pulse::BufferAttr attr{quint32(-1), quint32(-1), quint32(-1), quint32(-1), kChunk * 4};
    int error = 0;
    const QByteArray dev = id.toUtf8();
    Pulse::Simple *s = pa.open(nullptr, "Typing statistics", Pulse::Record, dev.isEmpty() ? nullptr : dev.constData(),
                               "webcam", &spec, nullptr, &attr, &error);
    if (!s) {
        emit q->failed(QCoreApplication::translate("Capture", "Микрофон не отдаёт звук в нужном формате"));
        active = false;
        return;
    }
    QVector<float> pcm(kChunk);
    while (!stop) {
        if (pa.read(s, pcm.data(), size_t(pcm.size()) * 4, &error) < 0) {
            emit q->failed(QCoreApplication::translate("Capture", "Микрофон отключился"));
            break;
        }
        // The chunk ends now: its first sample is its duration ago.
        emit q->samples(pcm, 1, kRate, hookNowUs() - qint64(kChunk) * 1000000 / kRate);
    }
    pa.free(s);
    active = false;
}

Microphone::Microphone(QObject *parent) : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
}

Microphone::~Microphone()
{
    stop();
}

QList<CaptureDevice> Microphone::devices()
{
    return {}; // the simple API has no list: the default source (pavucontrol chooses it)
}

void Microphone::start(const QString &deviceId)
{
    stop();
    d->stop = false;
    d->active = true;
    d->thread = std::thread([this, deviceId] { d->run(deviceId); });
}

void Microphone::stop()
{
    d->stop = true;
    if (d->thread.joinable())
        d->thread.join();
    d->active = false;
}

bool Microphone::isActive() const
{
    return d->active;
}
