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
};

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
