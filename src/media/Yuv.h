#pragma once

#include <QByteArray>
#include <QImage>

// A picture in I420 (8-bit Y, then U and V at half size), packed: the strides are the widths. The width and the
// height are even.
struct I420Frame
{
    int width = 0;
    int height = 0;
    QByteArray data;

    I420Frame() = default;
    I420Frame(int w, int h) { resize(w, h); }
    void resize(int w, int h);
    bool isNull() const { return width <= 0 || height <= 0; }

    uchar *y() { return reinterpret_cast<uchar *>(data.data()); }
    uchar *u() { return y() + width * height; }
    uchar *v() { return u() + (width / 2) * (height / 2); }
    const uchar *y() const { return reinterpret_cast<const uchar *>(data.constData()); }
    const uchar *u() const { return y() + width * height; }
    const uchar *v() const { return u() + (width / 2) * (height / 2); }
};

namespace Yuv {

// Camera formats → I420 of the same size (odd sizes lose their last column / row).
I420Frame fromNv12(const uchar *y, int yStride, const uchar *uv, int uvStride, int width, int height);
I420Frame fromYuyv(const uchar *src, int stride, int width, int height);
I420Frame fromPlanes(const uchar *y, int yStride, const uchar *u, int uStride, const uchar *v, int vStride,
                     int width, int height);
// From a picture (cameras that give JPEG or RGB; tests).
I420Frame fromImage(const QImage &image);

// The frame scaled (area average when smaller, bilinear when larger) and cropped to the aspect of the target, centred.
I420Frame scaled(const I420Frame &src, int width, int height);

// BT.601, limited range: what webcams and AV1 by default carry.
QImage toImage(const I420Frame &frame);

} // namespace Yuv
