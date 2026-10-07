#include "Av1Codec.h"

#include <aom/aom_decoder.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include <aom/aomdx.h>

#include <algorithm>

struct Av1Encoder::Impl
{
    aom_codec_ctx_t ctx{};
    bool open = false;
    qint64 lastPts = 0;
    qint64 lastKeyPts = 0;
    bool started = false;
};

Av1Encoder::Av1Encoder() : d(std::make_unique<Impl>()) {}

Av1Encoder::~Av1Encoder()
{
    close();
}

bool Av1Encoder::isOpen() const
{
    return d->open;
}

bool Av1Encoder::open(const Av1Settings &s, QString *error)
{
    close();
    m_settings = s;
    m_settings.width &= ~1;
    m_settings.height &= ~1;
    aom_codec_iface_t *iface = aom_codec_av1_cx();
    aom_codec_enc_cfg_t cfg;
    if (aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_REALTIME) != AOM_CODEC_OK) {
        if (error)
            *error = QStringLiteral("aom_codec_enc_config_default");
        return false;
    }
    cfg.g_w = unsigned(m_settings.width);
    cfg.g_h = unsigned(m_settings.height);
    cfg.g_timebase = {1, 1000000}; // microseconds: the frames come when the camera gives them
    cfg.g_threads = unsigned(std::max(1, m_settings.threads));
    cfg.g_lag_in_frames = 0;
    cfg.g_error_resilient = 0;
    cfg.rc_end_usage = AOM_CBR;
    cfg.rc_target_bitrate = unsigned(m_settings.kbps);
    cfg.rc_min_quantizer = 10;
    cfg.rc_max_quantizer = 58;
    cfg.rc_undershoot_pct = 50;
    cfg.rc_overshoot_pct = 50;
    cfg.rc_buf_initial_sz = 600;
    cfg.rc_buf_optimal_sz = 600;
    cfg.rc_buf_sz = 1000;
    cfg.rc_dropframe_thresh = 0;
    // Key frames are placed here by time (the frame rate of a camera is not steady).
    cfg.kf_mode = AOM_KF_DISABLED;
    if (aom_codec_enc_init(&d->ctx, iface, &cfg, 0) != AOM_CODEC_OK) {
        if (error)
            *error = QString::fromUtf8(aom_codec_error_detail(&d->ctx) ? aom_codec_error_detail(&d->ctx)
                                                                       : aom_codec_error(&d->ctx));
        return false;
    }
    // The settings WebRTC uses for AV1 at small sizes.
    aom_codec_control(&d->ctx, AOME_SET_CPUUSED, 10);
    aom_codec_control(&d->ctx, AV1E_SET_AQ_MODE, 3);
    aom_codec_control(&d->ctx, AV1E_SET_ROW_MT, 1);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_CDEF, 1);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_TPL_MODEL, 0);
    aom_codec_control(&d->ctx, AV1E_SET_DELTAQ_MODE, 0);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_ORDER_HINT, 0);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_WARPED_MOTION, 0);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_OBMC, 0);
    aom_codec_control(&d->ctx, AV1E_SET_ENABLE_PALETTE, 0);
    aom_codec_control(&d->ctx, AV1E_SET_NOISE_SENSITIVITY, 0);
    aom_codec_control(&d->ctx, AV1E_SET_COEFF_COST_UPD_FREQ, 3);
    aom_codec_control(&d->ctx, AV1E_SET_MODE_COST_UPD_FREQ, 3);
    aom_codec_control(&d->ctx, AV1E_SET_MV_COST_UPD_FREQ, 3);
    aom_codec_control(&d->ctx, AV1E_SET_GF_CBR_BOOST_PCT, 0);
    aom_codec_control(&d->ctx, AOME_SET_MAX_INTRA_BITRATE_PCT, 300);
    d->open = true;
    d->started = false;
    return true;
}

QList<EncodedFrame> Av1Encoder::encode(const I420Frame &frame, qint64 ptsUs, bool key)
{
    QList<EncodedFrame> out;
    if (!d->open || frame.width != m_settings.width || frame.height != m_settings.height)
        return out;
    if (d->started && ptsUs <= d->lastPts)
        ptsUs = d->lastPts + 1; // the encoder needs growing time
    if (!d->started || ptsUs - d->lastKeyPts >= qint64(m_settings.keyIntervalMs) * 1000)
        key = true;
    const qint64 duration = d->started ? std::max<qint64>(1, ptsUs - d->lastPts) : 1000000 / std::max(1, m_settings.fps);
    aom_image_t img;
    aom_img_wrap(&img, AOM_IMG_FMT_I420, unsigned(frame.width), unsigned(frame.height), 1,
                 const_cast<uchar *>(frame.y()));
    if (aom_codec_encode(&d->ctx, &img, ptsUs, (unsigned long)duration, key ? AOM_EFLAG_FORCE_KF : 0) != AOM_CODEC_OK)
        return out;
    d->started = true;
    d->lastPts = ptsUs;
    aom_codec_iter_t iter = nullptr;
    while (const aom_codec_cx_pkt_t *pkt = aom_codec_get_cx_data(&d->ctx, &iter)) {
        if (pkt->kind != AOM_CODEC_CX_FRAME_PKT)
            continue;
        EncodedFrame f;
        f.data = QByteArray(static_cast<const char *>(pkt->data.frame.buf), qsizetype(pkt->data.frame.sz));
        f.ptsUs = pkt->data.frame.pts;
        f.key = pkt->data.frame.flags & AOM_FRAME_IS_KEY;
        if (f.key)
            d->lastKeyPts = f.ptsUs;
        out.append(std::move(f));
    }
    return out;
}

void Av1Encoder::close()
{
    if (d->open)
        aom_codec_destroy(&d->ctx);
    d->open = false;
}

struct Av1Decoder::Impl
{
    aom_codec_ctx_t ctx{};
    bool open = false;
};

Av1Decoder::Av1Decoder() : d(std::make_unique<Impl>()) {}

Av1Decoder::~Av1Decoder()
{
    reset();
}

void Av1Decoder::reset()
{
    if (d->open)
        aom_codec_destroy(&d->ctx);
    d->open = false;
}

I420Frame Av1Decoder::decode(QByteArrayView packet)
{
    if (!d->open) {
        aom_codec_dec_cfg_t cfg{};
        cfg.threads = 1;
        cfg.allow_lowbitdepth = 1;
        if (aom_codec_dec_init(&d->ctx, aom_codec_av1_dx(), &cfg, 0) != AOM_CODEC_OK)
            return {};
        d->open = true;
    }
    if (aom_codec_decode(&d->ctx, reinterpret_cast<const uint8_t *>(packet.data()), size_t(packet.size()), nullptr)
        != AOM_CODEC_OK)
        return {};
    I420Frame result;
    aom_codec_iter_t iter = nullptr;
    while (aom_image_t *img = aom_codec_get_frame(&d->ctx, &iter)) {
        if (img->fmt != AOM_IMG_FMT_I420 && img->fmt != AOM_IMG_FMT_YV12)
            continue;
        const bool yv12 = img->fmt == AOM_IMG_FMT_YV12;
        result = Yuv::fromPlanes(img->planes[AOM_PLANE_Y], img->stride[AOM_PLANE_Y],
                                 img->planes[yv12 ? AOM_PLANE_V : AOM_PLANE_U], img->stride[yv12 ? AOM_PLANE_V : AOM_PLANE_U],
                                 img->planes[yv12 ? AOM_PLANE_U : AOM_PLANE_V], img->stride[yv12 ? AOM_PLANE_U : AOM_PLANE_V],
                                 int(img->d_w), int(img->d_h));
    }
    return result;
}
