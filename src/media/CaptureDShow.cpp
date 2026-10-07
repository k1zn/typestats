// Windows: DirectShow capture for the cameras Media Foundation does not list (OBS Virtual Camera and the like: source
// filters registered in the video input category with no device behind them). A filter graph source → Sample
// Grabber → Null Renderer, with no clock, so that a frame comes as soon as the source gives it.

#include "CaptureDShow.h"
#include "platform/HookClock.h"

#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <qedit.h>

#include <QCoreApplication>
#include <QImage>

#include <algorithm>
#include <cstdlib>

namespace {

constexpr GUID fourcc(DWORD c)
{
    return {c, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
}

// The GUIDs of the filters and formats (qedit's are in no library; the interfaces come from strmiids).
const GUID kSystemDeviceEnum = {0x62be5d10, 0x60eb, 0x11d0, {0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};
const GUID kVideoInputCategory = {0x860bb310, 0x5d01, 0x11d0, {0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};
const GUID kFilterGraph = {0xe436ebb3, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kCaptureGraphBuilder2 = {0xbf87b6e1, 0x8c27, 0x11d0, {0xb3, 0xf0, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}};
const GUID kSampleGrabber = {0xc1f400a0, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const GUID kNullRenderer = {0xc1f400a4, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const GUID kPinCapture = {0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};
const GUID kFormatVideoInfo = {0x05589f80, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}};
const GUID kFormatVideoInfo2 = {0xf72a76a0, 0xeb0a, 0x11d0, {0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};
const GUID kVideo = fourcc(0x73646976); // vids
const GUID kNv12 = fourcc(0x3231564e), kI420 = fourcc(0x30323449), kIyuv = fourcc(0x56555949), kYuy2 = fourcc(0x32595559);
const GUID kRgb32 = {0xe436eb7e, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kRgb24 = {0xe436eb7d, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};

const QString kPrefix = QStringLiteral("dshow:");

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

struct ComInit
{
    bool ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); // the GUI thread has its COM already
    ~ComInit()
    {
        if (ok)
            CoUninitialize();
    }
};

void clearType(AM_MEDIA_TYPE &t)
{
    if (t.cbFormat)
        CoTaskMemFree(t.pbFormat);
    t.cbFormat = 0;
    t.pbFormat = nullptr;
    if (t.pUnk)
        t.pUnk->Release();
    t.pUnk = nullptr;
}

void deleteType(AM_MEDIA_TYPE *t)
{
    if (t) {
        clearType(*t);
        CoTaskMemFree(t);
    }
}

// VIDEOINFOHEADER and VIDEOINFOHEADER2 begin alike up to AvgTimePerFrame.
VIDEOINFOHEADER *videoInfo(const AM_MEDIA_TYPE &t)
{
    if (t.formattype == kFormatVideoInfo && t.cbFormat >= sizeof(VIDEOINFOHEADER))
        return reinterpret_cast<VIDEOINFOHEADER *>(t.pbFormat);
    if (t.formattype == kFormatVideoInfo2 && t.cbFormat >= sizeof(VIDEOINFOHEADER2))
        return reinterpret_cast<VIDEOINFOHEADER *>(t.pbFormat);
    return nullptr;
}

const BITMAPINFOHEADER *bitmapHeader(const AM_MEDIA_TYPE &t)
{
    if (t.formattype == kFormatVideoInfo && t.cbFormat >= sizeof(VIDEOINFOHEADER))
        return &reinterpret_cast<const VIDEOINFOHEADER *>(t.pbFormat)->bmiHeader;
    if (t.formattype == kFormatVideoInfo2 && t.cbFormat >= sizeof(VIDEOINFOHEADER2))
        return &reinterpret_cast<const VIDEOINFOHEADER2 *>(t.pbFormat)->bmiHeader;
    return nullptr;
}

bool readable(const GUID &subtype)
{
    return subtype == kNv12 || subtype == kI420 || subtype == kIyuv || subtype == kYuy2 || subtype == kRgb32
           || subtype == kRgb24;
}

QString displayName(IMoniker *m)
{
    Com<IBindCtx> ctx;
    LPOLESTR s = nullptr;
    if (FAILED(CreateBindCtx(0, ctx.out())) || FAILED(m->GetDisplayName(ctx.p, nullptr, &s)) || !s)
        return {};
    const QString out = QString::fromWCharArray(s);
    CoTaskMemFree(s);
    return out;
}

QString property(IPropertyBag *bag, const wchar_t *name)
{
    VARIANT v;
    VariantInit(&v);
    QString out;
    if (SUCCEEDED(bag->Read(name, &v, nullptr)) && v.vt == VT_BSTR)
        out = QString::fromWCharArray(v.bstrVal);
    VariantClear(&v);
    return out;
}

// The video sources DirectShow knows; f(moniker, properties) returns true to stop.
template <typename F>
void forEachSource(F f)
{
    Com<ICreateDevEnum> devices;
    Com<IEnumMoniker> list;
    if (FAILED(CoCreateInstance(kSystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(devices.out())))
        || devices->CreateClassEnumerator(kVideoInputCategory, list.out(), 0) != S_OK)
        return;
    for (;;) {
        Com<IMoniker> m;
        if (list->Next(1, m.out(), nullptr) != S_OK)
            break;
        Com<IPropertyBag> bag;
        if (SUCCEEDED(m->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(bag.out()))) && f(m.p, bag.p))
            break;
    }
}

// Frames from the Sample Grabber's thread. Owned by run(), which outlives the graph.
struct Grabber final : ISampleGrabberCB
{
    Camera *q = nullptr;
    GUID subtype{};
    int w = 0, h = 0;
    bool bottomUp = false;

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(ISampleGrabberCB)) {
            *out = static_cast<ISampleGrabberCB *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP BufferCB(double, BYTE *, LONG) override { return E_NOTIMPL; }
    STDMETHODIMP SampleCB(double, IMediaSample *sample) override
    {
        const qint64 at = hookNowUs(); // a virtual camera gives the frame as it makes it
        BYTE *data = nullptr;
        if (sample && SUCCEEDED(sample->GetPointer(&data)) && data)
            deliver(data, sample->GetActualDataLength(), at);
        return S_OK;
    }

    void deliver(const BYTE *d, long n, qint64 at)
    {
        if (subtype == kNv12 || subtype == kI420 || subtype == kIyuv) {
            const long need = long(w) * h * 3 / 2;
            if (n < need)
                return;
            int stride = w;
            const int padded = int(n * 2 / (3L * h)); // a source may pad the rows
            if (padded > w && long(padded) * h * 3 / 2 <= n)
                stride = padded & ~1;
            const BYTE *y = d, *chroma = d + qsizetype(stride) * h;
            if (subtype == kNv12) {
                emit q->frame(Yuv::fromNv12(y, stride, chroma, stride, w, h), at);
            } else {
                const int cs = stride / 2;
                emit q->frame(Yuv::fromPlanes(y, stride, chroma, cs, chroma + qsizetype(cs) * ((h + 1) / 2), cs, w, h), at);
            }
        } else if (subtype == kYuy2) {
            const int stride = std::max(2 * w, int(n / h));
            if (n >= long(stride) * h)
                emit q->frame(Yuv::fromYuyv(d, stride, w, h), at);
        } else if (subtype == kRgb32 || subtype == kRgb24) {
            const int bytes = subtype == kRgb32 ? 4 : 3;
            const int stride = (w * bytes + 3) & ~3; // DIB rows
            if (n < long(stride) * h)
                return;
            QImage image(d, w, h, stride, subtype == kRgb32 ? QImage::Format_RGB32 : QImage::Format_BGR888);
            emit q->frame(Yuv::fromImage(bottomUp ? image.mirrored() : image), at);
        }
    }
};

} // namespace

bool DShow::isId(const QString &id)
{
    return id.startsWith(kPrefix);
}

QList<CaptureDevice> DShow::cameras()
{
    ComInit com;
    QList<CaptureDevice> out;
    forEachSource([&](IMoniker *m, IPropertyBag *bag) {
        // A filter with a device path is a real camera: Media Foundation lists it itself.
        if (property(bag, L"DevicePath").isEmpty()) {
            const QString name = displayName(m);
            if (!name.isEmpty())
                out.append({kPrefix + name, property(bag, L"FriendlyName")});
        }
        return false;
    });
    return out;
}

void DShow::run(Camera *q, const QString &id, QSize want, int fps, const std::atomic<bool> &stop)
{
    ComInit com;
    const auto fail = [q](const char *text) { emit q->failed(QCoreApplication::translate("Capture", text)); };
    Grabber cb; // outlives the graph below
    cb.q = q;

    const QString name = id.mid(kPrefix.size());
    Com<IBaseFilter> source;
    forEachSource([&](IMoniker *m, IPropertyBag *) {
        if (displayName(m) != name)
            return false;
        m->BindToObject(nullptr, nullptr, IID_PPV_ARGS(source.out()));
        return true;
    });
    if (!source) {
        fail(QT_TRANSLATE_NOOP("Capture", "Устройство не найдено"));
        return;
    }
    Com<IGraphBuilder> graph;
    Com<ICaptureGraphBuilder2> builder;
    if (FAILED(CoCreateInstance(kFilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(graph.out())))
        || FAILED(CoCreateInstance(kCaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(builder.out())))
        || FAILED(builder->SetFiltergraph(graph.p)) || FAILED(graph->AddFilter(source.p, L"Source"))) {
        fail(QT_TRANSLATE_NOOP("Capture", "Устройство занято или недоступно"));
        return;
    }
    Com<IBaseFilter> grabberFilter, sink;
    Com<ISampleGrabber> grabber;
    if (FAILED(CoCreateInstance(kSampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(grabberFilter.out())))
        || FAILED(grabberFilter->QueryInterface(IID_PPV_ARGS(grabber.out())))
        || FAILED(CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(sink.out())))) {
        fail(QT_TRANSLATE_NOOP("Capture", "В Windows нет Sample Grabber DirectShow (qedit.dll)"));
        return;
    }

    // The format nearest to what is asked, as for Media Foundation; among equal ones - one read without a converter.
    GUID subtype = kRgb32; // when the camera has nothing read directly: DirectShow puts a decoder in between
    Com<IAMStreamConfig> config;
    if (SUCCEEDED(builder->FindInterface(&kPinCapture, &kVideo, source.p, IID_IAMStreamConfig,
                                         reinterpret_cast<void **>(config.out())))) {
        int count = 0, size = 0;
        if (SUCCEEDED(config->GetNumberOfCapabilities(&count, &size)) && size == int(sizeof(VIDEO_STREAM_CONFIG_CAPS))) {
            AM_MEDIA_TYPE *best = nullptr;
            VIDEO_STREAM_CONFIG_CAPS bestCaps{};
            double bestScore = 1e18;
            for (int i = 0; i < count; ++i) {
                AM_MEDIA_TYPE *t = nullptr;
                VIDEO_STREAM_CONFIG_CAPS caps{};
                if (FAILED(config->GetStreamCaps(i, &t, reinterpret_cast<BYTE *>(&caps))) || !t)
                    continue;
                const BITMAPINFOHEADER *bh = bitmapHeader(*t);
                if (!bh || t->majortype != kVideo) {
                    deleteType(t);
                    continue;
                }
                const double w = bh->biWidth, h = std::abs(bh->biHeight);
                const double rate = caps.MinFrameInterval > 0 ? 1e7 / double(caps.MinFrameInterval) : fps;
                const bool large = w >= want.width() && h >= want.height();
                double score = large ? w * h : 1e12 - w * h;
                score += rate >= fps ? (rate - fps) * 1000 : 1e9 + (fps - rate) * 1000;
                score += readable(t->subtype) ? 0 : 0.5;
                if (score < bestScore) {
                    bestScore = score;
                    deleteType(best);
                    best = t;
                    bestCaps = caps;
                } else {
                    deleteType(t);
                }
            }
            if (best) {
                if (VIDEOINFOHEADER *vi = videoInfo(*best)) {
                    const LONGLONG frame = LONGLONG(1e7 / fps);
                    vi->AvgTimePerFrame = std::clamp(frame, bestCaps.MinFrameInterval,
                                                     std::max(bestCaps.MinFrameInterval, bestCaps.MaxFrameInterval));
                }
                if (SUCCEEDED(config->SetFormat(best)) && readable(best->subtype))
                    subtype = best->subtype;
                deleteType(best);
            }
        }
    }

    AM_MEDIA_TYPE ask{};
    ask.majortype = kVideo;
    ask.subtype = subtype;
    AM_MEDIA_TYPE got{};
    bool ok = SUCCEEDED(grabber->SetMediaType(&ask)) && SUCCEEDED(graph->AddFilter(grabberFilter.p, L"Grabber"))
              && SUCCEEDED(graph->AddFilter(sink.p, L"Sink"))
              && SUCCEEDED(builder->RenderStream(&kPinCapture, &kVideo, source.p, grabberFilter.p, sink.p))
              && SUCCEEDED(grabber->GetConnectedMediaType(&got));
    if (ok) {
        const BITMAPINFOHEADER *bh = bitmapHeader(got);
        ok = bh && readable(got.subtype) && bh->biWidth >= 2 && std::abs(bh->biHeight) >= 2;
        if (ok) {
            cb.subtype = got.subtype;
            cb.w = int(bh->biWidth) & ~1;
            cb.h = int(std::abs(bh->biHeight)) & ~1;
            cb.bottomUp = bh->biHeight > 0 && (got.subtype == kRgb32 || got.subtype == kRgb24);
        }
        clearType(got);
    }
    if (!ok) {
        fail(QT_TRANSLATE_NOOP("Capture", "Камера не отдаёт кадры в нужном формате"));
        return;
    }
    grabber->SetBufferSamples(FALSE);
    grabber->SetOneShot(FALSE);
    grabber->SetCallback(&cb, 0);
    Com<IMediaFilter> filter;
    if (SUCCEEDED(graph->QueryInterface(IID_PPV_ARGS(filter.out()))))
        filter->SetSyncSource(nullptr); // no clock: frames are not held until their presentation time
    Com<IMediaControl> control;
    Com<IMediaEvent> events;
    if (FAILED(graph->QueryInterface(IID_PPV_ARGS(control.out())))
        || FAILED(graph->QueryInterface(IID_IMediaEvent, reinterpret_cast<void **>(events.out()))) || FAILED(control->Run())) {
        grabber->SetCallback(nullptr, 0);
        fail(QT_TRANSLATE_NOOP("Capture", "Устройство занято или недоступно"));
        return;
    }
    emit q->started(QSize(cb.w, cb.h));
    while (!stop) {
        long code = 0;
        LONG_PTR p1 = 0, p2 = 0;
        if (events->GetEvent(&code, &p1, &p2, 50) != S_OK)
            continue;
        events->FreeEventParams(code, p1, p2);
        if (code == EC_ERRORABORT || code == EC_DEVICE_LOST || code == EC_COMPLETE) {
            fail(QT_TRANSLATE_NOOP("Capture", "Камера отключилась"));
            break;
        }
    }
    control->Stop();
    grabber->SetCallback(nullptr, 0);
}
