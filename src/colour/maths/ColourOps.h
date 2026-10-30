// Eclipse - the steps a colour conversion is made of, and running them on a colour.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QVector>

#include <array>
#include <memory>

namespace Cms {

using Rgb = std::array<float, 3>;
using Matrix3 = std::array<double, 9>;

// A named transfer curve, from its coded values to light (decoding) when run forwards.
enum class Curve {
    Linear,
    Srgb,           // IEC 61966-2-1
    Bt709,          // ITU-R BT.709 / BT.2020 camera curve
    Gamma18,
    Gamma22,
    Gamma24,        // BT.1886 with a black of 0
    Gamma26,
    Pq,             // SMPTE ST 2084; light of 1.0 is 100 nits
    Hlg,            // ITU-R BT.2100 HLG, scene light
    AcesCc,
    AcesCct,
    ProPhoto,       // ROMM RGB
    LStar,          // CIE L*
    AppleLog,
    CanonLog2,      // light scaled by 0.9, as the camera makers' own transforms
    CanonLog3,
};
QString curveName(Curve curve);
// Light from a coded value, and back.
double decode(Curve curve, double value);
double encode(Curve curve, double value);

// One step.  Forwards, a curve step decodes (coded value to light) and a log step encodes (light to log).
struct Op {
    enum Kind {
        Matrix,         // m: 3x3 by rows, then the offset
        Exponent,       // gamma per channel; style: 0 clamp below 0, 1 mirrored, 2 passed through below 0
        MonCurve,       // power with a straight piece near black (gamma, offset per channel)
        Log,            // base
        LogAffine,      // base, log slope/offset, lin slope/offset per channel
        LogCamera,      // LogAffine with a straight piece below a break
        Named,          // a named Curve
        Lut1D,          // tables per channel over inLow..inHigh
        Lut3D,          // a cube of size n over inLow..inHigh
        Range,          // inLow..inHigh to outLow..outHigh, clamped
        Cdl,            // slope, offset, power, saturation
        Fixed,          // a fixed function (style)
        Exposure,       // exposure, contrast, gamma, pivot
        Allocation,     // lg2 or uniform: inLow..inHigh to 0..1
    };
    Kind kind = Matrix;
    bool inverse = false;
    std::array<double, 12> m{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    std::array<double, 3> a{}, b{}, c{}, d{}, e{}, f{};
    double base = 10.0;
    int style = 0;
    Curve curve = Curve::Linear;
    std::array<double, 3> inLow{0, 0, 0}, inHigh{1, 1, 1};
    double outLow = 0.0, outHigh = 1.0;
    bool clampLow = true, clampHigh = true;
    int size = 0;                                       // a table's length, a cube's edge
    std::shared_ptr<QVector<float> const> table;        // Lut1D: size values per channel, one channel after another; Lut3D: rgb, red fastest
    bool tetrahedral = true;

    bool perChannel() const;
    // The same step run the other way; false when it cannot be.
    bool inverted(Op* out, QString* error) const;
};

enum class FixedStyle {
    RgbToHsv,
    XyzToXyy,
    XyzToUvy,
    XyzToLuv,
    Rec2100Surround,    // gamma in `a[0]`
    AcesGlow,           // a: gain, middle
    AcesRedMod,         // a: scale, pivot, width; b[0] 1 keeps the hue (the 0.3 version)
    AcesDarkToDim,      // a[0]: gamma
    AcesGamutComp,      // a: limits (cyan, magenta, yellow), b: thresholds, c[0]: power
    AcesToneScale,      // b[0]: 0 the RRT's, 1 the 48-nit ODT's, 2 the single-stage one for luminances a (lowest, middle, highest)
    AcesRollWhite,      // a: new white, width
    AdxDensity,         // ADX density to log exposure
    AcesOutputTwo,      // the ACES 2 output transform: a peak luminance, then the limiting primaries and white (a[1..2], b, c)
};

// The ACES fixed functions (FixedStyle::AcesGlow and on); false when the style is not one of them.
bool runAces(Op const& op, float* rgb, bool back);
void runAcesTwo(Op const& op, float* rgb, bool back);
// The ACES 2 tables of a display at an entry (hue + 1): the reach of AP1, the cusp's J and M.
std::array<double, 3> acesTwoTables(Op const& op, int entry);

using Chain = QVector<Op>;

// Runs the steps on a colour, in place.
void run(Chain const& chain, float* rgb);
void runOp(Op const& op, float* rgb);
// The chain run backwards; false (with the reason) when a step cannot be undone.
bool invert(Chain const& chain, Chain* out, QString* error);
// Next steps joined where they can be (matrices multiplied) and steps that do nothing left out.
Chain simplified(Chain chain);
bool isIdentity(Chain const& chain);

// Steps that make things.
Op matrixOp(Matrix3 const& m, std::array<double, 3> const& offset = {0, 0, 0});
Op curveOp(Curve curve, bool encodeIt = false);
Op gammaOp(double gamma, bool encodeIt = false);
Op monCurveOp(double gamma, double offset, bool encodeIt = false);
Op rangeOp(double inLow, double inHigh, double outLow, double outHigh, bool clampIt = true);

}  // namespace Cms
