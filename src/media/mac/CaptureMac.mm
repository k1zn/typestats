// macOS: AVFoundation (re/webcam.md). The camera and the microphone ask for the user's permission the first time
// (NSCameraUsageDescription / NSMicrophoneUsageDescription in Info.plist); frames are NV12 from the session, timed by
// the host clock and brought to steady_clock by their age.

#include "media/Capture.h"
#include "platform/HookClock.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <QCoreApplication>

#include <atomic>
#include <cstring>

namespace {

// How long ago the sample was taken (its presentation time on the host clock).
qint64 ageUs(CMSampleBufferRef sample)
{
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    const CMTime now = CMClockGetTime(CMClockGetHostTimeClock());
    if (!CMTIME_IS_VALID(pts))
        return 0;
    const double age = CMTimeGetSeconds(CMTimeSubtract(now, pts));
    return age > 0 && age < 2 ? qint64(age * 1e6) : 0;
}

NSArray<AVCaptureDevice *> *devicesOf(AVMediaType type)
{
    NSMutableArray<AVCaptureDeviceType> *types = [NSMutableArray array];
    if (type == AVMediaTypeVideo) {
        [types addObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
        if (@available(macOS 14.0, *))
            [types addObject:AVCaptureDeviceTypeExternal];
        else
            [types addObject:AVCaptureDeviceTypeExternalUnknown];
    } else {
        if (@available(macOS 14.0, *))
            [types addObject:AVCaptureDeviceTypeMicrophone];
        else
            [types addObject:AVCaptureDeviceTypeBuiltInMicrophone];
        if (@available(macOS 14.0, *))
            [types addObject:AVCaptureDeviceTypeExternal];
        else
            [types addObject:AVCaptureDeviceTypeExternalUnknown];
    }
    AVCaptureDeviceDiscoverySession *s =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                               mediaType:type
                                                                position:AVCaptureDevicePositionUnspecified];
    return s.devices;
}

QList<CaptureDevice> listOf(AVMediaType type)
{
    QList<CaptureDevice> out;
    @autoreleasepool {
        for (AVCaptureDevice *d in devicesOf(type))
            out.append({QString::fromNSString(d.uniqueID), QString::fromNSString(d.localizedName)});
    }
    return out;
}

AVCaptureDevice *pick(AVMediaType type, const QString &id)
{
    if (!id.isEmpty())
        if (AVCaptureDevice *d = [AVCaptureDevice deviceWithUniqueID:id.toNSString()])
            return d;
    return [AVCaptureDevice defaultDeviceWithMediaType:type];
}

} // namespace

@interface TsCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate,
                                         AVCaptureAudioDataOutputSampleBufferDelegate>
@property(nonatomic, assign) Camera *camera;
@property(nonatomic, assign) Microphone *microphone;
@end

@implementation TsCaptureDelegate
- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sample
           fromConnection:(AVCaptureConnection *)connection
{
    Q_UNUSED(output);
    Q_UNUSED(connection);
    const qint64 at = hookNowUs() - ageUs(sample);
    if (self.camera) {
        CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
        if (!image || CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
            return;
        const int w = int(CVPixelBufferGetWidth(image)), h = int(CVPixelBufferGetHeight(image));
        const auto *y = static_cast<const uchar *>(CVPixelBufferGetBaseAddressOfPlane(image, 0));
        const auto *uv = static_cast<const uchar *>(CVPixelBufferGetBaseAddressOfPlane(image, 1));
        const I420Frame frame = Yuv::fromNv12(y, int(CVPixelBufferGetBytesPerRowOfPlane(image, 0)), uv,
                                              int(CVPixelBufferGetBytesPerRowOfPlane(image, 1)), w, h);
        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        emit self.camera->frame(frame, at);
    } else if (self.microphone) {
        CMBlockBufferRef block = nullptr;
        AudioBufferList list;
        if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample, nullptr, &list, sizeof list, nullptr, nullptr, 0,
                                                                    &block)
            != noErr)
            return;
        const CMAudioFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
        const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format);
        if (asbd && list.mNumberBuffers > 0 && asbd->mBitsPerChannel == 32 && (asbd->mFormatFlags & kAudioFormatFlagIsFloat)) {
            const int channels = int(asbd->mChannelsPerFrame);
            const int frames = int(list.mBuffers[0].mDataByteSize / (4 * channels));
            QVector<float> pcm(qsizetype(frames) * channels);
            std::memcpy(pcm.data(), list.mBuffers[0].mData, size_t(pcm.size()) * 4);
            emit self.microphone->samples(pcm, channels, int(asbd->mSampleRate), at);
        }
        if (block)
            CFRelease(block);
    }
}
@end

// --- camera ---

struct Camera::Impl
{
    AVCaptureSession *session = nil;
    TsCaptureDelegate *delegate = nil;
    dispatch_queue_t queue = nullptr;
    std::atomic<bool> active{false};
};

Camera::Camera(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}

Camera::~Camera()
{
    stop();
}

QList<CaptureDevice> Camera::devices()
{
    return listOf(AVMediaTypeVideo);
}

void Camera::start(const QString &deviceId, QSize size, int fps)
{
    stop();
    d->active = true;
    auto begin = [this, deviceId, size, fps] {
        @autoreleasepool {
            AVCaptureDevice *device = pick(AVMediaTypeVideo, deviceId);
            NSError *error = nil;
            AVCaptureDeviceInput *input = device ? [AVCaptureDeviceInput deviceInputWithDevice:device error:&error] : nil;
            if (!input) {
                d->active = false;
                emit failed(QCoreApplication::translate("Capture", "Устройство занято или недоступно"));
                return;
            }
            d->session = [[AVCaptureSession alloc] init];
            NSString *preset = size.height() <= 240 ? AVCaptureSessionPreset320x240
                               : size.height() <= 480 && size.width() <= 640 ? AVCaptureSessionPreset640x480
                                                                              : AVCaptureSessionPreset1280x720;
            if ([d->session canSetSessionPreset:preset])
                d->session.sessionPreset = preset;
            [d->session addInput:input];
            AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
            output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
            output.alwaysDiscardsLateVideoFrames = YES;
            d->delegate = [[TsCaptureDelegate alloc] init];
            d->delegate.camera = this;
            d->queue = dispatch_queue_create("typestats.camera", DISPATCH_QUEUE_SERIAL);
            [output setSampleBufferDelegate:d->delegate queue:d->queue];
            if (![d->session canAddOutput:output]) {
                d->active = false;
                emit failed(QCoreApplication::translate("Capture", "Камера не отдаёт кадры в нужном формате"));
                return;
            }
            [d->session addOutput:output];
            if ([device lockForConfiguration:nil]) { // the rate asked for, when the camera has it
                for (AVFrameRateRange *r in device.activeFormat.videoSupportedFrameRateRanges)
                    if (r.minFrameRate <= fps && fps <= r.maxFrameRate) {
                        device.activeVideoMinFrameDuration = CMTimeMake(1, fps);
                        break;
                    }
                [device unlockForConfiguration];
            }
            [d->session startRunning];
            emit started(size);
        }
    };
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized:
        begin();
        break;
    case AVAuthorizationStatusNotDetermined: { // braced: the block below lives in this scope only
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     QMetaObject::invokeMethod(this, [this, granted, begin] {
                                         if (granted && d->active)
                                             begin();
                                         else if (!granted) {
                                             d->active = false;
                                             emit failed(QCoreApplication::translate(
                                                 "Capture", "Нет доступа к камере (Системные настройки → Конфиденциальность)"));
                                         }
                                     });
                                 }];
        break;
    }
    default:
        d->active = false;
        emit failed(QCoreApplication::translate("Capture", "Нет доступа к камере (Системные настройки → Конфиденциальность)"));
    }
}

void Camera::stop()
{
    if (d->session) {
        [d->session stopRunning];
        d->delegate.camera = nullptr;
        if (d->queue)
            dispatch_sync(d->queue, ^{}); // the delegate's last frame is done
        d->session = nil;
        d->delegate = nil;
        d->queue = nullptr;
    }
    d->active = false;
}

bool Camera::isActive() const
{
    return d->active;
}

// --- microphone ---

struct Microphone::Impl
{
    AVCaptureSession *session = nil;
    TsCaptureDelegate *delegate = nil;
    dispatch_queue_t queue = nullptr;
    std::atomic<bool> active{false};
};

Microphone::Microphone(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}

Microphone::~Microphone()
{
    stop();
}

QList<CaptureDevice> Microphone::devices()
{
    return listOf(AVMediaTypeAudio);
}

void Microphone::start(const QString &deviceId)
{
    stop();
    d->active = true;
    auto begin = [this, deviceId] {
        @autoreleasepool {
            AVCaptureDevice *device = pick(AVMediaTypeAudio, deviceId);
            AVCaptureDeviceInput *input = device ? [AVCaptureDeviceInput deviceInputWithDevice:device error:nil] : nil;
            if (!input) {
                d->active = false;
                emit failed(QCoreApplication::translate("Capture", "Устройство занято или недоступно"));
                return;
            }
            d->session = [[AVCaptureSession alloc] init];
            [d->session addInput:input];
            AVCaptureAudioDataOutput *output = [[AVCaptureAudioDataOutput alloc] init];
            output.audioSettings = @{
                AVFormatIDKey : @(kAudioFormatLinearPCM),
                AVLinearPCMBitDepthKey : @32,
                AVLinearPCMIsFloatKey : @YES,
                AVLinearPCMIsNonInterleaved : @NO,
                AVSampleRateKey : @48000,
                AVNumberOfChannelsKey : @1,
            };
            d->delegate = [[TsCaptureDelegate alloc] init];
            d->delegate.microphone = this;
            d->queue = dispatch_queue_create("typestats.microphone", DISPATCH_QUEUE_SERIAL);
            [output setSampleBufferDelegate:d->delegate queue:d->queue];
            [d->session addOutput:output];
            [d->session startRunning];
        }
    };
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized:
        begin();
        break;
    case AVAuthorizationStatusNotDetermined: { // braced: the block below lives in this scope only
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
                                     QMetaObject::invokeMethod(this, [this, granted, begin] {
                                         if (granted && d->active)
                                             begin();
                                         else if (!granted) {
                                             d->active = false;
                                             emit failed(QCoreApplication::translate(
                                                 "Capture", "Нет доступа к микрофону (Системные настройки → Конфиденциальность)"));
                                         }
                                     });
                                 }];
        break;
    }
    default:
        d->active = false;
        emit failed(QCoreApplication::translate("Capture", "Нет доступа к микрофону (Системные настройки → Конфиденциальность)"));
    }
}

void Microphone::stop()
{
    if (d->session) {
        [d->session stopRunning];
        d->delegate.microphone = nullptr;
        if (d->queue)
            dispatch_sync(d->queue, ^{});
        d->session = nil;
        d->delegate = nil;
        d->queue = nullptr;
    }
    d->active = false;
}

bool Microphone::isActive() const
{
    return d->active;
}
