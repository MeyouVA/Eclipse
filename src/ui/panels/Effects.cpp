#include "ui/panels/Effects.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QDir>
#include <QJSEngine>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QTemporaryDir>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"

namespace Effects {

// One pass of a box blur along the rows, then along the columns.  What lies outside the
// picture counts as transparent, so a blurred edge fades out.
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
    if (length < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // Samples along the line, from one end to the other, averaged (no more than 48 of them).
    int const samples = std::clamp(int(std::ceil(length)), 2, 48);
    double const turn = degrees * 3.14159265358979323846 / 180.0;
    QPointF const along(std::cos(turn), -std::sin(turn));
    QVector<QPoint> steps;
    for (int s = 0; s < samples; ++s) {
        double const t = (double(s) / (samples - 1) - 0.5) * length;
        steps.push_back(QPoint(int(std::lround(along.x() * t)), int(std::lround(along.y() * t))));
    }
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0;
            for (QPoint const& step : std::as_const(steps)) {
                int const sx = x + step.x(), sy = y + step.y();
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    continue;       // beyond the picture: nothing (so the edge fades out)
                QRgb const p = reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
                r += qRed(p);
                g += qGreen(p);
                b += qBlue(p);
                a += qAlpha(p);
            }
            out[x] = qRgba(r / samples, g / samples, b / samples, a / samples);
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
    int const samples = 24;
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
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // The shape of the picture, all in the one colour.
    QImage soft(image.size(), QImage::Format_ARGB32_Premultiplied);
    int const ca = color.alpha();
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const from = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        QRgb* const to = reinterpret_cast<QRgb*>(soft.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(from[x]) * ca / 255;
            to[x] = qRgba(color.red() * a / 255, color.green() * a / 255, color.blue() * a / 255, a);
        }
    }
    blur(soft, radius);
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.drawImage(offset, soft);
        painter.drawImage(0, 0, image);
    }
    out.setDevicePixelRatio(image.devicePixelRatio());
    image = out;
}

QString name(Effect const& effect, Scene const& scene)
{
    switch (effect.type) {
    case Effect::Glow:
        return QObject::tr("Glow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Shadow:
        return QObject::tr("Drop Shadow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Blur:
        return QObject::tr("Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Tint:
        return QObject::tr("Colour Tint  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Particles:
        return QObject::tr("Particles  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Transparency:
        return QObject::tr("Transparency  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::DirectionalBlur:
        return QObject::tr("Directional Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::HueSaturation:
        return QObject::tr("Hue-Saturation  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::BrightnessContrast:
        return QObject::tr("Brightness-Contrast  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Grain:
        return QObject::tr("Grain  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::RadialBlur:
        return QObject::tr("Radial Blur  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::ColourScale:
        return QObject::tr("Colour-Scale  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Negate:
        return QObject::tr("Negate  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Median:
        return QObject::tr("Median  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Ripple:
        return QObject::tr("Ripple  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Bloom:
        return QObject::tr("Bloom  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Pixelate:
        return QObject::tr("Pixelate  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Turbulence:
        return QObject::tr("Turbulence  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Gradient:
        return QObject::tr("Gradient  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::BlackWhite:
        return QObject::tr("Black & White  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::MatteResize:
        return QObject::tr("Matte Resize  %1%2").arg(effect.invert ? QStringLiteral("-") : QStringLiteral("+")).arg(effect.amount, 0, 'f', 1);
    case Effect::MatteBlur: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Matte Blur  %1  %2%3").arg(effect.amount, 0, 'f', 1)
            .arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"), effect.invert ? QObject::tr("  inverted") : QString());
    }
    case Effect::MatteGenerator:
        return QObject::tr("Matte Generator  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::ChannelSelector: {
        static char const* const channels[4] = {QT_TR_NOOP("Red"), QT_TR_NOOP("Green"), QT_TR_NOOP("Blue"), QT_TR_NOOP("Opacity")};
        return QObject::tr("Channel Selector  %1").arg(QObject::tr(channels[std::clamp(effect.channel, 0, 3)]));
    }
    case Effect::Crop:
        return effect.area.isEmpty() ? QObject::tr("Crop  (all)")
                                     : QObject::tr("Crop  %1 x %2").arg(effect.area.width(), 0, 'f', 0).arg(effect.area.height(), 0, 'f', 0);
    case Effect::RemoveTransparency:
        return QObject::tr("Remove Transparency  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::MatteComposite:
        return QObject::tr("Matte Composite");
    case Effect::MotionBlur:
        return QObject::tr("Motion Blur  %1 frames").arg(int(std::lround(effect.amount)));
    case Effect::FieldChart:
        return QObject::tr("Field Chart");
    case Effect::ColourCurves:
        return QObject::tr("Colour Curves");
    case Effect::ApplyPeg: {
        int const l = scene.layerIndex(effect.matte);
        return QObject::tr("Apply Peg  %1").arg(l >= 0 ? scene.layers[l].name : QObject::tr("(none)"));
    }
    case Effect::ScaleOutput:
        return QObject::tr("Scale Output  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Focus:
        return QObject::tr("Focus  %1 at %2").arg(effect.amount, 0, 'f', 1).arg(effect.direction, 0, 'f', 0);
    case Effect::Blending:
        return QObject::tr("Blending");
    case Effect::Glue:
        return QObject::tr("Glue  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Script:
        return QObject::tr("Script");
    case Effect::External: {
        QString const program = effect.text.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
        return program.isEmpty() ? QObject::tr("External") : QObject::tr("External  %1").arg(program);
    }
    case Effect::VariableBlur: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Variable Blur  %1  %2").arg(effect.amount, 0, 'f', 1).arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"));
    }
    case Effect::Tone:
    case Effect::Highlight: {
        int const matte = scene.layerIndex(effect.matte);
        return (effect.type == Effect::Tone ? QObject::tr("Tone  %1%2") : QObject::tr("Highlight  %1%2"))
            .arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"), effect.invert ? QObject::tr("  inverted") : QString());
    }
    default: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Cutter  %1%2").arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"),
                                               effect.invert ? QObject::tr("  inverted") : QString());
    }
    }
}

}

void Effects::channelSelect(QImage& image, int channel)
{
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const c = line[x];
            int const a = qAlpha(c);
            if (channel == 3) {
                line[x] = qRgba(a, a, a, 255);
                continue;
            }
            // (Premultiplied: the channel as it would be with the picture opaque, then as see-through as it was.)
            int const v = a > 0 ? std::min(255, (channel == 0 ? qRed(c) : channel == 1 ? qGreen(c) : qBlue(c)) * 255 / a) : 0;
            int const p = v * a / 255;
            line[x] = qRgba(p, p, p, a);
        }
    }
}

void Effects::crop(QImage& image, QRectF const& keep)
{
    QPainter p(&image);
    p.resetTransform();
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    QPainterPath outside;
    outside.addRect(QRectF(0, 0, image.width(), image.height()));
    QPainterPath inside;
    inside.addRect(keep);
    p.fillPath(outside.subtracted(inside), Qt::black);
}

void Effects::removeTransparency(QImage& image, double threshold)
{
    int const edge = std::clamp(int(std::lround(threshold * 255.0)), 0, 255);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const c = line[x];
            int const a = qAlpha(c);
            if (a == 0 || a < edge) {
                line[x] = 0;
                continue;
            }
            line[x] = qRgba(std::min(255, qRed(c) * 255 / a), std::min(255, qGreen(c) * 255 / a), std::min(255, qBlue(c) * 255 / a), 255);
        }
    }
}

void Effects::matteComposite(QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(line[x]);
            line[x] = qRgba(a, a, a, a);
        }
    }
}

void Effects::blendOnto(QImage& under, QImage const& top, int mode)
{
    static QPainter::CompositionMode const kModes[Layer::BlendCount] = {
        QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Multiply, QPainter::CompositionMode_Screen,
        QPainter::CompositionMode_Overlay, QPainter::CompositionMode_Darken, QPainter::CompositionMode_Lighten,
        QPainter::CompositionMode_ColorDodge, QPainter::CompositionMode_ColorBurn, QPainter::CompositionMode_HardLight,
        QPainter::CompositionMode_SoftLight, QPainter::CompositionMode_Difference, QPainter::CompositionMode_Exclusion,
        QPainter::CompositionMode_Plus};
    QPainter p(&under);
    p.resetTransform();
    p.setCompositionMode(kModes[std::clamp(mode, 0, int(Layer::BlendCount) - 1)]);
    p.drawImage(0, 0, top);
}

void Effects::variableBlur(QImage& image, QImage const& matte, double radius)
{
    QImage blurred = image;
    blur(blurred, radius);
    for (int y = 0; y < image.height() && y < matte.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        QRgb const* const soft = reinterpret_cast<QRgb const*>(blurred.constScanLine(y));
        QRgb const* const by = reinterpret_cast<QRgb const*>(matte.constScanLine(y));
        for (int x = 0; x < image.width() && x < matte.width(); ++x) {
            // How much: the matte's brightness, where it is (premultiplied: its grey is already as faint as it is).
            int const k = (qRed(by[x]) * 299 + qGreen(by[x]) * 587 + qBlue(by[x]) * 114) / 1000;
            if (k == 0)
                continue;
            QRgb const a = line[x], b = soft[x];
            auto mix = [k](int p, int q) { return p + (q - p) * k / 255; };
            line[x] = qRgba(mix(qRed(a), qRed(b)), mix(qGreen(a), qGreen(b)), mix(qBlue(a), qBlue(b)), mix(qAlpha(a), qAlpha(b)));
        }
    }
}

void Effects::fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width)
{
    QPainter p(&image);
    p.resetTransform();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(colour, std::max(0.5, width)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);
    // Twelve fields across: six either side of the middle, and as many down as the frame's shape allows.
    QPointF const middle = frame.center();
    double const field = frame.width() / 12.0;
    for (int k = 1; k <= 6; ++k) {
        p.drawLine(QPointF(middle.x() - k * field, frame.top()), QPointF(middle.x() - k * field, frame.bottom()));
        p.drawLine(QPointF(middle.x() + k * field, frame.top()), QPointF(middle.x() + k * field, frame.bottom()));
        if (k * field <= frame.height() / 2.0 + 0.5) {
            p.drawLine(QPointF(frame.left(), middle.y() - k * field), QPointF(frame.right(), middle.y() - k * field));
            p.drawLine(QPointF(frame.left(), middle.y() + k * field), QPointF(frame.right(), middle.y() + k * field));
        }
    }
    p.setPen(QPen(colour, std::max(1.0, width * 2.0)));
    p.drawLine(QPointF(middle.x(), frame.top()), QPointF(middle.x(), frame.bottom()));
    p.drawLine(QPointF(frame.left(), middle.y()), QPointF(frame.right(), middle.y()));
    p.setPen(QPen(colour, std::max(0.5, width), Qt::DashLine));
    p.drawLine(frame.topLeft(), frame.bottomRight());
    p.drawLine(frame.topRight(), frame.bottomLeft());
}

QVector<int> Effects::curveLevels(QVector<QPointF> const& points)
{
    QVector<int> levels(256);
    int const n = int(points.size());
    if (n == 0) {
        for (int i = 0; i < 256; ++i)
            levels[i] = i;
        return levels;
    }
    // A smooth curve that does not overshoot (Fritsch-Carlson): the slope at each point between those either side of it, cut
    // back where it would turn the curve back.
    QVector<double> slope(n, 0.0);
    QVector<double> step(std::max(0, n - 1), 0.0);
    for (int k = 0; k + 1 < n; ++k) {
        double const dx = points[k + 1].x() - points[k].x();
        step[k] = dx > 1e-9 ? (points[k + 1].y() - points[k].y()) / dx : 0.0;
    }
    for (int k = 0; k < n; ++k) {
        if (k == 0)
            slope[k] = n > 1 ? step[0] : 0.0;
        else if (k == n - 1)
            slope[k] = step[k - 1];
        else
            slope[k] = step[k - 1] * step[k] <= 0.0 ? 0.0 : (step[k - 1] + step[k]) / 2.0;
    }
    for (int k = 0; k + 1 < n; ++k) {
        if (std::abs(step[k]) < 1e-12) {
            slope[k] = slope[k + 1] = 0.0;
            continue;
        }
        double const a = slope[k] / step[k], b = slope[k + 1] / step[k];
        if (a * a + b * b > 9.0) {
            double const t = 3.0 / std::sqrt(a * a + b * b);
            slope[k] = t * a * step[k];
            slope[k + 1] = t * b * step[k];
        }
    }
    for (int i = 0; i < 256; ++i) {
        double const x = i / 255.0;
        double y = 0.0;
        if (x <= points.front().x()) {
            y = points.front().y();
        } else if (x >= points.back().x()) {
            y = points.back().y();
        } else {
            int k = 0;
            while (k + 1 < n - 1 && x > points[k + 1].x())
                ++k;
            double const h = points[k + 1].x() - points[k].x();
            double const t = h > 1e-9 ? (x - points[k].x()) / h : 0.0;
            double const t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * points[k].y() + (t3 - 2 * t2 + t) * h * slope[k] + (-2 * t3 + 3 * t2) * points[k + 1].y()
                + (t3 - t2) * h * slope[k + 1];
        }
        levels[i] = std::clamp(int(std::lround(y * 255.0)), 0, 255);
    }
    return levels;
}

void Effects::colourCurves(QImage& image, QVector<QPointF> const (&curves)[4], double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    // A table for each colour: the curve of the three together, then its own.
    QVector<int> const all = curveLevels(curves[0]);
    int table[3][256];
    for (int c = 0; c < 3; ++c) {
        QVector<int> const own = curveLevels(curves[c + 1]);
        for (int i = 0; i < 256; ++i) {
            int const to = own[all[i]];
            table[c][i] = std::clamp(int(std::lround(i + (to - i) * amount)), 0, 255);
        }
    }
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const px = line[x];
            int const a = qAlpha(px);
            if (a == 0)
                continue;
            // (On the colours as they are, not as they are multiplied by how opaque.)
            QRgb const plain = qUnpremultiply(px);
            line[x] = qPremultiply(qRgba(table[0][qRed(plain)], table[1][qGreen(plain)], table[2][qBlue(plain)], a));
        }
    }
}

void Effects::transformImage(QImage& image, QTransform const& transform)
{
    if (transform.isIdentity())
        return;
    double const dpr = image.devicePixelRatio();
    QImage source = image;
    source.setDevicePixelRatio(1.0);
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(transform);
        p.drawImage(0, 0, source);
    }
    out.setDevicePixelRatio(dpr);
    image = out;
}

void Effects::scaleOutput(QImage& image, double scale)
{
    scale = std::clamp(scale, 0.01, 1.0);
    if (scale >= 1.0 || image.isNull())
        return;
    double const dpr = image.devicePixelRatio();
    QSize const small(std::max(1, int(std::lround(image.width() * scale))), std::max(1, int(std::lround(image.height() * scale))));
    QImage out = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    image = out;
}

void Effects::glue(QImage& under, QImage const& top, double radius)
{
    double const dpr = under.devicePixelRatio();
    if (under.format() != QImage::Format_ARGB32_Premultiplied)
        under = under.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage both = under;
    both.setDevicePixelRatio(1.0);
    {
        QPainter p(&both);
        QImage t = top;
        t.setDevicePixelRatio(1.0);
        p.drawImage(0, 0, t);
    }
    // Where the glue goes: what is painted grown and shrunk again by as much - the gaps narrower than that closed.
    QImage closed = both;
    matteResize(closed, radius / 2.0, false);
    matteResize(closed, radius / 2.0, true);
    // Its colours: those of what is painted nearby, spread out.
    QImage spread = both;
    blur(spread, radius);
    QImage out(both.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < both.height(); ++y) {
        QRgb const* const b = reinterpret_cast<QRgb const*>(both.constScanLine(y));
        QRgb const* const c = reinterpret_cast<QRgb const*>(closed.constScanLine(y));
        QRgb const* const s = reinterpret_cast<QRgb const*>(spread.constScanLine(y));
        QRgb* const o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < both.width(); ++x) {
            int const glueAlpha = std::max(0, qAlpha(c[x]) - qAlpha(b[x]));
            if (glueAlpha == 0 || qAlpha(s[x]) == 0) {
                o[x] = b[x];
                continue;
            }
            // The glue, under what is painted there.
            QRgb const colour = qUnpremultiply(s[x]);
            int const a = qAlpha(b[x]), ga = glueAlpha * (255 - a) / 255;
            auto mix = [&](int painted, int glued) { return std::clamp(painted + glued * ga / 255, 0, 255); };
            o[x] = qRgba(mix(qRed(b[x]), qRed(colour)), mix(qGreen(b[x]), qGreen(colour)), mix(qBlue(b[x]), qBlue(colour)), std::min(255, a + ga));
        }
    }
    out.setDevicePixelRatio(dpr);
    under = out;
}

namespace {

// What a script calls `image`.
class ScriptImage : public QObject {
    Q_OBJECT
    Q_PROPERTY(int width READ width)
    Q_PROPERTY(int height READ height)

public:
    ScriptImage(QImage& image, double unit) : m_image(image), m_unit(unit) {}
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    Q_INVOKABLE QString pixel(int x, int y) const
    {
        return m_image.valid(x, y) ? m_image.pixelColor(x, y).name(QColor::HexArgb) : QString();
    }
    Q_INVOKABLE void setPixel(int x, int y, QString const& colour)
    {
        if (m_image.valid(x, y))
            m_image.setPixelColor(x, y, QColor(colour));
    }
    Q_INVOKABLE void blur(double radius) { Effects::blur(m_image, radius * m_unit); }
    Q_INVOKABLE void tint(QString const& colour, double amount) { Effects::tint(m_image, QColor(colour), amount); }
    Q_INVOKABLE void transparency(double amount) { Effects::transparency(m_image, amount); }
    Q_INVOKABLE void negate(double amount) { Effects::negate(m_image, amount); }
    Q_INVOKABLE void blackWhite(double amount) { Effects::blackWhite(m_image, amount); }
    Q_INVOKABLE void hueSaturation(double hue, double saturation, double lightness)
    {
        Effects::hueSaturation(m_image, hue, saturation, lightness, 1.0);
    }
    Q_INVOKABLE void brightnessContrast(double brightness, double contrast) { Effects::brightnessContrast(m_image, brightness, contrast, 1.0); }
    Q_INVOKABLE void pixelate(double size) { Effects::pixelate(m_image, size * m_unit); }
    Q_INVOKABLE void median(double radius) { Effects::median(m_image, radius * m_unit); }
    Q_INVOKABLE void bloom(double radius) { Effects::bloom(m_image, radius * m_unit); }
    Q_INVOKABLE void grain(double amount) { Effects::grain(m_image, amount, 1); }
    Q_INVOKABLE void directionalBlur(double length, double degrees) { Effects::directionalBlur(m_image, length * m_unit, degrees); }
    Q_INVOKABLE void colourScale(QString const& colour, double amount) { Effects::colourScale(m_image, QColor(colour), amount); }

private:
    QImage& m_image;
    double m_unit;
};

}

QString Effects::runScript(QImage& image, QString const& script, int frame, double amount, double unit)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QJSEngine engine;
    engine.setInterrupted(false);
    ScriptImage picture(image, unit);
    engine.globalObject().setProperty(QStringLiteral("image"), engine.newQObject(&picture));
    QJSEngine::setObjectOwnership(&picture, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("frame"), frame + 1);
    engine.globalObject().setProperty(QStringLiteral("amount"), amount);
    QJSValue const result = engine.evaluate(script);
    if (result.isError())
        return QObject::tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString());
    return QString();
}

QString Effects::runExternal(QImage& image, QString const& command, int frame, int seconds)
{
    QStringList words = QProcess::splitCommand(command);
    if (words.isEmpty())
        return QObject::tr("No command line.");
    QTemporaryDir dir;
    if (!dir.isValid())
        return QObject::tr("No room for the pictures.");
    QString const in = QDir::toNativeSeparators(dir.filePath(QStringLiteral("in.png")));
    QString const out = QDir::toNativeSeparators(dir.filePath(QStringLiteral("out.png")));
    double const dpr = image.devicePixelRatio();
    if (!image.save(in, "PNG"))
        return QObject::tr("The picture could not be written.");
    for (QString& w : words)
        w.replace(QStringLiteral("{in}"), in).replace(QStringLiteral("{out}"), out).replace(QStringLiteral("{frame}"), QString::number(frame + 1));
    QProcess process;
    process.setWorkingDirectory(dir.path());
    process.start(words.takeFirst(), words);
    if (!process.waitForStarted(10000))
        return QObject::tr("The program could not be started.");
    if (!process.waitForFinished(std::max(1, seconds) * 1000)) {
        process.kill();
        process.waitForFinished(2000);
        return QObject::tr("The program took too long.");
    }
    QImage made(out);
    if (made.isNull())
        return QObject::tr("The program made no picture at {out}.");
    made = made.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    made.setDevicePixelRatio(dpr);
    image = made;
    return QString();
}

// ---- the curve to drag --------------------------------------------------------------------

ColourCurveEdit::ColourCurveEdit(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ColourCurveEdit"));
    setMinimumSize(120, 120);
    setPoints({});
}

void ColourCurveEdit::setPoints(QVector<QPointF> const& points)
{
    m_points = points;
    if (m_points.size() < 2)
        m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    update();
}

QRectF ColourCurveEdit::box() const
{
    return QRectF(rect()).adjusted(6, 6, -6, -6);
}

QPointF ColourCurveEdit::toWidget(QPointF const& level) const
{
    QRectF const b = box();
    return QPointF(b.left() + level.x() * b.width(), b.bottom() - level.y() * b.height());
}

QPointF ColourCurveEdit::toLevel(QPointF const& at) const
{
    QRectF const b = box();
    return QPointF(std::clamp((at.x() - b.left()) / std::max(1.0, b.width()), 0.0, 1.0),
                   std::clamp((b.bottom() - at.y()) / std::max(1.0, b.height()), 0.0, 1.0));
}

int ColourCurveEdit::pointAt(QPointF const& at) const
{
    for (int k = 0; k < m_points.size(); ++k)
        if (QLineF(toWidget(m_points[k]), at).length() <= 6.0)
            return k;
    return -1;
}

void ColourCurveEdit::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF const b = box();
    p.fillRect(b, QColor(36, 36, 36));
    p.setPen(QColor(64, 64, 64));
    for (int k = 1; k < 4; ++k) {
        p.drawLine(QPointF(b.left() + b.width() * k / 4.0, b.top()), QPointF(b.left() + b.width() * k / 4.0, b.bottom()));
        p.drawLine(QPointF(b.left(), b.top() + b.height() * k / 4.0), QPointF(b.right(), b.top() + b.height() * k / 4.0));
    }
    p.setPen(QPen(QColor(90, 90, 90), 1.0, Qt::DashLine));
    p.drawLine(b.bottomLeft(), b.topRight());
    QVector<int> const levels = Effects::curveLevels(m_points);
    QPainterPath curve;
    for (int i = 0; i < 256; ++i) {
        QPointF const at = toWidget(QPointF(i / 255.0, levels[i] / 255.0));
        if (i == 0)
            curve.moveTo(at);
        else
            curve.lineTo(at);
    }
    p.setPen(QPen(QColor(230, 230, 230), 1.5));
    p.drawPath(curve);
    p.setBrush(QColor(255, 200, 80));
    p.setPen(Qt::black);
    for (QPointF const& point : m_points)
        p.drawEllipse(toWidget(point), 4.0, 4.0);
}

void ColourCurveEdit::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragged = pointAt(event->position());
    if (m_dragged < 0) {
        // A new point, where the click is (between the ends).
        QPointF const level = toLevel(event->position());
        int at = 0;
        while (at < m_points.size() && m_points[at].x() < level.x())
            ++at;
        if (at == 0 || at == m_points.size())
            return;
        m_points.insert(at, level);
        m_dragged = at;
    }
    update();
}

void ColourCurveEdit::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragged < 0)
        return;
    QPointF level = toLevel(event->position());
    // A point stays between the ones either side of it; the ends stay at the ends.
    if (m_dragged == 0)
        level.setX(0.0);
    else if (m_dragged == m_points.size() - 1)
        level.setX(1.0);
    else
        level.setX(std::clamp(level.x(), m_points[m_dragged - 1].x() + 0.01, m_points[m_dragged + 1].x() - 0.01));
    m_points[m_dragged] = level;
    update();
}

void ColourCurveEdit::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_dragged < 0)
        return;
    m_dragged = -1;
    emit edited(m_points);
}

void ColourCurveEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    int const k = pointAt(event->position());
    if (k <= 0 || k >= m_points.size() - 1)
        return;
    m_points.removeAt(k);
    m_dragged = -1;
    update();
    emit edited(m_points);
}

// ---- the panel ----------------------------------------------------------------------------

EffectsPanel::EffectsPanel(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("EffectsList"));
    m_list->setMaximumHeight(96);
    QToolButton* const addButton = new QToolButton(this);
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(tr("Add an effect to the layer"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* const menu = new QMenu(addButton);
    connect(menu->addAction(tr("Blur")), &QAction::triggered, this, [this] { add(Effect::Blur); });
    connect(menu->addAction(tr("Colour Tint")), &QAction::triggered, this, [this] { add(Effect::Tint); });
    connect(menu->addAction(tr("Cutter")), &QAction::triggered, this, [this] { add(Effect::Cutter); });
    connect(menu->addAction(tr("Glow")), &QAction::triggered, this, [this] { add(Effect::Glow); });
    connect(menu->addAction(tr("Drop Shadow")), &QAction::triggered, this, [this] { add(Effect::Shadow); });
    connect(menu->addAction(tr("Particles")), &QAction::triggered, this, [this] { add(Effect::Particles); });
    connect(menu->addAction(tr("Transparency")), &QAction::triggered, this, [this] { add(Effect::Transparency); });
    connect(menu->addAction(tr("Directional Blur")), &QAction::triggered, this, [this] { add(Effect::DirectionalBlur); });
    connect(menu->addAction(tr("Hue-Saturation")), &QAction::triggered, this, [this] { add(Effect::HueSaturation); });
    connect(menu->addAction(tr("Brightness-Contrast")), &QAction::triggered, this, [this] { add(Effect::BrightnessContrast); });
    connect(menu->addAction(tr("Grain")), &QAction::triggered, this, [this] { add(Effect::Grain); });
    connect(menu->addAction(tr("Radial Blur")), &QAction::triggered, this, [this] { add(Effect::RadialBlur); });
    connect(menu->addAction(tr("Colour-Scale")), &QAction::triggered, this, [this] { add(Effect::ColourScale); });
    connect(menu->addAction(tr("Negate")), &QAction::triggered, this, [this] { add(Effect::Negate); });
    connect(menu->addAction(tr("Median")), &QAction::triggered, this, [this] { add(Effect::Median); });
    connect(menu->addAction(tr("Ripple")), &QAction::triggered, this, [this] { add(Effect::Ripple); });
    connect(menu->addAction(tr("Tone")), &QAction::triggered, this, [this] { add(Effect::Tone); });
    connect(menu->addAction(tr("Highlight")), &QAction::triggered, this, [this] { add(Effect::Highlight); });
    connect(menu->addAction(tr("Bloom")), &QAction::triggered, this, [this] { add(Effect::Bloom); });
    connect(menu->addAction(tr("Pixelate")), &QAction::triggered, this, [this] { add(Effect::Pixelate); });
    connect(menu->addAction(tr("Turbulence")), &QAction::triggered, this, [this] { add(Effect::Turbulence); });
    connect(menu->addAction(tr("Matte Generator")), &QAction::triggered, this, [this] { add(Effect::MatteGenerator); });
    connect(menu->addAction(tr("Black & White")), &QAction::triggered, this, [this] { add(Effect::BlackWhite); });
    connect(menu->addAction(tr("Matte Resize")), &QAction::triggered, this, [this] { add(Effect::MatteResize); });
    connect(menu->addAction(tr("Matte Blur")), &QAction::triggered, this, [this] { add(Effect::MatteBlur); });
    connect(menu->addAction(tr("Gradient")), &QAction::triggered, this, [this] { add(Effect::Gradient); });
    connect(menu->addAction(tr("Channel Selector")), &QAction::triggered, this, [this] { add(Effect::ChannelSelector); });
    connect(menu->addAction(tr("Crop")), &QAction::triggered, this, [this] { add(Effect::Crop); });
    connect(menu->addAction(tr("Remove Transparency")), &QAction::triggered, this, [this] { add(Effect::RemoveTransparency); });
    connect(menu->addAction(tr("Matte Composite")), &QAction::triggered, this, [this] { add(Effect::MatteComposite); });
    connect(menu->addAction(tr("Variable Blur")), &QAction::triggered, this, [this] { add(Effect::VariableBlur); });
    connect(menu->addAction(tr("Motion Blur")), &QAction::triggered, this, [this] { add(Effect::MotionBlur); });
    connect(menu->addAction(tr("Field Chart")), &QAction::triggered, this, [this] { add(Effect::FieldChart); });
    connect(menu->addAction(tr("Colour Curves")), &QAction::triggered, this, [this] { add(Effect::ColourCurves); });
    connect(menu->addAction(tr("Focus")), &QAction::triggered, this, [this] { add(Effect::Focus); });
    connect(menu->addAction(tr("Apply Peg Transformation")), &QAction::triggered, this, [this] { add(Effect::ApplyPeg); });
    connect(menu->addAction(tr("Scale Output")), &QAction::triggered, this, [this] { add(Effect::ScaleOutput); });
    connect(menu->addAction(tr("Script")), &QAction::triggered, this, [this] { add(Effect::Script); });
    connect(menu->addAction(tr("External")), &QAction::triggered, this, [this] { add(Effect::External); });
    addButton->setMenu(menu);
    QToolButton* const removeButton = new QToolButton(this);
    removeButton->setText(QStringLiteral("-"));
    removeButton->setToolTip(tr("Remove the selected effect"));
    connect(removeButton, &QToolButton::clicked, this, &EffectsPanel::removeCurrent);

    m_amountLabel = new QLabel(this);
    m_amount = new QDoubleSpinBox(this);
    m_amount->setKeyboardTracking(false);
    m_color = new QPushButton(this);
    m_color->setFixedWidth(60);
    m_endColor = new QPushButton(this);
    m_endColor->setObjectName(QStringLiteral("EffectEndColour"));
    m_endColor->setFixedWidth(60);
    m_endColor->setToolTip(tr("The colour the gradient goes to."));
    m_matteLabel = new QLabel(tr("Matte"), this);
    m_matte = new QComboBox(this);
    m_matte->setToolTip(tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert = new QCheckBox(tr("Inverted"), this);
    m_invert->setToolTip(tr("Keep what lies outside the matte instead."));
    m_animated = new QCheckBox(tr("Animate"), this);
    m_animated->setToolTip(tr("When ticked, changing the amount sets a keyframe on the current frame, and the amount glides between keyframes."));
    m_offsetLabel = new QLabel(tr("Offset"), this);
    m_offsetX = new QDoubleSpinBox(this);
    m_offsetY = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* box : {m_offsetX, m_offsetY}) {
        box->setRange(-2000.0, 2000.0);
        box->setDecimals(1);
        box->setKeyboardTracking(false);
    }

    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel(tr("Effects"), this));
    buttons->addStretch();
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    QHBoxLayout* const amountRow = new QHBoxLayout;
    amountRow->addWidget(m_amountLabel);
    amountRow->addWidget(m_amount, 1);
    amountRow->addWidget(m_color);
    amountRow->addWidget(m_endColor);
    amountRow->addWidget(m_animated);
    QHBoxLayout* const offsetRow = new QHBoxLayout;
    offsetRow->addWidget(m_offsetLabel);
    offsetRow->addWidget(m_offsetX, 1);
    offsetRow->addWidget(m_offsetY, 1);
    QHBoxLayout* const matteRow = new QHBoxLayout;
    matteRow->addWidget(m_matteLabel);
    matteRow->addWidget(m_matte, 1);
    matteRow->addWidget(m_invert);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_list);
    layout->addLayout(amountRow);
    layout->addLayout(offsetRow);
    layout->addLayout(matteRow);
    {
        // Hue-Saturation, Brightness-Contrast, Directional Blur: what they change, and by how much.
        m_adjust = new QWidget(this);
        m_adjust->setObjectName(QStringLiteral("AdjustSettings"));
        m_adjustForm = new QFormLayout(m_adjust);
        m_adjustForm->setContentsMargins(0, 0, 0, 0);
        struct { char const* name; char const* label; char const* tip; double low, high; } const kBoxes[6] = {
            {"AdjustHue", QT_TR_NOOP("Hue"), QT_TR_NOOP("How far round the colour wheel each colour is turned (degrees)."), -180, 180},
            {"AdjustSaturation", QT_TR_NOOP("Saturation"), QT_TR_NOOP("Colours made richer (more) or greyer (less), in percent."), -100, 100},
            {"AdjustLightness", QT_TR_NOOP("Lightness"), QT_TR_NOOP("Colours made lighter (more) or darker (less), in percent."), -100, 100},
            {"AdjustBrightness", QT_TR_NOOP("Brightness"), QT_TR_NOOP("The whole picture made brighter (more) or darker (less), in percent."), -100, 100},
            {"AdjustContrast", QT_TR_NOOP("Contrast"), QT_TR_NOOP("Lights lighter and darks darker (more), or all nearer grey (less), in percent."), -100, 100},
            {"BlurDirection", QT_TR_NOOP("Direction"), QT_TR_NOOP("Which way the picture is smeared (degrees: 0 across, 90 up and down)."), -360, 360}};
        for (int i = 0; i < 6; ++i) {
            m_adjustBox[i] = new QDoubleSpinBox(m_adjust);
            m_adjustBox[i]->setObjectName(QLatin1String(kBoxes[i].name));
            m_adjustBox[i]->setRange(kBoxes[i].low, kBoxes[i].high);
            m_adjustBox[i]->setDecimals(1);
            m_adjustBox[i]->setSingleStep(i == 5 ? 15.0 : 5.0);
            m_adjustBox[i]->setKeyboardTracking(false);
            m_adjustBox[i]->setToolTip(tr(kBoxes[i].tip));
            m_adjustForm->addRow(tr(kBoxes[i].label), m_adjustBox[i]);
            connect(m_adjustBox[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                if (m_rebuilding)
                    return;
                edit([i, v](Effect& e) {
                    double* const fields[6] = {&e.hue, &e.saturation, &e.lightness, &e.brightness, &e.contrast, &e.direction};
                    *fields[i] = v;
                });
            });
        }
        layout->addWidget(m_adjust);
    }
    {
        // Channel Selector: which channel; Crop: the area kept.
        m_more = new QWidget(this);
        m_more->setObjectName(QStringLiteral("MoreEffectSettings"));
        m_moreForm = new QFormLayout(m_more);
        m_moreForm->setContentsMargins(0, 0, 0, 0);
        m_channel = new QComboBox(m_more);
        m_channel->setObjectName(QStringLiteral("EffectChannel"));
        m_channel->addItems({tr("Red"), tr("Green"), tr("Blue"), tr("Opacity")});
        m_moreForm->addRow(tr("Channel"), m_channel);
        connect(m_channel, &QComboBox::currentIndexChanged, this, [this](int channel) {
            if (!m_rebuilding)
                edit([channel](Effect& e) { e.channel = channel; });
        });
        static char const* const cropNames[4] = {QT_TR_NOOP("Left"), QT_TR_NOOP("Top"), QT_TR_NOOP("Width"), QT_TR_NOOP("Height")};
        for (int i = 0; i < 4; ++i) {
            m_cropBox[i] = new QDoubleSpinBox(m_more);
            m_cropBox[i]->setObjectName(QStringLiteral("EffectCrop%1").arg(i));
            m_cropBox[i]->setRange(i < 2 ? -10000.0 : 0.0, 10000.0);
            m_cropBox[i]->setDecimals(1);
            m_cropBox[i]->setKeyboardTracking(false);
            m_moreForm->addRow(tr(cropNames[i]), m_cropBox[i]);
            connect(m_cropBox[i], &QDoubleSpinBox::valueChanged, this, [this] {
                if (m_rebuilding)
                    return;
                QRectF const area(m_cropBox[0]->value(), m_cropBox[1]->value(), m_cropBox[2]->value(), m_cropBox[3]->value());
                edit([area](Effect& e) { e.area = area; });
            });
        }
        // Colour Curves: the curve picked, to drag.
        m_curveWhich = new QComboBox(m_more);
        m_curveWhich->setObjectName(QStringLiteral("EffectCurveWhich"));
        m_curveWhich->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
        m_moreForm->addRow(tr("Curve"), m_curveWhich);
        m_curve = new ColourCurveEdit(m_more);
        m_moreForm->addRow(m_curve);
        connect(m_curveWhich, &QComboBox::currentIndexChanged, this, [this] { showEditors(); });
        // Script and External: the script or the command line, and Apply to use it.
        m_text = new QPlainTextEdit(m_more);
        m_text->setObjectName(QStringLiteral("EffectText"));
        m_text->setMinimumHeight(90);
        m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_moreForm->addRow(m_text);
        m_textApply = new QPushButton(tr("Apply"), m_more);
        m_textApply->setObjectName(QStringLiteral("EffectTextApply"));
        m_moreForm->addRow(m_textApply);
        m_textSays = new QLabel(m_more);
        m_textSays->setObjectName(QStringLiteral("EffectTextSays"));
        m_textSays->setWordWrap(true);
        m_moreForm->addRow(m_textSays);
        connect(m_textApply, &QPushButton::clicked, this, [this] {
            QString const text = m_text->toPlainText();
            Scene const& scene = m_document->scene();
            int const row = m_list->currentRow();
            bool const script = m_layer >= 0 && m_layer < scene.layers.size() && row >= 0 && row < scene.layers[m_layer].effects.size()
                                && scene.layers[m_layer].effects[row].type == Effect::Script;
            // A script tried out first on a small picture: what is wrong with it, said here.
            QString says;
            if (script) {
                QImage trial(8, 8, QImage::Format_ARGB32_Premultiplied);
                trial.fill(Qt::white);
                says = Effects::runScript(trial, text, m_frame, 1.0, 1.0);
            }
            edit([text](Effect& e) { e.text = text; });
            m_textSays->setText(says);
        });
        connect(m_curve, &ColourCurveEdit::edited, this, [this](QVector<QPointF> const& points) {
            int const which = std::clamp(m_curveWhich->currentIndex(), 0, 3);
            // (A straight line from corner to corner is no curve at all.)
            bool const straight = points.size() == 2 && points[0] == QPointF(0, 0) && points[1] == QPointF(1, 1);
            edit([which, points, straight](Effect& e) { e.curves[which] = straight ? QVector<QPointF>() : points; });
        });
        layout->addWidget(m_more);
    }
    {
        // Particles: how long each lives, how fast and which way it leaves, how it falls.
        m_particles = new QWidget(this);
        m_particles->setObjectName(QStringLiteral("ParticleSettings"));
        QFormLayout* const form = new QFormLayout(m_particles);
        form->setContentsMargins(0, 0, 0, 0);
        struct { char const* name; char const* label; double low, high, step; int decimals; } const kBoxes[6] = {
            {"ParticleLife", QT_TR_NOOP("Life (frames)"), 1, 600, 1, 0}, {"ParticleSpeed", QT_TR_NOOP("Speed"), 0, 500, 1, 1},
            {"ParticleSpread", QT_TR_NOOP("Spread"), 0, 360, 5, 0}, {"ParticleDirection", QT_TR_NOOP("Direction"), -360, 360, 5, 0},
            {"ParticleGravity", QT_TR_NOOP("Gravity"), -50, 50, 0.1, 2}, {"ParticleSeed", QT_TR_NOOP("Scattering"), 1, 9999, 1, 0}};
        for (int i = 0; i < 6; ++i) {
            m_particleBox[i] = new QDoubleSpinBox(m_particles);
            m_particleBox[i]->setObjectName(QLatin1String(kBoxes[i].name));
            m_particleBox[i]->setRange(kBoxes[i].low, kBoxes[i].high);
            m_particleBox[i]->setSingleStep(kBoxes[i].step);
            m_particleBox[i]->setDecimals(kBoxes[i].decimals);
            m_particleBox[i]->setKeyboardTracking(false);
            form->addRow(tr(kBoxes[i].label), m_particleBox[i]);
            connect(m_particleBox[i], &QDoubleSpinBox::valueChanged, this, [this] {
                if (m_rebuilding)
                    return;
                double v[6];
                for (int k = 0; k < 6; ++k)
                    v[k] = m_particleBox[k]->value();
                edit([v](Effect& e) {
                    e.life = int(v[0]);
                    e.speed = v[1];
                    e.spread = v[2];
                    e.direction = v[3];
                    e.gravity = v[4];
                    e.seed = int(v[5]);
                });
            });
        }
        // How big at the end of its life; and whether they move with the layer.
        m_particleBox[6] = new QDoubleSpinBox(m_particles);
        m_particleBox[6]->setObjectName(QStringLiteral("ParticleEndSize"));
        m_particleBox[6]->setRange(0.0, 20.0);
        m_particleBox[6]->setSingleStep(0.1);
        m_particleBox[6]->setDecimals(2);
        m_particleBox[6]->setKeyboardTracking(false);
        m_particleBox[6]->setToolTip(tr("How big a particle is when it dies, against how big it was when it left (1: the same)."));
        form->addRow(tr("Size at the End"), m_particleBox[6]);
        connect(m_particleBox[6], &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (!m_rebuilding)
                edit([v](Effect& e) { e.endSize = v; });
        });
        // Wind across, and turning.
        struct Extra { char const* name; char const* label; char const* tip; double low, high, step; };
        static Extra const kExtras[7] = {
            {"ParticleWind", QT_TR_NOOP("Wind"), QT_TR_NOOP("Pushes the particles across by so many units a frame, each frame (less than 0: to the left)."), -20.0, 20.0, 0.1},
            {"ParticleSpin", QT_TR_NOOP("Spin"), QT_TR_NOOP("Each particle turns by up to so many degrees a frame, one way or the other."), 0.0, 360.0, 1.0},
            {"ParticleRegionWidth", QT_TR_NOOP("Region Width"), QT_TR_NOOP("They are let go from anywhere in a box this wide about the drawing (0: from the drawing itself)."), 0.0, 5000.0, 10.0},
            {"ParticleRegionHeight", QT_TR_NOOP("Region Height"), QT_TR_NOOP("They are let go from anywhere in a box this high about the drawing (0: from the drawing itself)."), 0.0, 5000.0, 10.0},
            {"ParticleFloor", QT_TR_NOOP("Floor"), QT_TR_NOOP("A floor so far below where they are let go, that they bounce off (0: none)."), 0.0, 5000.0, 10.0},
            {"ParticleBounce", QT_TR_NOOP("Bounce"), QT_TR_NOOP("How much of its speed a particle keeps when it bounces off the floor or a wall (0: none, 1: all)."), 0.0, 1.0, 0.05},
            {"ParticleWalls", QT_TR_NOOP("Walls"), QT_TR_NOOP("Walls so far to the left and to the right of where they are let go, that they bounce off (0: none)."), 0.0, 5000.0, 10.0},
        };
        for (int i = 0; i < 7; ++i) {
            QDoubleSpinBox* const box = new QDoubleSpinBox(m_particles);
            m_particleBox[7 + i] = box;
            box->setObjectName(QLatin1String(kExtras[i].name));
            box->setRange(kExtras[i].low, kExtras[i].high);
            box->setSingleStep(kExtras[i].step);
            box->setDecimals(2);
            box->setKeyboardTracking(false);
            box->setToolTip(tr(kExtras[i].tip));
            form->addRow(tr(kExtras[i].label), box);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                if (!m_rebuilding)
                    edit([i, v](Effect& e) {
                        double* const fields[7] = {&e.wind, &e.spin, &e.regionWidth, &e.regionHeight, &e.floor, &e.bounce, &e.walls};
                        *fields[i] = v;
                    });
            });
        }
        // The shape of the region they are let go from.
        m_particleShape = new QComboBox(m_particles);
        m_particleShape->setObjectName(QStringLiteral("ParticleRegionShape"));
        m_particleShape->addItems({tr("Box"), tr("Round"), tr("Ring (its edge)"), tr("The drawing's shape")});
        form->addRow(tr("Region Shape"), m_particleShape);
        connect(m_particleShape, &QComboBox::currentIndexChanged, this, [this](int shape) {
            if (!m_rebuilding && shape >= 0)
                edit([shape](Effect& e) { e.regionShape = shape; });
        });
        // The colour they turn to as they age.
        m_particleRecolour = new QCheckBox(tr("Turn to"), m_particles);
        m_particleRecolour->setObjectName(QStringLiteral("ParticleRecolour"));
        m_particleRecolour->setToolTip(tr("On: each particle turns to the colour beside it by the end of its life."));
        m_particleColour = new QPushButton(m_particles);
        m_particleColour->setObjectName(QStringLiteral("ParticleEndColour"));
        m_particleColour->setFixedWidth(60);
        form->addRow(m_particleRecolour, m_particleColour);
        connect(m_particleRecolour, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_rebuilding)
                edit([on](Effect& e) { e.endColor = on ? (e.endColor.isValid() ? e.endColor : QColor(255, 140, 40)) : QColor(); });
        });
        connect(m_particleColour, &QPushButton::clicked, this, [this] {
            Scene const& scene = m_document->scene();
            int const row = m_list->currentRow();
            if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
                return;
            QColor const was = scene.layers[m_layer].effects[row].endColor;
            QColor const chosen = QColorDialog::getColor(was.isValid() ? was : QColor(255, 140, 40), this, tr("Particle Colour at the End"));
            if (chosen.isValid())
                edit([chosen](Effect& e) { e.endColor = chosen; });
        });
        m_particleFollow = new QCheckBox(tr("Move with the layer"), m_particles);
        m_particleFollow->setObjectName(QStringLiteral("ParticleFollow"));
        m_particleFollow->setToolTip(tr("On: the particles go where the layer goes. Off: each stays where the layer was when it was let go."));
        form->addRow(m_particleFollow);
        connect(m_particleFollow, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_rebuilding)
                edit([on](Effect& e) { e.followLayer = on; });
        });
        layout->addWidget(m_particles);
    }

    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (!m_rebuilding)
            showEditors();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_rebuilding)
            return;
        int const row = m_list->row(item);
        bool const on = item->checkState() == Qt::Checked;
        m_list->setCurrentRow(row);
        edit([on](Effect& e) { e.enabled = on; });
    });
    connect(m_amount, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_rebuilding)
            return;
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        Effect const& effect = scene.layers[m_layer].effects[row];
        bool const percent = effect.type == Effect::Tint || effect.type == Effect::Transparency || effect.type == Effect::HueSaturation
                             || effect.type == Effect::BrightnessContrast || effect.type == Effect::Grain || effect.type == Effect::RadialBlur
                             || effect.type == Effect::ColourScale || effect.type == Effect::Negate || effect.type == Effect::BlackWhite
                             || effect.type == Effect::Gradient;
        double const amount = percent ? v / 100.0 : v;
        if (effect.animated) {
            // On this frame only: a keyframe.
            m_document->setEffectAmount(m_layer, m_frame, row, amount);
            m_list->setCurrentRow(row);
        } else {
            edit([amount](Effect& e) { e.amount = amount; });
        }
    });
    connect(m_animated, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_rebuilding)
            edit([on](Effect& e) { e.animated = on; });
    });
    for (QDoubleSpinBox* box : {m_offsetX, m_offsetY}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            QPointF const offset(m_offsetX->value(), m_offsetY->value());
            if (!m_rebuilding)
                edit([offset](Effect& e) { e.offset = offset; });
        });
    }
    connect(m_color, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        QColor const chosen = QColorDialog::getColor(scene.layers[m_layer].effects[row].color, this, tr("Effect Colour"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.color = chosen; });
    });
    connect(m_endColor, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        QColor const chosen = QColorDialog::getColor(scene.layers[m_layer].effects[row].endColor, this, tr("Gradient: To"));
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.endColor = chosen; });
    });
    connect(m_matte, &QComboBox::activated, this, [this](int index) {
        int const id = m_matte->itemData(index).toInt();
        if (!m_rebuilding)
            edit([id](Effect& e) { e.matte = id; });
    });
    connect(m_invert, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_rebuilding)
            edit([on](Effect& e) { e.invert = on; });
    });
    connect(m_document, &Document::changed, this, &EffectsPanel::rebuild);
    rebuild();
}

void EffectsPanel::setCurrentCell(int layer, int frame)
{
    bool const layerChanged = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (layerChanged)
        rebuild();
    else
        showEditors();  // an animated amount differs from frame to frame
}

template <class Change> void EffectsPanel::edit(Change change)
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
        return;
    QVector<Effect> effects = scene.layers[m_layer].effects;
    change(effects[row]);
    m_document->setEffects(m_layer, effects, tr("Change Effect"));
    m_list->setCurrentRow(row);
}

void EffectsPanel::add(Effect::Type type)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Effect const e = Effect::made(type);
    QVector<Effect> effects = scene.layers[m_layer].effects;
    effects.push_back(e);
    m_document->setEffects(m_layer, effects, tr("Add Effect"));
    m_list->setCurrentRow(int(effects.size()) - 1);
}

void EffectsPanel::removeCurrent()
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
        return;
    m_document->removeEffect(m_layer, row);
}

void EffectsPanel::rebuild()
{
    Scene const& scene = m_document->scene();
    int const keep = m_list->currentRow();
    m_rebuilding = true;
    m_list->clear();
    bool const drawing = m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].holdsDrawings();
    setEnabled(drawing);
    if (drawing) {
        for (Effect const& e : scene.layers[m_layer].effects) {
            QListWidgetItem* const item = new QListWidgetItem(Effects::name(e, scene), m_list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        }
        m_list->setCurrentRow(std::min(keep, m_list->count() - 1));
    }
    m_rebuilding = false;
    showEditors();
}

void EffectsPanel::showEditors()
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    bool const valid = m_layer >= 0 && m_layer < scene.layers.size() && row >= 0 && row < scene.layers[m_layer].effects.size();
    Effect const e = valid ? scene.layers[m_layer].effects[row] : Effect();
    bool const cutter = valid && e.type == Effect::Cutter;
    bool const shading = valid && (e.type == Effect::Tone || e.type == Effect::Highlight || e.type == Effect::MatteBlur || e.type == Effect::VariableBlur);
    bool const resizing = valid && e.type == Effect::MatteResize;
    bool const coloured = valid && (e.type == Effect::Tint || e.type == Effect::Glow || e.type == Effect::Shadow || e.type == Effect::ColourScale || shading
                                    || e.type == Effect::FieldChart
                                    || e.type == Effect::MatteGenerator || e.type == Effect::Gradient) && e.type != Effect::MatteBlur;
    bool const graded = valid && e.type == Effect::Gradient;
    // What the amount is on this frame: the keyframes say, for an animated effect.
    double const amount = valid ? scene.layers[m_layer].effectAmount(row, scene.layers[m_layer].poseAt(m_frame)) : 0.0;
    m_rebuilding = true;
    bool const pegged = valid && e.type == Effect::ApplyPeg;
    bool const plain = valid && (e.type == Effect::ChannelSelector || e.type == Effect::Crop || e.type == Effect::MatteComposite || e.type == Effect::Blending
                                 || e.type == Effect::Glue
                                 || pegged);
    bool const curving = valid && e.type == Effect::ColourCurves;
    m_amountLabel->setVisible(valid && !cutter && !plain);
    m_amount->setVisible(valid && !cutter && !plain);
    m_animated->setVisible(valid && !cutter && !plain);
    {
        bool const channel = valid && e.type == Effect::ChannelSelector, cropping = valid && e.type == Effect::Crop;
        m_more->setVisible(channel || cropping || curving);
        m_moreForm->setRowVisible(m_curveWhich, curving);
        m_moreForm->setRowVisible(m_curve, curving);
        bool const texted = valid && (e.type == Effect::Script || e.type == Effect::External);
        m_more->setVisible(m_more->isVisibleTo(this) || texted);
        m_moreForm->setRowVisible(m_text, texted);
        m_moreForm->setRowVisible(m_textApply, texted);
        m_moreForm->setRowVisible(m_textSays, texted);
        if (texted && m_text->toPlainText() != e.text)
            m_text->setPlainText(e.text);
        if (texted)
            m_text->setToolTip(e.type == Effect::Script
                                   ? tr("JavaScript run on the picture each frame: frame, amount, and image - blur(r), tint(\"#rrggbb\", a), "
                                        "transparency(a), negate(a), blackWhite(a), hueSaturation(h, s, l), brightnessContrast(b, c), pixelate(s), "
                                        "median(r), bloom(r), grain(a), directionalBlur(length, degrees), colourScale(\"#rrggbb\", a), "
                                        "pixel(x, y), setPixel(x, y, \"#rrggbb\"), width, height.")
                                   : tr("A command line run on the picture each frame: it reads {in} and writes {out} (PNG files); {frame} is the frame."));
        m_curve->setPoints(e.curves[std::clamp(m_curveWhich->currentIndex(), 0, 3)]);
        m_moreForm->setRowVisible(m_channel, channel);
        m_channel->setCurrentIndex(std::clamp(e.channel, 0, 3));
        double const area[4] = {e.area.x(), e.area.y(), e.area.width(), e.area.height()};
        for (int i = 0; i < 4; ++i) {
            m_moreForm->setRowVisible(m_cropBox[i], cropping);
            m_cropBox[i]->setValue(area[i]);
        }
    }
    m_animated->setChecked(valid && e.animated);
    m_color->setVisible(coloured);
    m_endColor->setVisible(graded);
    if (graded)
        m_endColor->setStyleSheet(QStringLiteral("background-color: %1;").arg(e.endColor.name()));
    if (coloured)
        m_color->setStyleSheet(QStringLiteral("background-color: %1;").arg(e.color.name()));
    bool const shadow = valid && e.type == Effect::Shadow;
    m_offsetLabel->setVisible(shadow);
    m_offsetX->setVisible(shadow);
    m_offsetY->setVisible(shadow);
    if (shadow) {
        m_offsetX->setValue(e.offset.x());
        m_offsetY->setValue(e.offset.y());
    }
    bool const particles = valid && e.type == Effect::Particles;
    {
        bool const hs = valid && e.type == Effect::HueSaturation, bc = valid && e.type == Effect::BrightnessContrast;
        bool const swirls = valid && e.type == Effect::Turbulence, generator = valid && e.type == Effect::MatteGenerator;
        bool const waves = (valid && e.type == Effect::Ripple) || swirls;
        bool const focusing = valid && e.type == Effect::Focus;
        bool const along = (valid && (e.type == Effect::DirectionalBlur || e.type == Effect::Gradient)) || waves || generator || focusing;
        m_adjust->setVisible(hs || bc || along);
        // The last box: the way a Directional Blur smears, or how far apart a Ripple's waves are.
        if (QLabel* const label = qobject_cast<QLabel*>(m_adjustForm->labelForField(m_adjustBox[5])))
            label->setText(focusing ? tr("In Focus At") : generator ? tr("Inner Radius") : swirls ? tr("Swirl Size") : waves ? tr("Wave Length") : tr("Direction"));
        m_adjustBox[5]->setRange(focusing ? -10000.0 : generator ? 0.0 : waves ? 2.0 : -360.0, focusing ? 10000.0 : waves || generator ? 2000.0 : 360.0);
        m_adjustBox[5]->setSingleStep(focusing ? 10.0 : waves || generator ? 10.0 : 15.0);
        m_adjustBox[5]->setToolTip(focusing ? tr("The depth that is sharp (towards the camera, in scene units): the further a layer stands from it, the more blurred.")
                                   : generator ? tr("How far out from the drawing the matte stays solid, in scene units.")
                                   : waves   ? tr("How far apart the waves are, in scene units.")
                                             : tr("Which way the picture is smeared (degrees: 0 across, 90 up and down)."));
        bool const shownFor[6] = {hs, hs, hs, bc, bc, along};
        double const values[6] = {e.hue, e.saturation, e.lightness, e.brightness, e.contrast, e.direction};
        for (int i = 0; i < 6; ++i) {
            m_adjustForm->setRowVisible(m_adjustBox[i], shownFor[i]);
            m_adjustBox[i]->setValue(values[i]);
        }
    }
    m_matteLabel->setVisible(cutter || particles || shading || pegged);
    m_matteLabel->setText(particles ? tr("Bounce Off") : pegged ? tr("Peg") : tr("Matte"));
    m_matte->setVisible(cutter || particles || shading || pegged);
    m_matte->setToolTip(pegged ? tr("The layer (a peg, mostly) whose motion on each frame moves this layer's picture as well.")
                        : particles ? tr("A layer whose drawing the particles bounce off (keeping as much of their speed as Bounce says).")
                        : shading ? tr("The layer whose drawing says where the shade (or the light) falls on this one.")
                                  : tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert->setVisible(cutter || shading || resizing || graded);
    m_invert->setText(resizing ? tr("Shrink") : graded ? tr("Radial") : tr("Inverted"));
    m_invert->setToolTip(resizing ? tr("Make what is painted smaller instead of bigger.") : graded ? tr("From the middle out, instead of from side to side.")
                                  : tr("Keep what lies outside the matte instead."));
    if (cutter || shading || resizing || graded)
        m_invert->setChecked(e.invert);
    if (cutter || particles || shading || pegged) {
        m_matte->clear();
        m_matte->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            if (l != m_layer && (pegged || scene.layers[l].holdsDrawings()))
                m_matte->addItem(scene.layers[l].name, scene.layers[l].id);
        m_matte->setCurrentIndex(std::max(0, m_matte->findData(e.matte)));
    }
    m_particles->setVisible(particles);
    if (particles) {
        double const values[6] = {double(e.life), e.speed, e.spread, e.direction, e.gravity, double(e.seed)};
        for (int i = 0; i < 6; ++i)
            m_particleBox[i]->setValue(values[i]);
        m_particleBox[6]->setValue(e.endSize);
        m_particleFollow->setChecked(e.followLayer);
        m_particleBox[7]->setValue(e.wind);
        m_particleBox[8]->setValue(e.spin);
        m_particleBox[9]->setValue(e.regionWidth);
        m_particleBox[10]->setValue(e.regionHeight);
        m_particleBox[11]->setValue(e.floor);
        m_particleBox[12]->setValue(e.bounce);
        m_particleBox[13]->setValue(e.walls);
        m_particleShape->setCurrentIndex(std::clamp(e.regionShape, 0, 3));
        m_particleRecolour->setChecked(e.endColor.isValid());
        m_particleColour->setEnabled(e.endColor.isValid());
        m_particleColour->setStyleSheet(e.endColor.isValid() ? QStringLiteral("background-color: %1;").arg(e.endColor.name()) : QString());
        m_amountLabel->setText(tr("Each Frame"));
        m_amount->setRange(0.0, 50.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (valid && (e.type == Effect::Tint || e.type == Effect::Transparency || e.type == Effect::HueSaturation || e.type == Effect::BrightnessContrast
                         || e.type == Effect::Grain || e.type == Effect::RadialBlur || e.type == Effect::ColourScale || e.type == Effect::Negate
                         || e.type == Effect::BlackWhite || e.type == Effect::Gradient || e.type == Effect::RemoveTransparency)) {
        m_amountLabel->setText(e.type == Effect::Transparency ? tr("Transparency") : e.type == Effect::RemoveTransparency ? tr("Threshold") : tr("Amount"));
        m_amount->setRange(0.0, 100.0);
        m_amount->setDecimals(0);
        m_amount->setSuffix(QStringLiteral(" %"));
        m_amount->setValue(amount * 100.0);
    } else if (valid && !cutter && !plain) {
        m_amountLabel->setText(e.type == Effect::Ripple ? tr("Height") : e.type == Effect::MatteBlur ? tr("Radius") : shading ? tr("Softness")
                               : resizing ? tr("By") : e.type == Effect::Pixelate ? tr("Block Size")
                               : e.type == Effect::Turbulence ? tr("Strength") : e.type == Effect::MatteGenerator ? tr("Outer Radius")
                               : e.type == Effect::MotionBlur ? tr("Frames") : e.type == Effect::FieldChart ? tr("Line Width")
                               : e.type == Effect::Focus ? tr("Blur per 100") : e.type == Effect::ScaleOutput ? tr("Scale") : tr("Radius"));
        m_amount->setRange(0.0, 500.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (cutter) {
        m_invert->setChecked(e.invert);
    }
    m_rebuilding = false;
}

#include "Effects.moc"
