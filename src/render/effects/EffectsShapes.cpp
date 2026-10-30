// Eclipse - effects that reshape or recolour what is painted: turbulence, gradient, mattes, glow and shadow.
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
#include "colour/images/DeepPixels.h"

namespace Effects {

namespace {

// A smooth noise (-1..1) over the plane: random values on a grid, eased between.
double smoothNoise(double x, double y, quint32 seed)
{
    auto at = [seed](qint64 i, qint64 j) {
        quint32 h = quint32(i) * 73856093u ^ quint32(j) * 19349663u ^ seed * 83492791u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return double(h & 0xffff) / 32767.5 - 1.0;
    };
    double const fx = std::floor(x), fy = std::floor(y);
    qint64 const i = qint64(fx), j = qint64(fy);
    double const u = x - fx, v = y - fy;
    double const su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
    double const top = at(i, j) + (at(i + 1, j) - at(i, j)) * su, bottom = at(i, j + 1) + (at(i + 1, j + 1) - at(i, j + 1)) * su;
    return top + (bottom - top) * sv;
}

}  // namespace

void turbulence(QImage& image, double strength, double size, int frame)
{
    if (strength <= 0.0 || size <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    QImage const source = image;
    // The noise drifts on a little each frame (a third of a swirl), the two directions from noises of their own.
    double const drift = frame / 3.0;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double const nx = x / size, ny = y / size;
            double const dx = strength * smoothNoise(nx + drift, ny, 11u), dy = strength * smoothNoise(nx, ny + drift, 29u);
            int const sx = int(std::lround(x + dx)), sy = int(std::lround(y + dy));
            out[x] = sx < 0 || sy < 0 || sx >= w || sy >= h ? QRgb(0) : reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
        }
    }
}

void gradient(QImage& image, QColor const& from, QColor const& to, double degrees, bool radial, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // Across what is painted.
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
    double const a = degrees * 3.14159265358979323846 / 180.0, dx = std::cos(a), dy = -std::sin(a);
    // How far the painting reaches along the way (or from the middle), so the gradient runs from end to end of it.
    double reach = 0.0;
    for (QPointF const corner : {QPointF(left, top), QPointF(right, top), QPointF(left, bottom), QPointF(right, bottom)})
        reach = std::max(reach, radial ? std::hypot(corner.x() - cx, corner.y() - cy) : std::abs((corner.x() - cx) * dx + (corner.y() - cy) * dy));
    reach = std::max(reach, 1.0);
    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int const al = qAlpha(row[x]);
            if (al == 0)
                continue;
            double const t = radial ? std::min(1.0, std::hypot(x - cx, y - cy) / reach) : std::clamp(0.5 + ((x - cx) * dx + (y - cy) * dy) / (2.0 * reach), 0.0, 1.0);
            auto channel = [&](double f, double g, int now) {
                int const want = int(std::lround((f + (g - f) * t) * al));
                return std::clamp(int(std::lround(now + (want - now) * amount)), 0, al);
            };
            row[x] = qRgba(channel(from.redF(), to.redF(), qRed(row[x])), channel(from.greenF(), to.greenF(), qGreen(row[x])),
                           channel(from.blueF(), to.blueF(), qBlue(row[x])), al);
        }
    }
}

void blackWhite(QImage& image, double amount)
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
            int const r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
            int const grey = (r * 30 + g * 59 + b * 11) / 100;
            auto one = [&](int v) { return v + ((grey - v) * k) / 256; };
            row[x] = qRgba(one(r), one(g), one(b), qAlpha(row[x]));
        }
    }
}

void matteResize(QImage& image, double radius, bool shrink)
{
    int const r = std::clamp(int(std::lround(radius)), 0, 100);
    if (r == 0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // Across, then down: each point the strongest (or weakest) of those so far either side - a square grown (or shrunk).
    auto pass = [&](bool across) {
        QImage const source = image;
        int const length = across ? w : h, lines = across ? h : w;
        for (int line = 0; line < lines; ++line)
            for (int i = 0; i < length; ++i) {
                int best[4] = {shrink ? 255 : 0, shrink ? 255 : 0, shrink ? 255 : 0, shrink ? 255 : 0};
                for (int k = std::max(0, i - r); k <= std::min(length - 1, i + r); ++k) {
                    QRgb const p = across ? reinterpret_cast<QRgb const*>(source.constScanLine(line))[k]
                                          : reinterpret_cast<QRgb const*>(source.constScanLine(k))[line];
                    int const v[4] = {qRed(p), qGreen(p), qBlue(p), qAlpha(p)};
                    for (int c = 0; c < 4; ++c)
                        best[c] = shrink ? std::min(best[c], v[c]) : std::max(best[c], v[c]);
                }
                // (Beyond the picture is clear: a shrink takes the edge away.)
                if (shrink && (i - r < 0 || i + r > length - 1))
                    best[0] = best[1] = best[2] = best[3] = 0;
                QRgb& out = across ? reinterpret_cast<QRgb*>(image.scanLine(line))[i] : reinterpret_cast<QRgb*>(image.scanLine(i))[line];
                out = qRgba(std::min(best[0], best[3]), std::min(best[1], best[3]), std::min(best[2], best[3]), best[3]);
            }
    };
    pass(true);
    pass(false);
}

void matteGenerator(QImage& image, double inner, double outer, QColor const& colour)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    if (w == 0 || h == 0)
        return;
    inner = std::max(0.0, inner);
    outer = std::max(inner, outer);
    // How far each point is from what is painted (a chamfer distance: 3 a step across or down, 4 one aslant).
    constexpr int kFar = 1 << 28;
    std::vector<int> d(size_t(w) * h, kFar);
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(row[x]) >= 128)
                d[size_t(y) * w + x] = 0;
    }
    auto at = [&](int x, int y) -> int& { return d[size_t(y) * w + x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int& v = at(x, y);
            if (x > 0) v = std::min(v, at(x - 1, y) + 3);
            if (y > 0) {
                v = std::min(v, at(x, y - 1) + 3);
                if (x > 0) v = std::min(v, at(x - 1, y - 1) + 4);
                if (x + 1 < w) v = std::min(v, at(x + 1, y - 1) + 4);
            }
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            int& v = at(x, y);
            if (x + 1 < w) v = std::min(v, at(x + 1, y) + 3);
            if (y + 1 < h) {
                v = std::min(v, at(x, y + 1) + 3);
                if (x + 1 < w) v = std::min(v, at(x + 1, y + 1) + 4);
                if (x > 0) v = std::min(v, at(x - 1, y + 1) + 4);
            }
        }
    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double const away = at(x, y) >= kFar ? 1e9 : at(x, y) / 3.0;
            double share = away <= inner ? 1.0 : away >= outer ? 0.0 : (outer - away) / (outer - inner);
            share = share * share * (3.0 - 2.0 * share);        // eased at both ends
            int const a = int(std::lround(share * colour.alpha()));
            row[x] = qRgba(colour.red() * a / 255, colour.green() * a / 255, colour.blue() * a / 255, a);
        }
    }
}

void halo(QImage& image, QColor const& color, double radius, QPointF offset)
{
    Deep::prepare(image);
    // The shape of the picture, all in the one colour (as deep as the picture).
    QImage soft(image.size(), image.format());
    double const ca = color.alphaF();
    for (int y = 0; y < image.height(); ++y) {
        Deep::ConstLine const from(image, y);
        Deep::Line const to(soft, y);
        for (int x = 0; x < image.width(); ++x) {
            double const a = from.alpha(x) * ca;
            to.set(x, {color.redF() * a, color.greenF() * a, color.blueF() * a, a});
        }
    }
    blur(soft, radius);
    QImage out(image.size(), image.format());
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.drawImage(offset, soft);
        painter.drawImage(0, 0, image);
    }
    out.setDevicePixelRatio(image.devicePixelRatio());
    image = out;
}

}  // namespace Effects
