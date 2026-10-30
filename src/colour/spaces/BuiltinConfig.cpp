// Eclipse - the colour spaces Eclipse knows without a configuration file, and the built-in transforms of OpenColorIO configurations.
#include "colour/spaces/ColourConfig.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace {

using namespace Cms;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

// A space of primaries and a curve, against the scene reference (ACES2065-1).
Space made(QString const& name, char const* description, char const* encoding, Primaries const& p, Curve curve, QStringList categories = {QStringLiteral("file-io")})
{
    Space s;
    s.name = name;
    s.description = said(description);
    s.encoding = QString::fromLatin1(encoding);
    s.categories = categories;
    s.primaries = p;
    s.curve = curve;
    s.curveKnown = true;
    s.hasFrom = true;
    s.fromReference << matrixOp(rgbToRgb(ap0Primaries(), p));
    if (curve != Curve::Linear)
        s.fromReference << curveOp(curve, true);
    s.fromReference = simplified(s.fromReference);
    return s;
}

Chain toXyzD65(Primaries const& p)
{
    return {matrixOp(rgbToXyzD65(p))};
}

// From CIE XYZ (D65) to the RGB of a display's primaries, its white adapted from D65 by Bradford.
Op fromXyzD65(Primaries const& p)
{
    return matrixOp(multiply(inverse(rgbToXyz(p)), bradford(kD65x, kD65y, p.wx, p.wy)));
}

Primaries awg3()
{
    return {0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020, kD65x, kD65y};
}

Primaries sGamut3()
{
    return {0.730, 0.280, 0.140, 0.855, 0.100, -0.050, kD65x, kD65y};
}

Primaries sGamut3Cine()
{
    return {0.766, 0.275, 0.225, 0.800, 0.089, -0.087, kD65x, kD65y};
}

// A camera log curve as OpenColorIO's LogCamera (log side slope and offset, lin side slope and offset, the break), log to light.
Op cameraLogDecode(double logSlope, double logOffset, double linSlope, double linOffset, double breakAt, double linearSlope = 0.0)
{
    Op op;
    op.kind = Op::LogCamera;
    op.base = 10.0;
    op.a = {logSlope, logSlope, logSlope};
    op.b = {logOffset, logOffset, logOffset};
    op.c = {linSlope, linSlope, linSlope};
    op.d = {linOffset, linOffset, linOffset};
    op.e = {breakAt, breakAt, breakAt};
    op.f = {linearSlope, linearSlope, linearSlope};
    op.inverse = true;
    return op;
}


Op fixedOp(FixedStyle style, std::array<double, 3> a = {}, std::array<double, 3> b = {}, double c0 = 0.0)
{
    Op op;
    op.kind = Op::Fixed;
    op.style = int(style);
    op.a = a;
    op.b = b;
    op.c = {c0, 0, 0};
    return op;
}

// A clamp below `low` only (style 1) or above `high` only (style 2).
Op clampBelow(double low)
{
    Op op = rangeOp(low, low + 1.0, low, low + 1.0);
    op.style = 1;
    return op;
}

Op clampAbove(double high)
{
    Op op = rangeOp(high - 1.0, high, high - 1.0, high);
    op.style = 2;
    return op;
}

Op scaleOp(double k, double offset = 0.0)
{
    return matrixOp({k, 0, 0, 0, k, 0, 0, 0, k}, {offset, offset, offset});
}

// The saturation matrix of the ACES transforms, about the luminance of AP1.
Op saturationOp(double sat)
{
    Matrix3 const toXyz = rgbToXyz(ap1Primaries());
    double const y[3] = {toXyz[3], toXyz[4], toXyz[5]};
    Matrix3 m{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[size_t(r * 3 + c)] = (1.0 - sat) * y[c] + (r == c ? sat : 0.0);
    return matrixOp(m);
}

Op rows(std::initializer_list<double> values)
{
    Matrix3 m{};
    std::copy(values.begin(), values.end(), m.begin());
    return matrixOp(m);
}

Op logOp(double base, bool inverseIt)
{
    Op op;
    op.kind = Op::Log;
    op.base = base;
    op.inverse = inverseIt;
    return op;
}

Op logCameraOp(double base, double logSlope, double logOffset, double linSlope, double linOffset, double breakAt, double linearSlope = 0.0)
{
    Op op = cameraLogDecode(logSlope, logOffset, linSlope, linOffset, breakAt, linearSlope);
    op.base = base;
    return op;
}

// The start shared by the ACES 1 output transforms: the RRT's glow, red modifier and desaturation, into AP1.
Chain acesPreamble()
{
    return {fixedOp(FixedStyle::AcesGlow, {0.05, 0.08, 0}), fixedOp(FixedStyle::AcesRedMod, {0.82, 0.03, 135.0}), clampBelow(0.0),
            matrixOp(rgbToRgb(ap0Primaries(), ap1Primaries())), clampBelow(0.0), saturationOp(0.96)};
}

// The ACES 2 output transforms: the peak luminance and limiting primaries (with white) of each, then the scales after the clamp.
bool acesTwoOutput(QString const& kind, Chain* out)
{
    struct Two {
        char const* name;
        double p[9];
        double scale1, scale2;
    };
    static Two const twos[] = {
        {"SDR-100nit-REC709_2.0", {100, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"SDR-100nit-P3-D65_2.0", {100, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"HDR-108nit-P3-D65_2.0", {225, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 0.4799999892711639, 1},
        {"HDR-300nit-P3-D65_2.0", {625, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 0.4799999892711639, 1},
        {"HDR-500nit-P3-D65_2.0", {500, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"HDR-1000nit-P3-D65_2.0", {1000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"HDR-2000nit-P3-D65_2.0", {2000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"HDR-4000nit-P3-D65_2.0", {4000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}, 1, 1},
        {"HDR-500nit-REC2020_2.0", {500, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329}, 1, 1},
        {"HDR-1000nit-REC2020_2.0", {1000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329}, 1, 1},
        {"HDR-2000nit-REC2020_2.0", {2000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329}, 1, 1},
        {"HDR-4000nit-REC2020_2.0", {4000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329}, 1, 1},
        {"SDR-100nit-REC709-D60-in-REC709-D65_2.0", {100, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06, 0.32168, 0.33767}, 0.9550158655187944, 1},
        {"SDR-100nit-REC709-D60-in-P3-D65_2.0", {100, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766538, 1},
        {"SDR-100nit-REC709-D60-in-REC2020-D65_2.0", {100, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06, 0.32168, 0.33767}, 0.976478635041989, 1},
        {"SDR-100nit-P3-D60-in-P3-D65_2.0", {100, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 1},
        {"SDR-100nit-P3-D60-in-XYZ-E_2.0", {100, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 1, 1},
        {"HDR-108nit-P3-D60-in-P3-D65_2.0", {225, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 0.4799999892711639},
        {"HDR-300nit-P3-D60-in-XYZ-E_2.0", {625, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9912520182004995, 0.4799999892711639},
        {"HDR-500nit-P3-D60-in-P3-D65_2.0", {500, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 1},
        {"HDR-1000nit-P3-D60-in-P3-D65_2.0", {1000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 1},
        {"HDR-2000nit-P3-D60-in-P3-D65_2.0", {2000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 1},
        {"HDR-4000nit-P3-D60-in-P3-D65_2.0", {4000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9636031240766543, 1},
        {"HDR-500nit-P3-D60-in-REC2020-D65_2.0", {500, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9764786350419896, 1},
        {"HDR-1000nit-P3-D60-in-REC2020-D65_2.0", {1000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9764786350419896, 1},
        {"HDR-2000nit-P3-D60-in-REC2020-D65_2.0", {2000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9764786350419896, 1},
        {"HDR-4000nit-P3-D60-in-REC2020-D65_2.0", {4000, 0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.32168, 0.33767}, 0.9764786350419896, 1},
        {"HDR-500nit-REC2020-D60-in-REC2020-D65_2.0", {500, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.32168, 0.33767}, 0.9764786350419894, 1},
        {"HDR-1000nit-REC2020-D60-in-REC2020-D65_2.0", {1000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.32168, 0.33767}, 0.9764786350419894, 1},
        {"HDR-2000nit-REC2020-D60-in-REC2020-D65_2.0", {2000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.32168, 0.33767}, 0.9764786350419894, 1},
        {"HDR-4000nit-REC2020-D60-in-REC2020-D65_2.0", {4000, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.32168, 0.33767}, 0.9764786350419894, 1},
    };
    for (Two const& t : twos) {
        if (kind.compare(QLatin1String(t.name), Qt::CaseInsensitive) != 0)
            continue;
        double const peak = t.p[0];
        // (The scene values are first held under the point where the tone scale "hits the roof", eight times over.)
        double const rHit = 128.0 + (896.0 - 128.0) * (std::log(peak / 100.0) / std::log(100.0));
        Op two = fixedOp(FixedStyle::AcesOutputTwo, {t.p[0], t.p[1], t.p[2]}, {t.p[3], t.p[4], t.p[5]});
        two.c = {t.p[6], t.p[7], t.p[8]};
        Primaries const lim{t.p[1], t.p[2], t.p[3], t.p[4], t.p[5], t.p[6], t.p[7], t.p[8]};
        *out << matrixOp(rgbToRgb(ap0Primaries(), ap1Primaries())) << rangeOp(0.0, 8.0 * rHit, 0.0, 8.0 * rHit) << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()))
             << two << rangeOp(0.0, peak / 100.0, 0.0, peak / 100.0);
        if (t.scale1 != 1.0)
            *out << scaleOp(t.scale1);
        if (t.scale2 != 1.0)
            *out << scaleOp(t.scale2);
        *out << matrixOp(rgbToXyz(lim));
        return true;
    }
    return false;
}

// The ACES 1 output transforms, from ACES2065-1 to CIE XYZ (D65) with 1.0 = 100 nits; false when the name is not one.
bool acesOutput(QString const& name, Chain* out)
{
    QString const prefix = QStringLiteral("ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - ");
    if (!name.startsWith(prefix, Qt::CaseInsensitive))
        return false;
    QString const kind = name.mid(prefix.size()).toUpper();
    if (acesTwoOutput(kind, out))
        return true;
    Primaries const p3d65 = p3d65Primaries(), rec709 = rec709Primaries(), rec2020 = rec2020Primaries(), ap1 = ap1Primaries();
    Primaries const dci = p3dciPrimaries();
    Matrix3 const dciToD65 = bradford(dci.wx, dci.wy, kD65x, kD65y);
    // SDR: the RRT and 48-nit ODT tone scales, then 0.02..48 nits to 0..1.
    Chain sdr = acesPreamble();
    sdr << fixedOp(FixedStyle::AcesToneScale, {}, {0, 0, 0}) << fixedOp(FixedStyle::AcesToneScale, {}, {1, 0, 0}) << scaleOp(1.0 / (48.0 - 0.02), -0.02 / (48.0 - 0.02));
    Chain video;
    video << fixedOp(FixedStyle::AcesDarkToDim, {0.9811, 0, 0}) << saturationOp(0.93);
    auto limited = [](Primaries const& p) {
        return Chain{matrixOp(rgbToRgb(ap1Primaries(), p)), rangeOp(0.0, 1.0, 0.0, 1.0), matrixOp(rgbToXyz(p))};
    };
    if (kind == QLatin1String("SDR-CINEMA_1.0")) {
        *out = sdr << matrixOp(rgbToXyzD65(ap1));
    } else if (kind == QLatin1String("SDR-VIDEO_1.0")) {
        *out = sdr << video << matrixOp(rgbToXyzD65(ap1));
    } else if (kind == QLatin1String("SDR-CINEMA-REC709LIM_1.1")) {
        *out = sdr << limited(rec709);
    } else if (kind == QLatin1String("SDR-VIDEO-REC709LIM_1.1")) {
        *out = sdr << video << limited(rec709);
    } else if (kind == QLatin1String("SDR-VIDEO-P3LIM_1.1")) {
        *out = sdr << video << limited(p3d65);
    } else if (kind == QLatin1String("SDR-CINEMA-D60SIM-D65_1.1")) {
        *out = sdr << clampAbove(1.0) << scaleOp(0.964) << matrixOp(rgbToXyz(ap1));
    } else if (kind == QLatin1String("SDR-VIDEO-D60SIM-D65_1.0")) {
        *out = sdr << clampAbove(1.0) << scaleOp(0.955) << video << matrixOp(rgbToXyz(ap1));
    } else if (kind == QLatin1String("SDR-CINEMA-D60SIM-DCI_1.0")) {
        *out = sdr << fixedOp(FixedStyle::AcesRollWhite, {0.918, 0.5, 0}) << clampAbove(0.918) << scaleOp(0.96) << matrixOp(rgbToXyz(ap1)) << matrixOp(dciToD65);
    } else if (kind == QLatin1String("SDR-CINEMA-D65SIM-DCI_1.1")) {
        *out = sdr << fixedOp(FixedStyle::AcesRollWhite, {0.908, 0.5, 0}) << clampAbove(0.908) << scaleOp(0.9575) << matrixOp(rgbToXyzD65(ap1)) << matrixOp(dciToD65);
    } else {
        // HDR: the single-stage tone scale for the display luminances, limited to the display primaries (no adaptation), then the white to D65.
        struct Hdr {
            char const* name;
            double low, middle, high;
            bool p3;
        };
        Hdr const hdrs[] = {{"HDR-VIDEO-1000NIT-15NIT-REC2020LIM_1.1", 0.0001, 15.0, 1000.0, false}, {"HDR-VIDEO-1000NIT-15NIT-P3LIM_1.1", 0.0001, 15.0, 1000.0, true},
                            {"HDR-VIDEO-2000NIT-15NIT-REC2020LIM_1.1", 0.0001, 15.0, 2000.0, false}, {"HDR-VIDEO-2000NIT-15NIT-P3LIM_1.1", 0.0001, 15.0, 2000.0, true},
                            {"HDR-VIDEO-4000NIT-15NIT-REC2020LIM_1.1", 0.0001, 15.0, 4000.0, false}, {"HDR-VIDEO-4000NIT-15NIT-P3LIM_1.1", 0.0001, 15.0, 4000.0, true},
                            {"HDR-CINEMA-108NIT-7.2NIT-P3LIM_1.1", 0.0001, 7.2, 108.0, true}};
        for (Hdr const& h : hdrs)
            if (kind == QLatin1String(h.name)) {
                Primaries const lim = h.p3 ? p3d65 : rec2020;
                Chain c = acesPreamble();
                c << fixedOp(FixedStyle::AcesToneScale, {h.low, h.middle, h.high}, {2, 0, 0}) << scaleOp(1.0 / (h.high - h.low), -h.low / (h.high - h.low))
                  << matrixOp(multiply(inverse(rgbToXyz(lim)), rgbToXyz(ap1))) << rangeOp(0.0, 1.0, 0.0, 1.0) << matrixOp(rgbToXyz(lim))
                  << matrixOp(bradford(kAcesWx, kAcesWy, kD65x, kD65y)) << scaleOp(h.high / 100.0);
                *out = c;
                return true;
            }
        return false;
    }
    return true;
}

}  // namespace

namespace Cms {

Config builtinConfig()
{
    Config c;
    c.title = QStringLiteral("eclipse-builtin");
    c.description = said("Eclipse's own colour spaces; the scene reference is ACES2065-1.");
    c.version = 2.1;
    Space aces;
    aces.name = QStringLiteral("ACES2065-1");
    aces.description = said("ACES2065-1 (Linear, AP0 primaries): the Academy Color Encoding System reference colour space");
    aces.encoding = QStringLiteral("scene-linear");
    aces.categories = {QStringLiteral("file-io")};
    aces.primaries = ap0Primaries();
    aces.curveKnown = true;
    c.spaces << aces;
    c.spaces << made(QStringLiteral("ACEScg"), "ACEScg (Linear, AP1 primaries): for rendering and compositing", "scene-linear", ap1Primaries(), Curve::Linear,
                     {QStringLiteral("file-io"), QStringLiteral("working-space")});
    c.spaces << made(QStringLiteral("ACEScct"), "ACEScct (log, AP1 primaries): for grading", "log", ap1Primaries(), Curve::AcesCct,
                     {QStringLiteral("file-io"), QStringLiteral("working-space")});
    c.spaces << made(QStringLiteral("ACEScc"), "ACEScc (log, AP1 primaries)", "log", ap1Primaries(), Curve::AcesCc);
    Space lin709 = made(QStringLiteral("Linear Rec.709 (sRGB)"), "Linear Rec.709 (sRGB): the primaries of sRGB, linear light", "scene-linear", rec709Primaries(),
                        Curve::Linear, {QStringLiteral("file-io"), QStringLiteral("working-space")});
    lin709.aliases = QStringList{QStringLiteral("Linear"), QStringLiteral("lin_srgb")};
    c.spaces << lin709;
    Space lin2020 = made(QStringLiteral("Linear Rec.2020"), "Rec. ITU-R BT.2020 primaries, linear light", "scene-linear", rec2020Primaries(), Curve::Linear);
    lin2020.aliases = QStringList{QStringLiteral("Rec.2020 Linear")};
    c.spaces << lin2020;
    Space linP3 = made(QStringLiteral("Linear P3-D65"), "P3 primaries with a D65 white, linear light", "scene-linear", p3d65Primaries(), Curve::Linear,
                       {QStringLiteral("file-io"), QStringLiteral("working-space")});
    linP3.aliases = QStringList{QStringLiteral("Display P3 Linear")};
    c.spaces << linP3;
    c.spaces << made(QStringLiteral("Linear Adobe RGB (1998)"), "Adobe RGB (1998) primaries, linear light", "scene-linear", adobeRgbPrimaries(), Curve::Linear);
    Space srgb = made(QStringLiteral("sRGB"), "sRGB IEC61966-2.1: most screens and picture files", "sdr-video", rec709Primaries(), Curve::Srgb,
                      {QStringLiteral("file-io"), QStringLiteral("working-space")});
    srgb.aliases = QStringList{QStringLiteral("srgb_display")};
    c.spaces << srgb;
    c.spaces << made(QStringLiteral("Display P3"), "Display P3: P3-D65 primaries with the sRGB curve", "sdr-video", p3d65Primaries(), Curve::Srgb);
    c.spaces << made(QStringLiteral("Rec.709"), "Rec. ITU-R BT.709 (the camera curve)", "sdr-video", rec709Primaries(), Curve::Bt709);
    c.spaces << made(QStringLiteral("Rec.709 2.4"), "Rec.709 with Gamma 2.4 (ITU-R BT.1886)", "sdr-video", rec709Primaries(), Curve::Gamma24);
    c.spaces << made(QStringLiteral("Gamma 2.2 Rec.709"), "Rec.709 primaries with a pure gamma of 2.2", "sdr-video", rec709Primaries(), Curve::Gamma22);
    c.spaces << made(QStringLiteral("Rec.2020"), "Rec. ITU-R BT.2020 (the camera curve)", "sdr-video", rec2020Primaries(), Curve::Bt709);
    c.spaces << made(QStringLiteral("Rec.2020 2.4"), "Rec.2020 with Gamma 2.4 (ITU-R BT.1886)", "sdr-video", rec2020Primaries(), Curve::Gamma24);
    c.spaces << made(QStringLiteral("Adobe RGB (1998)"), "Adobe RGB (1998)", "sdr-video", adobeRgbPrimaries(), Curve::Gamma22);
    c.spaces << made(QStringLiteral("DCI-P3"), "DCI-P3: digital cinema, gamma 2.6, the DCI white", "sdr-video", p3dciPrimaries(), Curve::Gamma26);
    c.spaces << made(QStringLiteral("P3-D65"), "P3 primaries with a D65 white and gamma 2.6", "sdr-video", p3d65Primaries(), Curve::Gamma26);
    c.spaces << made(QStringLiteral("ProPhoto RGB"), "ProPhoto RGB (ROMM RGB)", "sdr-video", proPhotoPrimaries(), Curve::ProPhoto);
    c.spaces << made(QStringLiteral("Rec.2100 PQ"), "Rec.2100 with the PQ curve (ST 2084); 1.0 is 100 nits", "hdr-video", rec2020Primaries(), Curve::Pq);
    c.spaces << made(QStringLiteral("P3-D65 PQ"), "P3-D65 primaries with the PQ curve (ST 2084); 1.0 is 100 nits", "hdr-video", p3d65Primaries(), Curve::Pq);
    c.spaces << made(QStringLiteral("Rec.2100 HLG"), "Rec.2100 with the HLG curve (scene light)", "hdr-video", rec2020Primaries(), Curve::Hlg);
    Space xyz;
    xyz.name = QStringLiteral("CIE-XYZ-D65");
    xyz.aliases = QStringList{QStringLiteral("cie_xyz_d65")};
    xyz.description = said("CIE XYZ with a D65 white");
    xyz.encoding = QStringLiteral("scene-linear");
    xyz.hasFrom = true;
    xyz.fromReference = toXyzD65(ap0Primaries());
    c.spaces << xyz;
    Space raw;
    raw.name = QStringLiteral("Raw");
    raw.family = QStringLiteral("Utility");
    raw.description = said("Numbers, not colours: never converted");
    raw.isData = true;
    raw.encoding = QStringLiteral("data");
    c.spaces << raw;
    c.roles = {{QStringLiteral("aces_interchange"), QStringLiteral("ACES2065-1")},
               {QStringLiteral("cie_xyz_d65_interchange"), QStringLiteral("CIE-XYZ-D65")},
               {QStringLiteral("color_picking"), QStringLiteral("sRGB")},
               {QStringLiteral("color_timing"), QStringLiteral("ACEScct")},
               {QStringLiteral("compositing_log"), QStringLiteral("ACEScct")},
               {QStringLiteral("data"), QStringLiteral("Raw")},
               {QStringLiteral("default"), QStringLiteral("sRGB")},
               {QStringLiteral("matte_paint"), QStringLiteral("sRGB")},
               {QStringLiteral("scene_linear"), QStringLiteral("Linear Rec.709 (sRGB)")},
               {QStringLiteral("texture_paint"), QStringLiteral("sRGB")}};
    c.inactiveSpaces = QStringList{QStringLiteral("CIE-XYZ-D65"), QStringLiteral("Raw")};
    // Display-referred spaces (from CIE XYZ, 1.0 = 100 nits) for the tone-mapped views; not offered as colour spaces of their own.
    struct DisplayEncoding {
        char const* name;
        char const* style;
        char const* encoding;
        Primaries p;
        Curve curve;
    };
    DisplayEncoding const encodings[] = {
        {"sRGB - Display", "DISPLAY - CIE-XYZ-D65_to_sRGB", "sdr-video", rec709Primaries(), Curve::Srgb},
        {"Display P3 - Display", "DISPLAY - CIE-XYZ-D65_to_DisplayP3", "sdr-video", p3d65Primaries(), Curve::Srgb},
        {"Rec.1886 Rec.709 - Display", "DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.709", "sdr-video", rec709Primaries(), Curve::Gamma24},
        {"Rec.2100-PQ - Display", "DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ", "hdr-video", rec2020Primaries(), Curve::Pq},
        {"ST2084-P3-D65 - Display", "DISPLAY - CIE-XYZ-D65_to_ST2084-P3-D65", "hdr-video", p3d65Primaries(), Curve::Pq},
    };
    for (DisplayEncoding const& e : encodings) {
        Space d;
        d.name = QString::fromLatin1(e.name);
        d.family = QStringLiteral("Display");
        d.description = said("A display's own encoding, used by the tone-mapped views");
        d.encoding = QString::fromLatin1(e.encoding);
        d.displayReferred = true;
        d.hasFrom = builtinTransform(QString::fromLatin1(e.style), &d.fromReference, nullptr);
        d.primaries = e.p;
        d.curve = e.curve;
        d.curveKnown = true;
        c.spaces << d;
        c.inactiveSpaces << d.name;
    }
    // The ACES output transforms as view transforms: scene light tone mapped for a display.
    struct Tone {
        char const* name;
        char const* style;
    };
    Tone const tones[] = {
        {"Un-tone-mapped", "UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD"},
        {"ACES 1.0 - SDR Video", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - SDR-VIDEO_1.0"},
        {"ACES 1.1 - SDR Video (P3 lim)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - SDR-VIDEO-P3lim_1.1"},
        {"ACES 1.1 - HDR Video (1000 nits & Rec.2020 lim)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - HDR-VIDEO-1000nit-15nit-REC2020lim_1.1"},
        {"ACES 1.1 - HDR Video (1000 nits & P3 lim)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - HDR-VIDEO-1000nit-15nit-P3lim_1.1"},
        {"ACES 2.0 - SDR 100 nits (Rec.709)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - SDR-100nit-REC709_2.0"},
        {"ACES 2.0 - SDR 100 nits (P3-D65)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - SDR-100nit-P3-D65_2.0"},
        {"ACES 2.0 - HDR 1000 nits (Rec.2020)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - HDR-1000nit-REC2020_2.0"},
        {"ACES 2.0 - HDR 1000 nits (P3-D65)", "ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - HDR-1000nit-P3-D65_2.0"},
    };
    for (Tone const& t : tones) {
        ViewTransform v;
        v.name = QString::fromLatin1(t.name);
        v.family = QStringLiteral("ACES");
        v.description = said("Scene light shown on a display through the ACES output transform");
        v.hasFrom = builtinTransform(QString::fromLatin1(t.style), &v.fromSceneReference, &v.problem);
        c.viewTransforms << v;
    }
    c.defaultViewTransform = QStringLiteral("Un-tone-mapped");
    // Each display: its plain view, the tone-mapped views that suit it, then Raw.
    struct Toned {
        char const* display;
        char const* encoding;
        QList<char const*> views;
    };
    QList<Toned> const toned = {
        {"sRGB", "sRGB - Display", {"ACES 1.0 - SDR Video", "ACES 2.0 - SDR 100 nits (Rec.709)"}},
        {"Display P3", "Display P3 - Display", {"ACES 1.1 - SDR Video (P3 lim)", "ACES 2.0 - SDR 100 nits (P3-D65)"}},
        {"Rec.709 2.4", "Rec.1886 Rec.709 - Display", {"ACES 1.0 - SDR Video", "ACES 2.0 - SDR 100 nits (Rec.709)"}},
        {"Rec.2100 PQ", "Rec.2100-PQ - Display", {"ACES 1.1 - HDR Video (1000 nits & Rec.2020 lim)", "ACES 2.0 - HDR 1000 nits (Rec.2020)"}},
        {"P3-D65 PQ", "ST2084-P3-D65 - Display", {"ACES 1.1 - HDR Video (1000 nits & P3 lim)", "ACES 2.0 - HDR 1000 nits (P3-D65)"}},
    };
    for (char const* name : {"sRGB", "Display P3", "Rec.709", "Rec.709 2.4", "Rec.2020", "Rec.2020 2.4", "Adobe RGB (1998)", "DCI-P3", "P3-D65", "Rec.2100 PQ", "P3-D65 PQ", "Rec.2100 HLG"}) {
        Display d;
        d.name = QString::fromLatin1(name);
        d.views << View{d.name, d.name, {}, {}, {}, {}};
        for (Toned const& t : toned)
            if (qstrcmp(t.display, name) == 0)
                for (char const* view : t.views)
                    d.views << View{QString::fromLatin1(view), {}, QString::fromLatin1(view), QString::fromLatin1(t.encoding), {}, {}};
        d.views << View{QStringLiteral("Raw"), QStringLiteral("Raw"), {}, {}, {}, {}};
        c.displays << d;
    }
    c.fileRules << FileRule{QStringLiteral("Default"), QStringLiteral("sRGB"), {}, {}, {}};
    return c;
}

bool builtinTransform(QString const& style, Chain* out, QString* error)
{
    out->clear();
    QString const s = style.trimmed();
    auto is = [&s](char const* name) { return s.compare(QLatin1String(name), Qt::CaseInsensitive) == 0; };
    if (is("IDENTITY"))
        return true;
    if (is("ACEScg_to_ACES2065-1")) {
        *out << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("ACEScct_to_ACES2065-1")) {
        *out << curveOp(Curve::AcesCct) << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("ACEScc_to_ACES2065-1")) {
        *out << rangeOp(-0.36, 1.5, -0.36, 1.5) << curveOp(Curve::AcesCc) << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries())) << clampBelow(0.0);
        return true;
    }
    if (is("UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD")) {
        *out = toXyzD65(ap0Primaries());
        return true;
    }
    if (is("UTILITY - ACES-AP1_to_CIE-XYZ-D65_BFD")) {
        *out = toXyzD65(ap1Primaries());
        return true;
    }
    if (is("UTILITY - ACES-AP1_to_LINEAR-REC709_BFD")) {
        *out << matrixOp(rgbToRgb(ap1Primaries(), rec709Primaries()));
        return true;
    }
    if (is("CURVE - ACEScct-LOG_to_LINEAR")) {
        *out << curveOp(Curve::AcesCct);
        return true;
    }
    if (is("CURVE - LINEAR_to_ACEScct-LOG")) {
        *out << curveOp(Curve::AcesCct, true);
        return true;
    }
    if (is("CURVE - ST-2084_to_LINEAR")) {
        *out << curveOp(Curve::Pq);
        return true;
    }
    if (is("CURVE - LINEAR_to_ST-2084")) {
        *out << curveOp(Curve::Pq, true);
        return true;
    }
    // (As OpenColorIO: the gamma displays clamp below black, sRGB carries its straight piece on below it, the others are mirrored.)
    struct DisplayStyle {
        char const* name;
        Primaries p;
        Curve curve;
    };
    Primaries d60 = p3dciPrimaries();
    d60.wx = kAcesWx;
    d60.wy = kAcesWy;
    DisplayStyle const displays[] = {
        {"DISPLAY - CIE-XYZ-D65_to_sRGB", rec709Primaries(), Curve::Srgb},
        {"DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.709", rec709Primaries(), Curve::Gamma24},
        {"DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.2020", rec2020Primaries(), Curve::Gamma24},
        {"DISPLAY - CIE-XYZ-D65_to_G2.2-REC.709", rec709Primaries(), Curve::Gamma22},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-DCI-BFD", p3dciPrimaries(), Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-D65", p3d65Primaries(), Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-D60-BFD", d60, Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_DisplayP3", p3d65Primaries(), Curve::Srgb},
        {"DISPLAY - CIE-XYZ-D65_to_ST2084-P3-D65", p3d65Primaries(), Curve::Pq},
        {"DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ", rec2020Primaries(), Curve::Pq},
    };
    for (DisplayStyle const& d : displays)
        if (is(d.name)) {
            *out << fromXyzD65(d.p);
            double const gamma = d.curve == Curve::Gamma22 ? 2.2 : d.curve == Curve::Gamma24 ? 2.4 : d.curve == Curve::Gamma26 ? 2.6 : 0.0;
            if (gamma > 0.0) {
                Op power = gammaOp(gamma, true);
                power.style = 0;
                *out << power;
            } else if (is("DISPLAY - CIE-XYZ-D65_to_sRGB")) {
                Op curve = monCurveOp(2.4, 0.055, true);
                curve.style = 0;
                *out << curve;
            } else {
                *out << curveOp(d.curve, true);
            }
            return true;
        }
    if (is("DISPLAY - CIE-XYZ-D65_to_REC.2100-HLG-1000nit")) {
        // Display light (1.0 = 100 nits) to a 1000-nit HLG signal: the system gamma of 1.2 taken back off, then the HLG curve.
        Op surround;
        surround.kind = Op::Fixed;
        surround.style = int(FixedStyle::Rec2100Surround);
        surround.a = {1.0 / 1.2, 0, 0};
        // (Scaled as OpenColorIO does, to three times scene light, so the darkest luminance is held at the same place.)
        Matrix3 m = inverse(rgbToXyz(rec2020Primaries()));
        double const k = 100.0 / (1000.0 * std::pow(3.0, -1.2));
        for (double& v : m)
            v *= k;
        *out << matrixOp(m) << surround << scaleOp(1.0 / 3.0) << curveOp(Curve::Hlg, true);
        return true;
    }
    if (is("ARRI_ALEXA-LOGC-EI800-AWG_to_ACES2065-1")) {
        *out << cameraLogDecode(0.2471896383, 0.3855369987, 5.555555555555555, 0.052272275, 0.0105909905)
             << rows({0.6802055051062792, 0.2361366016064812, 0.08365789328723973, 0.08541497974214032, 1.0174708786070419, -0.10288585834918218,
                      0.002056521669296831, -0.06256250038479215, 1.0605059787154951});
        return true;
    }
    if (is("SONY_SLOG3-SGAMUT3_to_ACES2065-1") || is("SONY_SLOG3-SGAMUT3.CINE_to_ACES2065-1")) {
        Op const decodeLog = cameraLogDecode(261.5 / 1023.0, 420.0 / 1023.0, 1.0 / 0.19, 0.01 / 0.19, 0.01125, (171.2102946929 - 95.0) / 0.01125 / 1023.0);
        *out << decodeLog;
        if (is("SONY_SLOG3-SGAMUT3_to_ACES2065-1"))
            *out << rows({0.7529825953998397, 0.14337021623555743, 0.10364718836460277, 0.021707697441442908, 1.015318835505277, -0.03702653294671948,
                          -0.009416052749633549, 0.003370417858823673, 1.0060456348908096});
        else
            *out << rows({0.6387886671859788, 0.2723514337112621, 0.08885989910275935, -0.0039159060252824205, 1.0880732308974002, -0.08415732487211768,
                          -0.02990720212391506, -0.026432579910194733, 1.0563397820341096});
        return true;
    }
    if (acesOutput(s, out))
        return true;
    if (is("ACES-LMT - BLUE_LIGHT_ARTIFACT_FIX")) {
        *out << rows({0.9404372683, -0.0183068787, 0.0778696104, 0.0083786969, 0.8286599939, 0.1629613092, 0.0005471261, -0.0008833746, 1.0003362486});
        return true;
    }
    if (is("ACES-LMT - ACES 1.3 Reference Gamut Compression")) {
        *out << matrixOp(rgbToRgb(ap0Primaries(), ap1Primaries())) << fixedOp(FixedStyle::AcesGamutComp, {1.147, 1.264, 1.312}, {0.815, 0.803, 0.880}, 1.2)
             << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("ACESproxy10i_to_ACES2065-1")) {
        *out << rangeOp(64.0 / 1023.0, 940.0 / 1023.0, -9.72, 7.8) << logOp(2.0, true) << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("ADX10_to_ACES2065-1") || is("ADX16_to_ACES2065-1")) {
        double const k = is("ADX10_to_ACES2065-1") ? 1023.0 / 500.0 : 65535.0 / 8000.0;
        *out << scaleOp(k, -95.0 / 500.0) << rows({0.75573, 0.22197, 0.02230, 0.05901, 0.96928, -0.02829, 0.16134, 0.07406, 0.76460})
             << fixedOp(FixedStyle::AdxDensity) << logOp(10.0, true) << rows({0.72286, 0.12630, 0.15084, 0.11923, 0.76418, 0.11659, 0.01427, 0.08213, 0.90359});
        return true;
    }
    Op const appleGamut = rows({0.6790856347069116, 0.15770091464315905, 0.16321345064992895, 0.04600200308005939, 0.8590546730029082, 0.09494332391703267,
                                -0.0005739431876161902, 0.028467768408026412, 0.9721061747795897});
    Op const canonGamut = rows({0.7630644547757339, 0.1490211611370605, 0.08791438408720548, 0.0036574567051239296, 1.1069603803762151, -0.11061783708133885,
                                -0.009407794045718898, -0.21838330498998706, 1.2277910990357057});
    Op const redGamut = rows({0.7850588040680928, 0.08385875654408452, 0.13108243938782282, 0.023173834845475434, 1.0878975491923313, -0.11107138403780661,
                              -0.07376043536820821, -0.31459007229020775, 1.3883505076584162});
    if (is("APPLE_LOG_to_ACES2065-1") || is("CURVE - APPLE_LOG_to_LINEAR")) {
        *out << curveOp(Curve::AppleLog);
        if (is("APPLE_LOG_to_ACES2065-1"))
            *out << appleGamut;
        return true;
    }
    if (is("CANON_CLOG2-CGAMUT_to_ACES2065-1") || is("CURVE - CANON_CLOG2_to_LINEAR") || is("CANON_CLOG3-CGAMUT_to_ACES2065-1") || is("CURVE - CANON_CLOG3_to_LINEAR")) {
        *out << curveOp(s.contains(QLatin1String("CLOG2"), Qt::CaseInsensitive) ? Curve::CanonLog2 : Curve::CanonLog3);
        if (s.contains(QLatin1String("CGAMUT"), Qt::CaseInsensitive))
            *out << canonGamut;
        return true;
    }
    if (is("ARRI_LOGC4_to_ACES2065-1")) {
        *out << logCameraOp(2.0, 0.0647954196341293, -0.295908392682586, 2231.82630906769, 64.0, -0.0180569961199113)
             << rows({0.750957362824734, 0.14442278670975686, 0.10461985046550892, 0.0008218370793800299, 1.0073975848850039, -0.008219421964383633,
                      -0.0004999521435334737, -0.0008541772314369779, 1.0013541293749704});
        return true;
    }
    if (is("PANASONIC_VLOG-VGAMUT_to_ACES2065-1")) {
        *out << logCameraOp(10.0, 0.241514, 0.598206, 1.0, 0.00873, 0.01)
             << rows({0.7246167041315297, 0.16691528819370582, 0.10846800767476404, 0.021390245413145742, 0.9849081557030553, -0.006298401116201002,
                      -0.009235562870765617, -0.0010569056390051055, 1.0102924685097707});
        return true;
    }
    if (is("RED_REDLOGFILM-RWG_to_ACES2065-1")) {
        Op op;
        op.kind = Op::LogAffine;
        op.base = 10.0;
        op.a = {300.0 / 1023.0, 300.0 / 1023.0, 300.0 / 1023.0};
        op.b = {685.0 / 1023.0, 685.0 / 1023.0, 685.0 / 1023.0};
        op.c = {0.989202248376723, 0.989202248376723, 0.989202248376723};
        op.d = {0.010797751623277032, 0.010797751623277032, 0.010797751623277032};
        op.inverse = true;
        *out << op << redGamut;
        return true;
    }
    if (is("RED_LOG3G10-RWG_to_ACES2065-1")) {
        *out << logCameraOp(10.0, 0.224282, 0.0, 155.975327, 0.01 * 155.975327 + 1.0, -0.01) << redGamut;
        return true;
    }
    if (is("SONY_SLOG3-SGAMUT3-VENICE_to_ACES2065-1") || is("SONY_SLOG3-SGAMUT3.CINE-VENICE_to_ACES2065-1")) {
        *out << cameraLogDecode(261.5 / 1023.0, 420.0 / 1023.0, 1.0 / 0.19, 0.01 / 0.19, 0.01125, (171.2102946929 - 95.0) / 0.01125 / 1023.0);
        if (is("SONY_SLOG3-SGAMUT3-VENICE_to_ACES2065-1"))
            *out << rows({0.7933297411, 0.0890786256, 0.1175916333, 0.0155810585, 1.0327123069, -0.0482933654, -0.0188647478, 0.0127694121, 1.0060953358});
        else
            *out << rows({0.6742570921, 0.2205717359, 0.105171172, -0.0093136061, 1.1059588614, -0.0966452553, -0.0382090673, -0.0179383766, 1.0561474439});
        return true;
    }
    if (is("DISPLAY - CIE-XYZ-D65_to_DCDM-D65")) {
        Op power = gammaOp(2.6, true);
        power.style = 0;
        *out << scaleOp(48.0 / 52.37) << power;
        return true;
    }
    if (is("DISPLAY - CIE-XYZ-D65_to_ST2084-DCDM-D65")) {
        *out << curveOp(Curve::Pq, true);
        return true;
    }
    if (is("DISPLAY - CIE-XYZ-D65_to_DisplayP3-HDR")) {
        *out << fromXyzD65(p3d65Primaries()) << curveOp(Curve::Srgb, true);
        return true;
    }
    if (is("CURVE - HLG-OETF")) {
        *out << scaleOp(1.0 / 3.0) << curveOp(Curve::Hlg, true);
        return true;
    }
    if (is("CURVE - HLG-OETF-INVERSE")) {
        *out << curveOp(Curve::Hlg) << scaleOp(3.0);
        return true;
    }
    if (error)
        *error = said("Eclipse cannot do the built-in transform '%1'.").arg(style);
    return false;
}

}  // namespace Cms
