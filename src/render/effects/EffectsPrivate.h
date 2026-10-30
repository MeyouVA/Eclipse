// Eclipse - what the effect files share: going over the pixels, and noise.
#pragma once

#include <QImage>
#include <QColor>

#include "colour/images/DeepPixels.h"

#include <algorithm>
#include <cmath>

namespace Effects::detail {

inline void premultiplied(QImage& image)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Each pixel as red, green, blue and alpha (0..1, not multiplied by the alpha), changed by `change`; the clear ones too with `clear`.
template <class Change>
void perPixel(QImage& image, Change&& change, bool clear = false)
{
    if (Deep::isDeep(image)) {
        // (16 bits a channel: kept so, with the same sums in doubles.)
        for (int y = 0; y < image.height(); ++y) {
            Deep::Line const row(image, y);
            for (int x = 0; x < image.width(); ++x) {
                Deep::Pixel const p = row[x];
                if (p.a <= 0.0 && !clear)
                    continue;
                double r = p.a > 0.0 ? p.r / p.a : 0.0, g = p.a > 0.0 ? p.g / p.a : 0.0, b = p.a > 0.0 ? p.b / p.a : 0.0, al = p.a;
                change(r, g, b, al, x, y);
                al = std::clamp(al, 0.0, 1.0);
                row.set(x, {std::clamp(r, 0.0, 1.0) * al, std::clamp(g, 0.0, 1.0) * al, std::clamp(b, 0.0, 1.0) * al, al});
            }
        }
        return;
    }
    premultiplied(image);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0 && !clear)
                continue;
            double r = a ? qRed(row[x]) / double(a) : 0.0, g = a ? qGreen(row[x]) / double(a) : 0.0, b = a ? qBlue(row[x]) / double(a) : 0.0;
            double al = a / 255.0;
            change(r, g, b, al, x, y);
            al = std::clamp(al, 0.0, 1.0);
            auto one = [al](double v) { return std::clamp(int(std::lround(std::clamp(v, 0.0, 1.0) * al * 255.0)), 0, 255); };
            row[x] = qRgba(one(r), one(g), one(b), int(std::lround(al * 255.0)));
        }
    }
}

// A number from a place and a seed, the same each time they are the same.
inline quint32 hash(quint32 x, quint32 y, quint32 seed)
{
    quint32 h = x * 73856093u ^ y * 19349663u ^ seed * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

inline double random01(quint32 x, quint32 y, quint32 seed)
{
    return (hash(x, y, seed) & 0xffffff) / double(0xffffff);
}

// A noise that changes smoothly from place to place (0..1), whole numbers being where its values are set.
inline double smooth(double x, double y, quint32 seed)
{
    double const fx = std::floor(x), fy = std::floor(y);
    double const tx = x - fx, ty = y - fy;
    double const sx = tx * tx * (3.0 - 2.0 * tx), sy = ty * ty * (3.0 - 2.0 * ty);
    quint32 const ix = quint32(qint64(fx)), iy = quint32(qint64(fy));
    double const a = random01(ix, iy, seed), b = random01(ix + 1, iy, seed), c = random01(ix, iy + 1, seed), d = random01(ix + 1, iy + 1, seed);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

inline double luminance(double r, double g, double b)
{
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

}  // namespace Effects::detail
