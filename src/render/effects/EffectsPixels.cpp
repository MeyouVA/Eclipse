// Eclipse - effects on a picture's pixels: blurs, colour changes, grain, median, ripple, bloom, pixelate.
#include "render/effects/Effects.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace Effects {

static void boxBlur(QImage& image, int r)
{
    int const w = image.width(), h = image.height();
    if (r < 1 || w < 1 || h < 1)
        return;
    int const span = 2 * r + 1;
    QVector<QRgb> buffer(std::max(w, h));
    auto run = [&](QRgb* first, int count, qsizetype stride) {
        for (int i = 0; i < count; ++i)
            buffer[i] = *reinterpret_cast<QRgb*>(reinterpret_cast<uchar*>(first) + i * stride);
        unsigned a = 0, red = 0, g = 0, b = 0;
        auto take = [&](int i, int sign) {
            if (i < 0 || i >= count)
                return;
            QRgb const c = buffer[i];
            a += sign * qAlpha(c);
            red += sign * qRed(c);
            g += sign * qGreen(c);
            b += sign * qBlue(c);
        };
        for (int i = -r; i <= r; ++i)
            take(i, +1);
        for (int i = 0; i < count; ++i) {
            *reinterpret_cast<QRgb*>(reinterpret_cast<uchar*>(first) + i * stride) = qRgba(int(red / span), int(g / span), int(b / span), int(a / span));
            take(i - r, -1);
            take(i + r + 1, +1);
        }
    };
    for (int y = 0; y < h; ++y)
        run(reinterpret_cast<QRgb*>(image.scanLine(y)), w, sizeof(QRgb));
    qsizetype const lineBytes = image.bytesPerLine();
    for (int x = 0; x < w; ++x)
        run(reinterpret_cast<QRgb*>(image.scanLine(0)) + x, h, lineBytes);
}

void blur(QImage& image, double radius)
{
    if (radius < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Three box blurs in a row come very close to a true soft blur.
    int const r = std::max(1, int(std::lround(radius * 0.6)));
    for (int pass = 0; pass < 3; ++pass)
        boxBlur(image, std::min(r, 400));
}

void tint(QImage& image, QColor const& color, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const k = int(std::lround(amount * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // The colours are stored already multiplied by how opaque the pixel is.
            int const tr = color.red() * a / 255, tg = color.green() * a / 255, tb = color.blue() * a / 255;
            row[x] = qRgba(qRed(row[x]) + (tr - qRed(row[x])) * k / 256, qGreen(row[x]) + (tg - qGreen(row[x])) * k / 256,
                           qBlue(row[x]) + (tb - qBlue(row[x])) * k / 256, a);
        }
    }
}

void transparency(QImage& image, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const k = int(std::lround((1.0 - amount) * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (row[x] != 0)
                row[x] = qRgba(qRed(row[x]) * k / 256, qGreen(row[x]) * k / 256, qBlue(row[x]) * k / 256, qAlpha(row[x]) * k / 256);
    }
}

void directionalBlur(QImage& image, double length, double degrees)
{
    directionalBlur(image, length, degrees, 0.0, 0);
}

void directionalBlur(QImage& image, double length, double degrees, double falloff, int trail)
{
    if (length < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // Samples along the line, from one end to the other, averaged (no more than 48 of them).
    int const samples = std::clamp(int(std::ceil(length)), 2, 48);
    double const turn = degrees * 3.14159265358979323846 / 180.0;
    QPointF const along(std::cos(turn), -std::sin(turn));
    // Each sample weighed less the further along it is, as the falloff says (none: all the same).
    QVector<QPoint> steps;
    QVector<int> weights;
    int total = 0;
    falloff = std::clamp(falloff, 0.0, 1.0);
    for (int s = 0; s < samples; ++s) {
        double const share = double(s) / (samples - 1);
        double const t = (trail == 1 ? share : trail == 2 ? share - 1.0 : share - 0.5) * length;
        steps.push_back(QPoint(int(std::lround(along.x() * t)), int(std::lround(along.y() * t))));
        double const away = trail == 0 ? std::abs(share - 0.5) * 2.0 : trail == 1 ? share : 1.0 - share;
        weights.push_back(std::max(1, int(std::lround(256.0 * (1.0 - falloff * away)))));
        total += weights.back();
    }
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            qint64 r = 0, g = 0, b = 0, a = 0;
            for (int s = 0; s < samples; ++s) {
                int const sx = x + steps[s].x(), sy = y + steps[s].y();
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    continue;       // beyond the picture: nothing (so the edge fades out)
                QRgb const p = reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
                r += qint64(qRed(p)) * weights[s];
                g += qint64(qGreen(p)) * weights[s];
                b += qint64(qBlue(p)) * weights[s];
                a += qint64(qAlpha(p)) * weights[s];
            }
            out[x] = qRgba(int(r / total), int(g / total), int(b / total), int(a / total));
        }
    }
}

void hueSaturation(QImage& image, double hue, double saturation, double lightness, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (hue == 0.0 && saturation == 0.0 && lightness == 0.0))
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    double const satBy = 1.0 + std::clamp(saturation, -100.0, 100.0) / 100.0, lightBy = std::clamp(lightness, -100.0, 100.0) / 100.0;
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // The colour as it is, not multiplied by how opaque it is; then as hue, saturation and lightness.
            double const r = qRed(row[x]) / double(a), g = qGreen(row[x]) / double(a), b = qBlue(row[x]) / double(a);
            double const most = std::max({r, g, b}), least = std::min({r, g, b});
            double l = (most + least) / 2.0, s = 0.0, hh = 0.0;
            if (most - least > 1e-9) {
                double const d = most - least;
                s = l > 0.5 ? d / (2.0 - most - least) : d / (most + least);
                hh = most == r ? (g - b) / d + (g < b ? 6.0 : 0.0) : most == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
                hh *= 60.0;
            }
            hh = std::fmod(hh + hue + 720.0, 360.0);
            s = std::clamp(s * satBy, 0.0, 1.0);
            l = std::clamp(lightBy >= 0.0 ? l + (1.0 - l) * lightBy : l * (1.0 + lightBy), 0.0, 1.0);
            auto part = [](double p, double q, double t) {
                t = t < 0.0 ? t + 1.0 : t > 1.0 ? t - 1.0 : t;
                return t < 1.0 / 6.0 ? p + (q - p) * 6.0 * t : t < 0.5 ? q : t < 2.0 / 3.0 ? p + (q - p) * (2.0 / 3.0 - t) * 6.0 : p;
            };
            double nr = l, ng = l, nb = l;
            if (s > 1e-9) {
                double const q = l < 0.5 ? l * (1.0 + s) : l + s - l * s, p = 2.0 * l - q, t = hh / 360.0;
                nr = part(p, q, t + 1.0 / 3.0);
                ng = part(p, q, t);
                nb = part(p, q, t - 1.0 / 3.0);
            }
            auto mix = [&](double was, double now) { return std::clamp(int(std::lround((was + (now - was) * amount) * a)), 0, a); };
            row[x] = qRgba(mix(r, nr), mix(g, ng), mix(b, nb), a);
        }
    }
}

void brightnessContrast(QImage& image, double brightness, double contrast, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (brightness == 0.0 && contrast == 0.0))
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    double const c = std::clamp(contrast, -100.0, 100.0) / 100.0;
    double const factor = c >= 0.0 ? 1.0 / std::max(0.01, 1.0 - c) : 1.0 + c;     // all the way: black and white, or flat grey
    double const lift = std::clamp(brightness, -100.0, 100.0) / 100.0;
    int table[256];
    for (int v = 0; v < 256; ++v) {
        double const now = std::clamp((v / 255.0 - 0.5) * factor + 0.5 + lift, 0.0, 1.0);
        table[v] = std::clamp(int(std::lround((v / 255.0 + (now - v / 255.0) * amount) * 255.0)), 0, 255);
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            auto one = [&](int premultiplied) { return table[std::clamp(premultiplied * 255 / a, 0, 255)] * a / 255; };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), a);
        }
    }
}

void grain(QImage& image, double amount, quint32 seed)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const spread = int(std::lround(amount * 160.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // A number for this point that depends on nothing but where it is and the seed.
            quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ seed * 83492791u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            int const shift = (int(h & 0xff) - 128) * spread / 128 * a / 255;
            auto one = [&](int v) { return std::clamp(v + shift, 0, a); };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), a);
        }
    }
}

void radialBlur(QImage& image, double amount)
{
    radialBlur(image, amount, 1.0);
}

void radialBlur(QImage& image, double amount, double quality)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // The middle of what is painted.
    int left = w, top = h, right = -1, bottom = -1;
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(row[x]) != 0) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    }
    if (right < 0)
        return;
    double const cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    int const samples = std::clamp(int(std::lround(24.0 * quality)), 3, 24);
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, al = 0;
            for (int s = 0; s < samples; ++s) {
                // Along the line from the middle through the point, so far either side of it.
                double const k = 1.0 + amount * (0.5 - double(s) / (samples - 1));
                int const sx = int(std::lround(cx + (x - cx) * k)), sy = int(std::lround(cy + (y - cy) * k));
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    continue;
                QRgb const p = reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
                r += qRed(p);
                g += qGreen(p);
                b += qBlue(p);
                al += qAlpha(p);
            }
            out[x] = qRgba(r / samples, g / samples, b / samples, al / samples);
        }
    }
}

void colourScale(QImage& image, QColor const& scale, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // (Premultiplied colours scale as they are: the shares apply to the colour, not to how opaque it is.)
    int const kr = int(std::lround((1.0 + (scale.redF() - 1.0) * amount) * 256.0)), kg = int(std::lround((1.0 + (scale.greenF() - 1.0) * amount) * 256.0)),
              kb = int(std::lround((1.0 + (scale.blueF() - 1.0) * amount) * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (row[x] != 0)
                row[x] = qRgba(qRed(row[x]) * kr / 256, qGreen(row[x]) * kg / 256, qBlue(row[x]) * kb / 256, qAlpha(row[x]));
    }
}

void negate(QImage& image, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const k = int(std::lround(amount * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // (Premultiplied: the opposite of a colour is its opacity less it.)
            auto one = [&](int v) { return v + ((a - 2 * v) * k) / 256; };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), a);
        }
    }
}

void median(QImage& image, double radius)
{
    // (Past a few pixels a median is slow and looks no different from a blur of the shapes.)
    int const r = std::clamp(int(std::lround(radius)), 0, 8);
    if (r == 0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    QImage const source = image;
    std::vector<int> channel[4];
    for (auto& c : channel)
        c.reserve(size_t((2 * r + 1) * (2 * r + 1)));
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            for (auto& c : channel)
                c.clear();
            bool any = false;
            for (int dy = -r; dy <= r; ++dy) {
                int const sy = y + dy;
                if (sy < 0 || sy >= h)
                    continue;
                QRgb const* const row = reinterpret_cast<QRgb const*>(source.constScanLine(sy));
                for (int dx = -r; dx <= r; ++dx) {
                    int const sx = x + dx;
                    if (sx < 0 || sx >= w || dx * dx + dy * dy > r * r)
                        continue;
                    QRgb const p = row[sx];
                    any = any || p != 0;
                    channel[0].push_back(qRed(p));
                    channel[1].push_back(qGreen(p));
                    channel[2].push_back(qBlue(p));
                    channel[3].push_back(qAlpha(p));
                }
            }
            if (!any)
                continue;
            int middle[4];
            for (int c = 0; c < 4; ++c) {
                auto const at = channel[c].begin() + qsizetype(channel[c].size() / 2);
                std::nth_element(channel[c].begin(), at, channel[c].end());
                middle[c] = *at;
            }
            // (Each part on its own: kept no brighter than it is opaque.)
            out[x] = qRgba(std::min(middle[0], middle[3]), std::min(middle[1], middle[3]), std::min(middle[2], middle[3]), middle[3]);
        }
    }
}

void ripple(QImage& image, double height, double length, int frame)
{
    if (height <= 0.0 || length <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    QImage const source = image;
    double constexpr kTau = 6.283185307179586;
    // The waves move on a twelfth of their length each frame.
    double const phase = frame * kTau / 12.0;
    for (int y = 0; y < h; ++y) {
        double const shift = height * std::sin(y * kTau / length - phase);
        int const whole = int(std::floor(shift));
        int const part = int(std::lround((shift - whole) * 256.0));
        QRgb const* const in = reinterpret_cast<QRgb const*>(source.constScanLine(y));
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        auto at = [&](int x) { return x < 0 || x >= w ? QRgb(0) : in[x]; };
        for (int x = 0; x < w; ++x) {
            // Between the two points it falls between, as near each as it is.
            QRgb const a = at(x - whole), b = at(x - whole - 1);
            auto mix = [&](int va, int vb) { return (va * (256 - part) + vb * part) / 256; };
            out[x] = qRgba(mix(qRed(a), qRed(b)), mix(qGreen(a), qGreen(b)), mix(qBlue(a), qBlue(b)), mix(qAlpha(a), qAlpha(b)));
        }
    }
}

void bloom(QImage& image, double radius)
{
    if (radius < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // The bright parts only, as bright as they are past the threshold.
    QImage bright(image.size(), QImage::Format_ARGB32_Premultiplied);
    bright.setDevicePixelRatio(image.devicePixelRatio());
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const in = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        QRgb* const out = reinterpret_cast<QRgb*>(bright.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const light = (qRed(in[x]) * 30 + qGreen(in[x]) * 59 + qBlue(in[x]) * 11) / 100;
            int const past = std::max(0, light - 170) * 255 / 85;       // 0 below two thirds, all of it at white
            out[x] = qRgba(qRed(in[x]) * past / 255, qGreen(in[x]) * past / 255, qBlue(in[x]) * past / 255, qAlpha(in[x]) * past / 255);
        }
    }
    blur(bright, radius);
    QPainter p(&image);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.drawImage(QPointF(), bright);
}

void pixelate(QImage& image, double size)
{
    int const block = int(std::lround(size));
    if (block < 2)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    for (int top = 0; top < h; top += block)
        for (int left = 0; left < w; left += block) {
            int const bottom = std::min(h, top + block), right = std::min(w, left + block);
            qint64 r = 0, g = 0, b = 0, a = 0;
            for (int y = top; y < bottom; ++y) {
                QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
                for (int x = left; x < right; ++x) {
                    r += qRed(row[x]);
                    g += qGreen(row[x]);
                    b += qBlue(row[x]);
                    a += qAlpha(row[x]);
                }
            }
            qint64 const n = qint64(bottom - top) * (right - left);
            QRgb const mean = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
            for (int y = top; y < bottom; ++y) {
                QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
                std::fill(row + left, row + right, mean);
            }
        }
}

}  // namespace Effects
