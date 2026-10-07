#include "Yuv.h"

#include <algorithm>
#include <cstring>

void I420Frame::resize(int w, int h)
{
    width = w & ~1;
    height = h & ~1;
    data.resize(qsizetype(width) * height * 3 / 2);
}

namespace {

uchar clamp255(int v) { return uchar(std::clamp(v, 0, 255)); }

void copyPlane(const uchar *src, int stride, uchar *dst, int width, int height)
{
    for (int r = 0; r < height; ++r)
        std::memcpy(dst + qsizetype(r) * width, src + qsizetype(r) * stride, size_t(width));
}

// Samples the rectangle (cx, cy, cw, ch) of a plane into dw × dh: the average of the source area of each target
// pixel when shrinking, bilinear when growing.
void resample(const uchar *src, int stride, double cx, double cy, double cw, double ch, uchar *dst, int dw, int dh,
              int srcW, int srcH)
{
    const double sx = cw / dw, sy = ch / dh;
    for (int r = 0; r < dh; ++r) {
        uchar *out = dst + qsizetype(r) * dw;
        if (sx >= 1 && sy >= 1) {
            const int y0 = std::clamp(int(cy + r * sy), 0, srcH - 1);
            const int y1 = std::clamp(int(cy + (r + 1) * sy), y0 + 1, srcH);
            for (int c = 0; c < dw; ++c) {
                const int x0 = std::clamp(int(cx + c * sx), 0, srcW - 1);
                const int x1 = std::clamp(int(cx + (c + 1) * sx), x0 + 1, srcW);
                int sum = 0;
                for (int y = y0; y < y1; ++y) {
                    const uchar *row = src + qsizetype(y) * stride;
                    for (int x = x0; x < x1; ++x)
                        sum += row[x];
                }
                const int n = (y1 - y0) * (x1 - x0);
                out[c] = uchar((sum + n / 2) / n);
            }
        } else {
            const double fy = std::clamp(cy + (r + 0.5) * sy - 0.5, 0.0, double(srcH - 1));
            const int y0 = int(fy), y1 = std::min(y0 + 1, srcH - 1);
            const int wy = int((fy - y0) * 256);
            for (int c = 0; c < dw; ++c) {
                const double fx = std::clamp(cx + (c + 0.5) * sx - 0.5, 0.0, double(srcW - 1));
                const int x0 = int(fx), x1 = std::min(x0 + 1, srcW - 1);
                const int wx = int((fx - x0) * 256);
                const uchar *a = src + qsizetype(y0) * stride, *b = src + qsizetype(y1) * stride;
                const int top = a[x0] * (256 - wx) + a[x1] * wx;
                const int bottom = b[x0] * (256 - wx) + b[x1] * wx;
                out[c] = uchar((top * (256 - wy) + bottom * wy + 32768) >> 16);
            }
        }
    }
}

} // namespace

namespace Yuv {

I420Frame fromPlanes(const uchar *y, int yStride, const uchar *u, int uStride, const uchar *v, int vStride,
                     int width, int height)
{
    I420Frame f(width, height);
    copyPlane(y, yStride, f.y(), f.width, f.height);
    copyPlane(u, uStride, f.u(), f.width / 2, f.height / 2);
    copyPlane(v, vStride, f.v(), f.width / 2, f.height / 2);
    return f;
}

I420Frame fromNv12(const uchar *y, int yStride, const uchar *uv, int uvStride, int width, int height)
{
    I420Frame f(width, height);
    copyPlane(y, yStride, f.y(), f.width, f.height);
    const int cw = f.width / 2, chh = f.height / 2;
    uchar *u = f.u(), *v = f.v();
    for (int r = 0; r < chh; ++r) {
        const uchar *row = uv + qsizetype(r) * uvStride;
        for (int c = 0; c < cw; ++c) {
            u[r * cw + c] = row[2 * c];
            v[r * cw + c] = row[2 * c + 1];
        }
    }
    return f;
}

I420Frame fromYuyv(const uchar *src, int stride, int width, int height)
{
    I420Frame f(width, height);
    const int cw = f.width / 2;
    uchar *y = f.y(), *u = f.u(), *v = f.v();
    for (int r = 0; r < f.height; ++r) {
        const uchar *row = src + qsizetype(r) * stride;
        for (int c = 0; c < cw; ++c) {
            y[r * f.width + 2 * c] = row[4 * c];
            y[r * f.width + 2 * c + 1] = row[4 * c + 2];
        }
        if (r % 2 == 0) { // chroma of the even rows, averaged with the next one
            const uchar *next = src + qsizetype(r + 1) * stride;
            for (int c = 0; c < cw; ++c) {
                u[(r / 2) * cw + c] = uchar((row[4 * c + 1] + next[4 * c + 1] + 1) / 2);
                v[(r / 2) * cw + c] = uchar((row[4 * c + 3] + next[4 * c + 3] + 1) / 2);
            }
        }
    }
    return f;
}

I420Frame fromImage(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    I420Frame f(rgb.width(), rgb.height());
    const int cw = f.width / 2;
    uchar *y = f.y(), *u = f.u(), *v = f.v();
    for (int r = 0; r < f.height; ++r) {
        const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(r));
        for (int c = 0; c < f.width; ++c) {
            const int R = qRed(row[c]), G = qGreen(row[c]), B = qBlue(row[c]);
            y[r * f.width + c] = uchar(((66 * R + 129 * G + 25 * B + 128) >> 8) + 16);
        }
    }
    for (int r = 0; r < f.height / 2; ++r) {
        const QRgb *a = reinterpret_cast<const QRgb *>(rgb.constScanLine(2 * r));
        const QRgb *b = reinterpret_cast<const QRgb *>(rgb.constScanLine(2 * r + 1));
        for (int c = 0; c < cw; ++c) {
            int R = 0, G = 0, B = 0;
            for (QRgb p : {a[2 * c], a[2 * c + 1], b[2 * c], b[2 * c + 1]}) {
                R += qRed(p);
                G += qGreen(p);
                B += qBlue(p);
            }
            R = (R + 2) / 4;
            G = (G + 2) / 4;
            B = (B + 2) / 4;
            u[r * cw + c] = uchar(((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128);
            v[r * cw + c] = uchar(((112 * R - 94 * G - 18 * B + 128) >> 8) + 128);
        }
    }
    return f;
}

I420Frame scaled(const I420Frame &src, int width, int height)
{
    I420Frame f(width, height);
    if (src.isNull() || f.isNull())
        return f;
    if (src.width == f.width && src.height == f.height)
        return src;
    // The largest centred part of the source with the target's aspect.
    double cw = src.width, ch = src.height;
    if (cw * f.height > ch * f.width)
        cw = ch * f.width / f.height;
    else
        ch = cw * f.height / f.width;
    const double cx = (src.width - cw) / 2, cy = (src.height - ch) / 2;
    resample(src.y(), src.width, cx, cy, cw, ch, f.y(), f.width, f.height, src.width, src.height);
    resample(src.u(), src.width / 2, cx / 2, cy / 2, cw / 2, ch / 2, f.u(), f.width / 2, f.height / 2,
             src.width / 2, src.height / 2);
    resample(src.v(), src.width / 2, cx / 2, cy / 2, cw / 2, ch / 2, f.v(), f.width / 2, f.height / 2,
             src.width / 2, src.height / 2);
    return f;
}

QImage toImage(const I420Frame &frame)
{
    QImage image(frame.width, frame.height, QImage::Format_RGB32);
    if (frame.isNull())
        return image;
    const int cw = frame.width / 2;
    const uchar *y = frame.y(), *u = frame.u(), *v = frame.v();
    for (int r = 0; r < frame.height; ++r) {
        QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(r));
        const uchar *yr = y + qsizetype(r) * frame.width;
        const uchar *ur = u + qsizetype(r / 2) * cw, *vr = v + qsizetype(r / 2) * cw;
        for (int c = 0; c < frame.width; ++c) {
            const int C = 298 * (yr[c] - 16), D = ur[c / 2] - 128, E = vr[c / 2] - 128;
            out[c] = qRgb(clamp255((C + 409 * E + 128) >> 8), clamp255((C - 100 * D - 208 * E + 128) >> 8),
                          clamp255((C + 516 * D + 128) >> 8));
        }
    }
    return image;
}

} // namespace Yuv
