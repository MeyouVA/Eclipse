// Eclipse - the newer colour effects: fade, channel swap, levels, screen, contrast, gamma, banding, dither, and the older ones' added settings.
#include "render/effects/Effects.h"
#include "render/effects/EffectsPrivate.h"

#include <algorithm>
#include <cmath>

namespace Effects {

using namespace detail;

void colourFade(QImage& image, QColor const& target, double amount, int space, int hueWay)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    double const tr = target.redF(), tg = target.greenF(), tb = target.blueF();
    float th = 0, ts = 0, tv = 0;
    if (space == 1)
        target.getHsvF(&th, &ts, &tv);
    else if (space == 2)
        target.getHslF(&th, &ts, &tv);
    perPixel(image, [&](double& r, double& g, double& b, double&, int, int) {
        if (space == 0) {
            r += (tr - r) * amount;
            g += (tg - g) * amount;
            b += (tb - b) * amount;
            return;
        }
        QColor const was = QColor::fromRgbF(float(r), float(g), float(b));
        float h = 0, s = 0, v = 0;
        if (space == 1)
            was.getHsvF(&h, &s, &v);
        else
            was.getHslF(&h, &s, &v);
        double from = h < 0 ? (th < 0 ? 0.0 : th) : h, to = th < 0 ? from : th;
        double d = to - from;
        if (hueWay == 1)
            d = d > 0.5 ? d - 1.0 : d < -0.5 ? d + 1.0 : d;
        else if (hueWay == 2)
            d = d < 0 ? d + 1.0 : d;
        else if (hueWay == 3)
            d = d > 0 ? d - 1.0 : d;
        double const nh = std::fmod(from + d * amount + 2.0, 1.0);
        double const ns = s + (ts - s) * amount, nv = v + (tv - v) * amount;
        QColor const now = space == 1 ? QColor::fromHsvF(float(nh), float(std::clamp(ns, 0.0, 1.0)), float(std::clamp(nv, 0.0, 1.0)))
                                      : QColor::fromHslF(float(nh), float(std::clamp(ns, 0.0, 1.0)), float(std::clamp(nv, 0.0, 1.0)));
        r = now.redF();
        g = now.greenF();
        b = now.blueF();
    });
}

void channelSwap(QImage& image, int const (&from)[4])
{
    if (from[0] == 0 && from[1] == 1 && from[2] == 2 && from[3] == 3)
        return;
    bool const clear = from[3] >= 4 || from[3] < 3;
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        double const was[6] = {r, g, b, a, 0.0, 1.0};
        r = was[std::clamp(from[0], 0, 5)];
        g = was[std::clamp(from[1], 0, 5)];
        b = was[std::clamp(from[2], 0, 5)];
        a = was[std::clamp(from[3], 0, 5)];
    }, clear);
}

void levels(QImage& image, int channel, double inBlack, double inWhite, double gamma, double outBlack, double outWhite, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    double const span = std::max(1e-6, inWhite - inBlack), power = 1.0 / std::max(0.01, gamma);
    auto map = [&](double v) {
        double const now = outBlack + std::pow(std::clamp((v - inBlack) / span, 0.0, 1.0), power) * (outWhite - outBlack);
        return v + (now - v) * amount;
    };
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        if (channel == 0 || channel == 1)
            r = map(r);
        if (channel == 0 || channel == 2)
            g = map(g);
        if (channel == 0 || channel == 3)
            b = map(b);
        if (channel == 4)
            a = map(a);
    });
}

void colourScreen(QImage& image, double const (&range)[6], bool outside)
{
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        bool const in = r >= range[0] && r <= range[1] && g >= range[2] && g <= range[3] && b >= range[4] && b <= range[5];
        if (in == outside)
            a = 0.0;
    });
}

void contrast(QImage& image, double midPoint, double dark, double bright, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (dark == 1.0 && bright == 1.0))
        return;
    auto map = [&](double v) {
        double const now = v < midPoint ? midPoint - (midPoint - v) * dark : midPoint + (v - midPoint) * bright;
        return v + (now - v) * amount;
    };
    perPixel(image, [&](double& r, double& g, double& b, double&, int, int) {
        r = map(r);
        g = map(g);
        b = map(b);
    });
}

void gamma(QImage& image, double rgb, double red, double green, double blue, double alpha, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    auto map = [&](double v, double by) {
        double const now = std::pow(std::clamp(v, 0.0, 1.0), 1.0 / std::max(0.01, by));
        return v + (now - v) * amount;
    };
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        r = map(r, rgb * red);
        g = map(g, rgb * green);
        b = map(b, rgb * blue);
        a = map(a, alpha);
    });
}

void colourBanding(QImage& image, double const (&thresholds)[4], QColor const (&colours)[4])
{
    perPixel(image, [&](double& r, double& g, double& b, double&, int, int) {
        double const l = luminance(r, g, b);
        int band = -1;
        for (int k = 0; k < 4; ++k)
            if (l >= thresholds[k])
                band = k;
        if (band < 0)
            return;
        r = colours[band].redF();
        g = colours[band].greenF();
        b = colours[band].blueF();
    });
}

void dither(QImage& image, double magnitude, quint32 seed)
{
    if (magnitude <= 0.0)
        return;
    double const most = magnitude / 255.0;
    perPixel(image, [&](double& r, double& g, double& b, double&, int x, int y) {
        r += (random01(quint32(x), quint32(y), seed) - 0.5) * 2.0 * most;
        g += (random01(quint32(x), quint32(y), seed + 1) - 0.5) * 2.0 * most;
        b += (random01(quint32(x), quint32(y), seed + 2) - 0.5) * 2.0 * most;
    });
}

void negate(QImage& image, double amount, bool colour, bool alpha, bool clamp)
{
    if (colour && !alpha && clamp) {
        negate(image, amount);
        return;
    }
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (!colour && !alpha))
        return;
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        if (colour) {
            r += (1.0 - 2.0 * r) * amount;
            g += (1.0 - 2.0 * g) * amount;
            b += (1.0 - 2.0 * b) * amount;
        }
        if (alpha)
            a += (1.0 - 2.0 * a) * amount;
        else if (!clamp)
            a = 1.0;
    }, !clamp || alpha);
}

void greyMatte(QImage& image)
{
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        a *= luminance(r, g, b);
        r = g = b = 1.0;
    });
}

void colourScaleMore(QImage& image, double alpha, double hue, double saturation, double value)
{
    if (alpha == 1.0 && hue == 0.0 && saturation == 1.0 && value == 1.0)
        return;
    bool const hsv = hue != 0.0 || saturation != 1.0 || value != 1.0;
    perPixel(image, [&](double& r, double& g, double& b, double& a, int, int) {
        a *= alpha;
        if (!hsv)
            return;
        float h = 0, s = 0, v = 0;
        QColor::fromRgbF(float(r), float(g), float(b)).getHsvF(&h, &s, &v);
        double const nh = std::fmod((h < 0 ? 0.0 : h) + hue / 360.0 + 2.0, 1.0);
        QColor const now = QColor::fromHsvF(float(nh), float(std::clamp(s * saturation, 0.0, 1.0)), float(std::clamp(v * value, 0.0, 1.0)));
        r = now.redF();
        g = now.greenF();
        b = now.blueF();
    });
}

void colorize(QImage& image, double hue, double saturation, double lightness, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    double const h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0) / 360.0;
    double const s = std::clamp((saturation + 100.0) / 200.0, 0.0, 1.0), lift = std::clamp(lightness, -100.0, 100.0) / 100.0;
    perPixel(image, [&](double& r, double& g, double& b, double&, int, int) {
        float hh = 0, ss = 0, l = 0;
        QColor::fromRgbF(float(r), float(g), float(b)).getHslF(&hh, &ss, &l);
        double const nl = std::clamp(lift >= 0.0 ? l + (1.0 - l) * lift : l * (1.0 + lift), 0.0, 1.0);
        QColor const now = QColor::fromHslF(float(h), float(s), float(nl));
        r += (now.redF() - r) * amount;
        g += (now.greenF() - g) * amount;
        b += (now.blueF() - b) * amount;
    });
}

void increaseOpacity(QImage& image, double factor)
{
    if (factor <= 0.0 || factor == 1.0)
        return;
    premultiplied(image);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // (Premultiplied: the colours rise with the opacity, so they stay what they were.)
            double const k = std::min(factor, 255.0 / a);
            auto one = [k](int v) { return std::clamp(int(std::lround(v * k)), 0, 255); };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), one(a));
        }
    }
}

void grain(QImage& image, double amount, quint32 seed, double smoothness)
{
    if (smoothness <= 0.0) {
        grain(image, amount, seed);
        return;
    }
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    double const cell = 1.0 + std::clamp(smoothness, 0.0, 1.0) * 8.0, spread = amount * 160.0 / 255.0;
    perPixel(image, [&](double& r, double& g, double& b, double&, int x, int y) {
        double const shift = (smooth(x / cell, y / cell, seed) - 0.5) * 2.0 * spread;
        r += shift;
        g += shift;
        b += shift;
    });
}

}  // namespace Effects
