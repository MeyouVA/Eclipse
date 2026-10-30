// Eclipse - keying (taking away a colour, a background or the dark) and the generic composite.
#include "render/effects/Effects.h"

#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

// From 0 below `low` to 1 above `low + soft`, smoothly between (a step when there is no softness).
double ramp(double value, double low, double soft)
{
    if (soft <= 1e-9)
        return value >= low ? 1.0 : 0.0;
    double const t = std::clamp((value - low) / soft, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A matte's clip: what is below black gone wholly, what is above white kept wholly, the rest spread between.
double clipped(double matte, double black, double white)
{
    if (white <= black + 1e-6)
        return matte >= black ? 1.0 : 0.0;
    return std::clamp((matte - black) / (white - black), 0.0, 1.0);
}

struct Straight {
    double r = 0, g = 0, b = 0, a = 0;
};

Straight straight(QRgb p)
{
    Straight s;
    s.a = qAlpha(p) / 255.0;
    if (s.a > 0.0) {
        s.r = qRed(p) / 255.0 / s.a;
        s.g = qGreen(p) / 255.0 / s.a;
        s.b = qBlue(p) / 255.0 / s.a;
    }
    return s;
}

QRgb premultiplied(Straight const& s)
{
    double const a = std::clamp(s.a, 0.0, 1.0);
    auto channel = [a](double c) { return int(std::lround(std::clamp(c, 0.0, 1.0) * a * 255.0)); };
    return qRgba(channel(s.r), channel(s.g), channel(s.b), int(std::lround(a * 255.0)));
}

// Every pixel kept as much as the matte says.
template <typename Matte>
void keep(QImage& image, Matte const& matte)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            double const k = std::clamp(matte(x, y, line[x]), 0.0, 1.0);
            QRgb const p = line[x];
            line[x] = qRgba(int(std::lround(qRed(p) * k)), int(std::lround(qGreen(p) * k)), int(std::lround(qBlue(p) * k)),
                            int(std::lround(qAlpha(p) * k)));
        }
    }
}

}  // namespace

QStringList Effects::colourOperations()
{
    return {QObject::tr("Apply With Alpha"), QObject::tr("Replace"), QObject::tr("Add"), QObject::tr("Multiply"), QObject::tr("Screen"),
            QObject::tr("Darken"), QObject::tr("Lighten"), QObject::tr("Difference"), QObject::tr("Subtract"), QObject::tr("Divide"),
            QObject::tr("Colour")};
}

QStringList Effects::alphaOperations()
{
    return {QObject::tr("Apply"), QObject::tr("Replace"), QObject::tr("Add"), QObject::tr("Multiply"), QObject::tr("Maximum"), QObject::tr("Minimum")};
}

void Effects::genericComposite(QImage& under, QImage const& top, int colourOperation, int alphaOperation, double const (&intensity)[3], double opacity)
{
    if (under.format() != QImage::Format_ARGB32_Premultiplied)
        under = under.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage const over = top.format() == QImage::Format_ARGB32_Premultiplied ? top : top.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    double const amount = std::clamp(opacity, 0.0, 1.0);
    auto mix = [colourOperation](double s, double d) {
        switch (colourOperation) {
        case 2: return s + d;
        case 3: return s * d;
        case 4: return s + d - s * d;
        case 5: return std::min(s, d);
        case 6: return std::max(s, d);
        case 7: return std::abs(d - s);
        case 8: return d - s;
        case 9: return s <= 1e-6 ? 1.0 : d / s;
        default: return s;
        }
    };
    for (int y = 0; y < under.height() && y < over.height(); ++y) {
        QRgb* const to = reinterpret_cast<QRgb*>(under.scanLine(y));
        QRgb const* const from = reinterpret_cast<QRgb const*>(over.constScanLine(y));
        for (int x = 0; x < under.width() && x < over.width(); ++x) {
            Straight s = straight(from[x]);
            Straight const d = straight(to[x]);
            s.a *= amount;
            s.r *= intensity[0];
            s.g *= intensity[1];
            s.b *= intensity[2];
            if (s.a <= 0.0 && alphaOperation != 1 && alphaOperation != 3 && alphaOperation != 5)
                continue;
            // The colour: what the operation makes where both are, each alone where only it is.
            double const both = s.a * d.a;
            double const cover = s.a + d.a - both;
            Straight out;
            double mixed[3] = {mix(s.r, d.r), mix(s.g, d.g), mix(s.b, d.b)};
            if (colourOperation == 10) {
                // Colour: the hue and saturation of what comes in, the lightness of what is under it.
                auto lum = [](double r, double g, double b) { return 0.3 * r + 0.59 * g + 0.11 * b; };
                double const shift = lum(d.r, d.g, d.b) - lum(s.r, s.g, s.b);
                double c[3] = {s.r + shift, s.g + shift, s.b + shift};
                double const l = lum(c[0], c[1], c[2]), lo = std::min({c[0], c[1], c[2]}), hi = std::max({c[0], c[1], c[2]});
                for (double& v : c) {
                    if (lo < 0.0)
                        v = l + (v - l) * l / std::max(1e-9, l - lo);
                    if (hi > 1.0)
                        v = l + (v - l) * (1.0 - l) / std::max(1e-9, hi - l);
                }
                mixed[0] = c[0];
                mixed[1] = c[1];
                mixed[2] = c[2];
            }
            auto colour = [&](double sc, double dc, double m) {
                if (colourOperation == 1)
                    return s.a > 0.0 ? sc : dc;
                if (cover <= 1e-9)
                    return 0.0;
                return (sc * s.a * (1.0 - d.a) + dc * d.a * (1.0 - s.a) + m * both) / cover;
            };
            out.r = colour(s.r, d.r, mixed[0]);
            out.g = colour(s.g, d.g, mixed[1]);
            out.b = colour(s.b, d.b, mixed[2]);
            switch (alphaOperation) {
            case 1: out.a = s.a; break;
            case 2: out.a = std::min(1.0, s.a + d.a); break;
            case 3: out.a = s.a * d.a; break;
            case 4: out.a = std::max(s.a, d.a); break;
            case 5: out.a = std::min(s.a, d.a); break;
            default: out.a = cover; break;
            }
            to[x] = premultiplied(out);
        }
    }
}

void Effects::chromaKey(QImage& image, QColor const& key, double threshold, double softness, double clipBlack, double clipWhite, double spill, bool invert)
{
    // How far a colour is from the key, in its hue and richness alone (Cb and Cr), so shading of the backing still keys.
    auto chroma = [](double r, double g, double b) {
        return std::pair<double, double>(-0.169 * r - 0.331 * g + 0.5 * b, 0.5 * r - 0.419 * g - 0.081 * b);
    };
    auto const [kb, kr] = chroma(key.redF(), key.greenF(), key.blueF());
    double const scale = std::max(0.05, std::hypot(kb, kr));
    int const strongest = key.green() >= key.red() && key.green() >= key.blue() ? 1 : key.blue() >= key.red() ? 2 : 0;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            Straight s = straight(line[x]);
            auto const [cb, cr] = chroma(s.r, s.g, s.b);
            double const distance = std::hypot(cb - kb, cr - kr) / scale;
            double matte = clipped(ramp(distance, threshold, softness), clipBlack, clipWhite);
            if (invert)
                matte = 1.0 - matte;
            if (spill > 0.0 && !invert) {
                // The key colour that bled onto what stays taken out: its channel brought down towards the others.
                double* const c[3] = {&s.r, &s.g, &s.b};
                double const others = std::max(*c[(strongest + 1) % 3], *c[(strongest + 2) % 3]);
                if (*c[strongest] > others)
                    *c[strongest] -= (*c[strongest] - others) * spill;
            }
            s.a *= matte;
            line[x] = premultiplied(s);
        }
    }
}

void Effects::differenceKey(QImage& image, QImage const& clean, double threshold, double softness, double clipBlack, double clipWhite, bool invert)
{
    QImage const plate = clean.format() == QImage::Format_ARGB32_Premultiplied ? clean : clean.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    keep(image, [&](int x, int y, QRgb p) {
        QRgb const q = x < plate.width() && y < plate.height() ? reinterpret_cast<QRgb const*>(plate.constScanLine(y))[x] : 0u;
        double const difference = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q)),
                                             std::abs(qAlpha(p) - qAlpha(q))}) / 255.0;
        double const matte = clipped(ramp(difference, threshold, softness), clipBlack, clipWhite);
        return invert ? 1.0 - matte : matte;
    });
}

void Effects::luminanceThreshold(QImage& image, double threshold, double softness, double clipBlack, double clipWhite, bool invert)
{
    keep(image, [&](int, int, QRgb p) {
        Straight const s = straight(p);
        double const light = 0.299 * s.r + 0.587 * s.g + 0.114 * s.b;
        double const matte = clipped(ramp(light, threshold, softness), clipBlack, clipWhite);
        return invert ? 1.0 - matte : matte;
    });
}
