// Eclipse - the named transfer curves: coded values to light and back.
#include "colour/maths/ColourOps.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace {

// (Every curve is mirrored below 0, so colours outside the range come back as they went.)
template <typename F>
double mirrored(double value, F f)
{
    return value < 0.0 ? -f(-value) : f(value);
}

constexpr double kPqM1 = 2610.0 / 16384.0;
constexpr double kPqM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kPqC1 = 3424.0 / 4096.0;
constexpr double kPqC2 = 2413.0 / 4096.0 * 32.0;
constexpr double kPqC3 = 2392.0 / 4096.0 * 32.0;
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;
constexpr double kHlgC = 0.55991073;

}  // namespace

namespace Cms {

QString curveName(Curve curve)
{
    switch (curve) {
    case Curve::Linear: return QCoreApplication::translate("Colour", "Linear");
    case Curve::Srgb: return QStringLiteral("sRGB");
    case Curve::Bt709: return QStringLiteral("Rec.709");
    case Curve::Gamma18: return QCoreApplication::translate("Colour", "Gamma 1.8");
    case Curve::Gamma22: return QCoreApplication::translate("Colour", "Gamma 2.2");
    case Curve::Gamma24: return QCoreApplication::translate("Colour", "Gamma 2.4");
    case Curve::Gamma26: return QCoreApplication::translate("Colour", "Gamma 2.6");
    case Curve::Pq: return QStringLiteral("PQ (ST 2084)");
    case Curve::Hlg: return QStringLiteral("HLG");
    case Curve::AcesCc: return QStringLiteral("ACEScc");
    case Curve::AcesCct: return QStringLiteral("ACEScct");
    case Curve::ProPhoto: return QStringLiteral("ProPhoto");
    case Curve::LStar: return QStringLiteral("L*");
    }
    return {};
}

double decode(Curve curve, double v)
{
    switch (curve) {
    case Curve::Linear:
        return v;
    case Curve::Srgb:
        return mirrored(v, [](double x) { return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); });
    case Curve::Bt709:
        return mirrored(v, [](double x) { return x < 0.081 ? x / 4.5 : std::pow((x + 0.099) / 1.099, 1.0 / 0.45); });
    case Curve::Gamma18:
        return mirrored(v, [](double x) { return std::pow(x, 1.8); });
    case Curve::Gamma22:
        return mirrored(v, [](double x) { return std::pow(x, 2.2); });
    case Curve::Gamma24:
        return mirrored(v, [](double x) { return std::pow(x, 2.4); });
    case Curve::Gamma26:
        return mirrored(v, [](double x) { return std::pow(x, 2.6); });
    case Curve::Pq:
        return mirrored(v, [](double x) {
            double const p = std::pow(x, 1.0 / kPqM2);
            return std::pow(std::max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1) * 100.0;
        });
    case Curve::Hlg:
        return mirrored(v, [](double x) { return x <= 0.5 ? x * x / 3.0 : (std::exp((x - kHlgC) / kHlgA) + kHlgB) / 12.0; });
    case Curve::AcesCc:
        if (v < (9.72 - 15.0) / 17.52)
            return (std::pow(2.0, v * 17.52 - 9.72) - std::pow(2.0, -16.0)) * 2.0;
        return std::min(std::pow(2.0, v * 17.52 - 9.72), 65504.0);
    case Curve::AcesCct:
        if (v <= 0.155251141552511)
            return (v - 0.0729055341958355) / 10.5402377416545;
        return std::min(std::pow(2.0, v * 17.52 - 9.72), 65504.0);
    case Curve::ProPhoto:
        return mirrored(v, [](double x) { return x < 16.0 / 512.0 ? x / 16.0 : std::pow(x, 1.8); });
    case Curve::LStar:
        return mirrored(v, [](double x) { return x > 0.08 ? std::pow((x + 0.16) / 1.16, 3.0) : x * 2700.0 / 24389.0; });
    }
    return v;
}

double encode(Curve curve, double v)
{
    switch (curve) {
    case Curve::Linear:
        return v;
    case Curve::Srgb:
        return mirrored(v, [](double x) { return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055; });
    case Curve::Bt709:
        return mirrored(v, [](double x) { return x < 0.018 ? x * 4.5 : 1.099 * std::pow(x, 0.45) - 0.099; });
    case Curve::Gamma18:
        return mirrored(v, [](double x) { return std::pow(x, 1.0 / 1.8); });
    case Curve::Gamma22:
        return mirrored(v, [](double x) { return std::pow(x, 1.0 / 2.2); });
    case Curve::Gamma24:
        return mirrored(v, [](double x) { return std::pow(x, 1.0 / 2.4); });
    case Curve::Gamma26:
        return mirrored(v, [](double x) { return std::pow(x, 1.0 / 2.6); });
    case Curve::Pq:
        return mirrored(v, [](double x) {
            double const y = std::pow(std::min(x / 100.0, 1.0), kPqM1);
            return std::pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2);
        });
    case Curve::Hlg:
        return mirrored(v, [](double x) { return x <= 1.0 / 12.0 ? std::sqrt(3.0 * x) : kHlgA * std::log(12.0 * x - kHlgB) + kHlgC; });
    case Curve::AcesCc:
        if (v <= 0.0)
            return (-16.0 + 9.72) / 17.52;
        if (v < std::pow(2.0, -15.0))
            return (std::log2(std::pow(2.0, -16.0) + v * 0.5) + 9.72) / 17.52;
        return (std::log2(v) + 9.72) / 17.52;
    case Curve::AcesCct:
        if (v <= 0.0078125)
            return 10.5402377416545 * v + 0.0729055341958355;
        return (std::log2(v) + 9.72) / 17.52;
    case Curve::ProPhoto:
        return mirrored(v, [](double x) { return x < 1.0 / 512.0 ? x * 16.0 : std::pow(x, 1.0 / 1.8); });
    case Curve::LStar:
        return mirrored(v, [](double x) { return x > 216.0 / 24389.0 ? 1.16 * std::cbrt(x) - 0.16 : x * 24389.0 / 2700.0; });
    }
    return v;
}

}  // namespace Cms
