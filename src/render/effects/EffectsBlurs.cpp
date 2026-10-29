// Eclipse - the newer blurs: box, zoom and spin, bokeh, unsharp mask.
#include "render/effects/Effects.h"
#include "render/effects/EffectsPrivate.h"

#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace Effects {

using namespace detail;

namespace {

constexpr double kPi = 3.14159265358979323846;

// One pass of a running mean `r` pixels either side, along rows (across) or columns.
void boxPass(QImage& image, int r, bool across, bool repeatEdges)
{
    int const w = image.width(), h = image.height();
    if (r < 1 || w < 1 || h < 1)
        return;
    int const count = across ? w : h, lines = across ? h : w;
    int const span = 2 * r + 1;
    QVector<QRgb> buffer(count);
    qsizetype const lineBytes = image.bytesPerLine();
    for (int line = 0; line < lines; ++line) {
        uchar* const first = across ? image.scanLine(line) : image.scanLine(0) + line * sizeof(QRgb);
        qsizetype const stride = across ? qsizetype(sizeof(QRgb)) : lineBytes;
        for (int i = 0; i < count; ++i)
            buffer[i] = *reinterpret_cast<QRgb*>(first + i * stride);
        qint64 a = 0, red = 0, g = 0, b = 0;
        auto take = [&](int i, int sign) {
            if (i < 0 || i >= count) {
                if (!repeatEdges)
                    return;
                i = std::clamp(i, 0, count - 1);
            }
            QRgb const c = buffer[i];
            a += sign * qAlpha(c);
            red += sign * qRed(c);
            g += sign * qGreen(c);
            b += sign * qBlue(c);
        };
        for (int i = -r; i <= r; ++i)
            take(i, +1);
        for (int i = 0; i < count; ++i) {
            *reinterpret_cast<QRgb*>(first + i * stride) = qRgba(int(red / span), int(g / span), int(b / span), int(a / span));
            take(i - r, -1);
            take(i + r + 1, +1);
        }
    }
}

QRgb sampleAt(QImage const& source, int x, int y, bool repeatEdges, bool& inside)
{
    inside = x >= 0 && y >= 0 && x < source.width() && y < source.height();
    if (!inside) {
        if (!repeatEdges)
            return 0;
        x = std::clamp(x, 0, source.width() - 1);
        y = std::clamp(y, 0, source.height() - 1);
    }
    return reinterpret_cast<QRgb const*>(source.constScanLine(y))[x];
}

}  // namespace

void boxBlur(QImage& image, double radiusX, double radiusY, int passes, bool repeatEdges)
{
    premultiplied(image);
    int const rx = std::min(400, int(std::lround(radiusX))), ry = std::min(400, int(std::lround(radiusY)));
    for (int pass = 0; pass < std::clamp(passes, 1, 8); ++pass) {
        boxPass(image, rx, true, repeatEdges);
        boxPass(image, ry, false, repeatEdges);
    }
}

void zoomBlur(QImage& image, QPointF centre, double amount, int kind, double quality)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    premultiplied(image);
    int const w = image.width(), h = image.height();
    int const samples = std::clamp(int(std::lround(48.0 * std::clamp(quality, 0.1, 1.0))), 4, 48);
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, al = 0;
            double const dx = x - centre.x(), dy = y - centre.y();
            for (int s = 0; s < samples; ++s) {
                double const t = 0.5 - double(s) / (samples - 1);
                double const k = kind == 1 ? 1.0 : 1.0 + amount * t;
                double const turn = kind == 0 ? 0.0 : amount * t * kPi / 4.0;
                double const c = std::cos(turn), sn = std::sin(turn);
                int const sx = int(std::lround(centre.x() + (dx * c - dy * sn) * k)), sy = int(std::lround(centre.y() + (dx * sn + dy * c) * k));
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

void bokehBlur(QImage& image, double radius, int shape, double brightness, bool repeatEdges)
{
    if (radius < 0.5)
        return;
    premultiplied(image);
    radius = std::min(radius, 300.0);
    // The points of the shape, on a grid fine enough to look smooth and coarse enough to be quick.
    QPolygonF outline;
    int const corners = shape == 1 ? 6 : shape == 2 ? 4 : shape == 3 ? 3 : 0;
    for (int k = 0; k < corners; ++k) {
        double const turn = (shape == 2 ? kPi / 4.0 : -kPi / 2.0) + 2.0 * kPi * k / corners;
        outline << QPointF(std::cos(turn), std::sin(turn)) * radius;
    }
    double const step = std::max(1.0, radius / 5.0);
    QVector<QPoint> taps;
    for (double y = -radius; y <= radius + 1e-9; y += step)
        for (double x = -radius; x <= radius + 1e-9; x += step) {
            bool const in = corners ? outline.containsPoint(QPointF(x, y), Qt::OddEvenFill) : x * x + y * y <= radius * radius + 1e-9;
            if (in)
                taps.push_back(QPoint(int(std::lround(x)), int(std::lround(y))));
        }
    if (taps.isEmpty())
        taps.push_back(QPoint());
    // How much each pixel counts: the bright ones more.
    QImage const source = image;
    int const w = image.width(), h = image.height();
    QVector<float> weight(qsizetype(w) * h);
    double const boost = std::max(0.0, brightness - 1.0);
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(source.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int const a = qAlpha(row[x]);
            double const l = a ? luminance(qRed(row[x]), qGreen(row[x]), qBlue(row[x])) / a : 0.0;
            weight[qsizetype(y) * w + x] = float(1.0 + boost * l * l * l);
        }
    }
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double r = 0, g = 0, b = 0, al = 0, total = 0;
            for (QPoint const& tap : std::as_const(taps)) {
                bool inside = false;
                int const sx = x + tap.x(), sy = y + tap.y();
                QRgb const p = sampleAt(source, sx, sy, repeatEdges, inside);
                double const k = inside || repeatEdges ? weight[qsizetype(std::clamp(sy, 0, h - 1)) * w + std::clamp(sx, 0, w - 1)] : 1.0;
                r += qRed(p) * k;
                g += qGreen(p) * k;
                b += qBlue(p) * k;
                al += qAlpha(p) * k;
                total += k;
            }
            int const a = std::clamp(int(std::lround(al / total)), 0, 255);
            auto one = [&](double v) { return std::clamp(int(std::lround(v / total)), 0, a); };
            out[x] = qRgba(one(r), one(g), one(b), a);
        }
    }
}

void unsharpMask(QImage& image, double radius, double strength, double threshold)
{
    if (radius < 0.5 || strength <= 0.0)
        return;
    premultiplied(image);
    QImage soft = image;
    blur(soft, radius);
    int const least = int(std::lround(std::clamp(threshold, 0.0, 1.0) * 255.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        QRgb const* const blurred = reinterpret_cast<QRgb const*>(soft.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            auto one = [&](int v, int b) {
                int const d = v - b;
                return std::abs(d) <= least ? v : std::clamp(int(std::lround(v + d * strength)), 0, a);
            };
            row[x] = qRgba(one(qRed(row[x]), qRed(blurred[x])), one(qGreen(row[x]), qGreen(blurred[x])), one(qBlue(row[x]), qBlue(blurred[x])), a);
        }
    }
}

}  // namespace Effects
