// Eclipse - pictures of 16 bits a channel ("deep"), painted while colour is managed so that linear light keeps its darks; reading and writing their pixels.
#pragma once

#include <QImage>
#include <QPaintDevice>
#include <QRgba64>

#include <algorithm>
#include <cmath>

namespace Deep {

// A pixel as 0..1 numbers, multiplied by its alpha.
struct Pixel {
    double r = 0, g = 0, b = 0, a = 0;
};

inline bool isDeep(QImage const& image)
{
    return image.format() == QImage::Format_RGBA64_Premultiplied || image.format()==QImage::Format_RGBA32FPx4_Premultiplied;
}

inline bool isFloat(QImage const& image) { return image.format()==QImage::Format_RGBA32FPx4_Premultiplied; }

inline bool isDeep(QPaintDevice const* device)
{
    return device && device->devType() == QInternal::Image && isDeep(*static_cast<QImage const*>(device));
}

// The format of a picture painted on its own to go onto `device`: deep when that is.
inline QImage::Format formatFor(QPaintDevice const* device)
{
    if(device && device->devType()==QInternal::Image && isFloat(*static_cast<QImage const*>(device))) return QImage::Format_RGBA32FPx4_Premultiplied;
    return isDeep(device) ? QImage::Format_RGBA64_Premultiplied : QImage::Format_ARGB32_Premultiplied;
}

// Made ready to be gone over pixel by pixel: deep pictures stay deep, the rest become 8 bits a channel, multiplied by alpha.
inline void prepare(QImage& image)
{
    if(image.pixelFormat().typeInterpretation()==QPixelFormat::FloatingPoint && !isFloat(image))
        image=image.convertToFormat(QImage::Format_RGBA32FPx4_Premultiplied);
    else if (!isDeep(image) && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// A row of a prepared picture, read as Pixels whichever its depth.
class ConstLine {
public:
    ConstLine(QImage const& image, int y) : m_deep(isDeep(image)), m_float(isFloat(image)), m_bits(image.constScanLine(y)) {}

    Pixel operator[](int x) const
    {
        if(m_float) { float const* p=reinterpret_cast<float const*>(m_bits)+x*4; return {p[0],p[1],p[2],p[3]}; }
        if (m_deep) {
            QRgba64 const p = reinterpret_cast<QRgba64 const*>(m_bits)[x];
            return {p.red() / 65535.0, p.green() / 65535.0, p.blue() / 65535.0, p.alpha() / 65535.0};
        }
        QRgb const p = reinterpret_cast<QRgb const*>(m_bits)[x];
        return {qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0, qAlpha(p) / 255.0};
    }

    double alpha(int x) const
    {
        if(m_float) return reinterpret_cast<float const*>(m_bits)[x*4+3];
        return m_deep ? reinterpret_cast<QRgba64 const*>(m_bits)[x].alpha() / 65535.0 : qAlpha(reinterpret_cast<QRgb const*>(m_bits)[x]) / 255.0;
    }

private:
    bool m_deep;
    bool m_float;
    uchar const* m_bits;
};

class Line {
public:
    Line(QImage& image, int y) : m_deep(isDeep(image)), m_float(isFloat(image)), m_bits(image.scanLine(y)) {}

    Pixel operator[](int x) const
    {
        if(m_float) { float const* p=reinterpret_cast<float const*>(m_bits)+x*4; return {p[0],p[1],p[2],p[3]}; }
        if (m_deep) {
            QRgba64 const p = reinterpret_cast<QRgba64 const*>(m_bits)[x];
            return {p.red() / 65535.0, p.green() / 65535.0, p.blue() / 65535.0, p.alpha() / 65535.0};
        }
        QRgb const p = reinterpret_cast<QRgb const*>(m_bits)[x];
        return {qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0, qAlpha(p) / 255.0};
    }

    double alpha(int x) const
    {
        if(m_float) return reinterpret_cast<float const*>(m_bits)[x*4+3];
        return m_deep ? reinterpret_cast<QRgba64 const*>(m_bits)[x].alpha() / 65535.0 : qAlpha(reinterpret_cast<QRgb const*>(m_bits)[x]) / 255.0;
    }

    // (Colours are kept no more than the alpha, as a premultiplied pixel must be.)
    void set(int x, Pixel const& p) const
    {
        double const a = std::clamp(p.a, 0.0, 1.0);
        if(m_float) { float* out=reinterpret_cast<float*>(m_bits)+x*4; out[0]=a>0?float(p.r):0; out[1]=a>0?float(p.g):0; out[2]=a>0?float(p.b):0; out[3]=float(a); return; }
        auto level = [a](double v, double top) { return std::lround(std::clamp(v, 0.0, a) * top); };
        if (m_deep)
            reinterpret_cast<QRgba64*>(m_bits)[x] = QRgba64::fromRgba64(quint16(level(p.r, 65535.0)), quint16(level(p.g, 65535.0)), quint16(level(p.b, 65535.0)),
                                                                        quint16(std::lround(a * 65535.0)));
        else
            reinterpret_cast<QRgb*>(m_bits)[x] = qRgba(int(level(p.r, 255.0)), int(level(p.g, 255.0)), int(level(p.b, 255.0)), int(std::lround(a * 255.0)));
    }

private:
    bool m_deep;
    bool m_float;
    uchar* m_bits;
};

}  // namespace Deep
