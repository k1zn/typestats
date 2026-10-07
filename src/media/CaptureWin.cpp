// Windows: Media Foundation source readers (re/webcam.md). Media Foundation is loaded at run time: Windows N has
// none without its Media Feature Pack, and the program must start there all the same.

#include "Capture.h"
#include "CaptureDShow.h"
#include "platform/HookClock.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <QCoreApplication>

#include <atomic>
#include <cmath>
#include <thread>

namespace {

// The time the device took the picture, QPC in 100 ns (Windows 10+; mfcaptureengine.h).
const GUID kDeviceReferenceSystemTime = {0x6523775a, 0xba2d, 0x405f, {0xb2, 0xc5, 0x01, 0xff, 0x88, 0xe2, 0xe8, 0xf6}};

// A function of a DLL loaded at run time (through void (*)(): a cast between function types GCC accepts).
template <typename F>
F resolve(HMODULE module, const char *name)
{
    return reinterpret_cast<F>(reinterpret_cast<void (*)()>(GetProcAddress(module, name)));
}

struct Mf
{
    HRESULT(WINAPI *startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI *shutdown)() = nullptr;
    HRESULT(WINAPI *createAttributes)(IMFAttributes **, UINT32) = nullptr;
    HRESULT(WINAPI *createMediaType)(IMFMediaType **) = nullptr;
    HRESULT(WINAPI *enumDeviceSources)(IMFAttributes *, IMFActivate ***, UINT32 *) = nullptr;
    HRESULT(WINAPI *createSourceReader)(IMFMediaSource *, IMFAttributes *, IMFSourceReader **) = nullptr;
    bool ok = false;

    static const Mf &get()
    {
        static const Mf mf = [] {
            Mf m;
            HMODULE plat = LoadLibraryW(L"mfplat.dll"), mf = LoadLibraryW(L"mf.dll"), rw = LoadLibraryW(L"mfreadwrite.dll");
            if (!plat || !mf || !rw)
                return m;
            m.startup = resolve<decltype(m.startup)>(plat, "MFStartup");
            m.shutdown = resolve<decltype(m.shutdown)>(plat, "MFShutdown");
            m.createAttributes = resolve<decltype(m.createAttributes)>(plat, "MFCreateAttributes");
            m.createMediaType = resolve<decltype(m.createMediaType)>(plat, "MFCreateMediaType");
            m.enumDeviceSources = resolve<decltype(m.enumDeviceSources)>(mf, "MFEnumDeviceSources");
            m.createSourceReader =
                resolve<decltype(m.createSourceReader)>(rw, "MFCreateSourceReaderFromMediaSource");
            m.ok = m.startup && m.shutdown && m.createAttributes && m.createMediaType && m.enumDeviceSources
                   && m.createSourceReader;
            return m;
        }();
        return mf;
    }
};

template <typename T>
struct Com
{
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { reset(); }
    void reset()
    {
        if (p)
            p->Release();
        p = nullptr;
    }
    T **out()
    {
        reset();
        return &p;
    }
    T *operator->() const { return p; }
    explicit operator bool() const { return p; }
};

// COM and Media Foundation for the capture thread.
struct MfSession
{
    bool ok = false;
    bool com = false;
    MfSession()
    {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ok = Mf::get().ok && SUCCEEDED(Mf::get().startup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfSession()
    {
        if (ok)
            Mf::get().shutdown();
        if (com)
            CoUninitialize();
    }
};

QString allocatedString(IMFActivate *a, REFGUID key)
{
    WCHAR *s = nullptr;
    UINT32 n = 0;
    if (FAILED(a->GetAllocatedString(key, &s, &n)))
        return {};
    const QString out = QString::fromWCharArray(s, int(n));
    CoTaskMemFree(s);
    return out;
}

struct Activations
{
    IMFActivate **list = nullptr;
    UINT32 count = 0;
    ~Activations()
    {
        for (UINT32 i = 0; i < count; ++i)
            list[i]->Release();
        CoTaskMemFree(list);
    }
};

bool enumerate(REFGUID type, Activations &out)
{
    Com<IMFAttributes> attr;
    if (FAILED(Mf::get().createAttributes(attr.out(), 1)) || FAILED(attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, type)))
        return false;
    return SUCCEEDED(Mf::get().enumDeviceSources(attr.p, &out.list, &out.count));
}

QList<CaptureDevice> devicesOf(REFGUID type, REFGUID idKey)
{
    QList<CaptureDevice> out;
    MfSession mf;
    if (!mf.ok)
        return out;
    Activations a;
    if (!enumerate(type, a))
        return out;
    for (UINT32 i = 0; i < a.count; ++i)
        out.append({allocatedString(a.list[i], idKey), allocatedString(a.list[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
    return out;
}

// The source of the device with this id (empty: the first one).
bool openSource(REFGUID type, REFGUID idKey, const QString &id, Com<IMFMediaSource> &source, QString &error)
{
    Activations a;
    if (!enumerate(type, a) || a.count == 0) {
        error = QCoreApplication::translate("Capture", "Устройство не найдено");
        return false;
    }
    UINT32 pick = 0;
    for (UINT32 i = 0; i < a.count; ++i)
        if (!id.isEmpty() && allocatedString(a.list[i], idKey) == id)
            pick = i;
    if (FAILED(a.list[pick]->ActivateObject(IID_PPV_ARGS(source.out())))) {
        error = QCoreApplication::translate("Capture", "Устройство занято или недоступно");
        return false;
    }
    return true;
}

qint64 qpc100ns()
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return qint64(double(c.QuadPart) * 1e7 / double(f.QuadPart));
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
    if (DShow::isId(id)) {
        DShow::run(q, id, want, fps, stop);
        active = false;
        return;
    }
    MfSession mf;
    QString error;
    Com<IMFMediaSource> source;
    if (!mf.ok) {
        emit q->failed(QCoreApplication::translate("Capture", "Нет Media Foundation (Windows N: нужен Media Feature Pack)"));
        active = false;
        return;
    }
    if (!openSource(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    id, source, error)) {
        emit q->failed(error);
        active = false;
        return;
    }
    Com<IMFAttributes> attr;
    Mf::get().createAttributes(attr.out(), 2);
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    Com<IMFSourceReader> reader;
    const DWORD stream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    bool ok = SUCCEEDED(Mf::get().createSourceReader(source.p, attr.p, reader.out()));
    if (ok) {
        // The native format nearest to what is asked: the smallest one at least as large (else the largest), at the
        // rate nearest to the one asked for and not below it when possible.
        Com<IMFMediaType> best;
        double bestScore = 1e18;
        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> t;
            if (FAILED(reader->GetNativeMediaType(stream, i, t.out())))
                break;
            UINT32 w = 0, h = 0, num = 0, den = 1;
            MFGetAttributeSize(t.p, MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(t.p, MF_MT_FRAME_RATE, &num, &den);
            const double rate = den ? double(num) / den : 0;
            const bool large = int(w) >= want.width() && int(h) >= want.height();
            double score = large ? double(w) * h : 1e12 - double(w) * h;
            score += rate >= fps ? (rate - fps) * 1000 : 1e9 + (fps - rate) * 1000;
            if (score < bestScore) {
                bestScore = score;
                best.reset();
                best.p = t.p;
                t.p = nullptr;
            }
        }
        if (best)
            reader->SetCurrentMediaType(stream, nullptr, best.p);
        Com<IMFMediaType> out;
        Mf::get().createMediaType(out.out());
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        ok = SUCCEEDED(reader->SetCurrentMediaType(stream, nullptr, out.p));
    }
    UINT32 w = 0, h = 0;
    LONG strideValue = 0;
    if (ok) {
        Com<IMFMediaType> current;
        ok = SUCCEEDED(reader->GetCurrentMediaType(stream, current.out()));
        if (ok) {
            MFGetAttributeSize(current.p, MF_MT_FRAME_SIZE, &w, &h);
            UINT32 s = 0;
            strideValue = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) ? LONG(s) : LONG(w);
        }
    }
    if (!ok || w < 2 || h < 2) {
        emit q->failed(QCoreApplication::translate("Capture", "Камера не отдаёт кадры в нужном формате"));
        source->Shutdown();
        active = false;
        return;
    }
    emit q->started(QSize(int(w), int(h)));
    const int stride = std::abs(int(strideValue)) < int(w) ? int(w) : std::abs(int(strideValue));
    while (!stop) {
        DWORD index = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, &index, &flags, &ts, sample.out()))
            || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            if (!stop)
                emit q->failed(QCoreApplication::translate("Capture", "Камера отключилась"));
            break;
        }
        if (!sample)
            continue;
        qint64 at = hookNowUs();
        UINT64 ref = 0;
        if (SUCCEEDED(sample->GetUINT64(kDeviceReferenceSystemTime, &ref)) && ref) {
            const qint64 age = (qpc100ns() - qint64(ref)) / 10;
            if (age >= 0 && age < 2000000)
                at -= age;
        }
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.out())))
            continue;
        BYTE *data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length)))
            continue;
        if (length >= DWORD(stride) * h * 3 / 2)
            emit q->frame(Yuv::fromNv12(data, stride, data + qsizetype(stride) * h, stride, int(w), int(h)), at);
        buffer->Unlock();
    }
    reader.reset();
    source->Shutdown();
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
    return devicesOf(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)
           + DShow::cameras();
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
    MfSession mf;
    QString error;
    Com<IMFMediaSource> source;
    if (!mf.ok || !openSource(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID,
                              MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, id, source, error)) {
        emit q->failed(mf.ok ? error : QCoreApplication::translate("Capture", "Нет Media Foundation"));
        active = false;
        return;
    }
    Com<IMFSourceReader> reader;
    const DWORD stream = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    bool ok = SUCCEEDED(Mf::get().createSourceReader(source.p, nullptr, reader.out()));
    if (ok) {
        Com<IMFMediaType> out;
        Mf::get().createMediaType(out.out());
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        ok = SUCCEEDED(reader->SetCurrentMediaType(stream, nullptr, out.p));
    }
    UINT32 channels = 0, rate = 0;
    if (ok) {
        Com<IMFMediaType> current;
        ok = SUCCEEDED(reader->GetCurrentMediaType(stream, current.out()))
             && SUCCEEDED(current->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels))
             && SUCCEEDED(current->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
    }
    if (!ok || channels == 0 || rate == 0) {
        emit q->failed(QCoreApplication::translate("Capture", "Микрофон не отдаёт звук в нужном формате"));
        source->Shutdown();
        active = false;
        return;
    }
    while (!stop) {
        DWORD index = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, &index, &flags, &ts, sample.out()))
            || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            if (!stop)
                emit q->failed(QCoreApplication::translate("Capture", "Микрофон отключился"));
            break;
        }
        if (!sample)
            continue;
        const qint64 now = hookNowUs();
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.out())))
            continue;
        BYTE *data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length)))
            continue;
        const int frames = int(length / (4 * channels));
        QVector<float> pcm(qsizetype(frames) * channels);
        std::memcpy(pcm.data(), data, size_t(pcm.size()) * 4);
        buffer->Unlock();
        // The buffer ends now: its first sample is its duration ago.
        emit q->samples(pcm, int(channels), int(rate), now - qint64(frames) * 1000000 / rate);
    }
    reader.reset();
    source->Shutdown();
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
    return devicesOf(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID);
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
