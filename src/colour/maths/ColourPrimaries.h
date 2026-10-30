// Eclipse - primaries and white points, and the matrices between colour spaces made from them.
#pragma once

#include "colour/maths/ColourOps.h"

namespace Cms {

// The chromaticities of red, green, blue and white (CIE 1931 x, y).
struct Primaries {
    double rx = 0, ry = 0, gx = 0, gy = 0, bx = 0, by = 0, wx = 0, wy = 0;
    bool valid() const { return wy > 0 && ry > 0 && gy > 0 && by != 0; }
    bool operator==(Primaries const&) const = default;
};

Primaries rec709Primaries();
Primaries p3d65Primaries();
Primaries p3dciPrimaries();
Primaries rec2020Primaries();
Primaries ap0Primaries();
Primaries ap1Primaries();
Primaries adobeRgbPrimaries();
Primaries proPhotoPrimaries();

constexpr double kD65x = 0.3127, kD65y = 0.3290;
constexpr double kD50x = 0.3457, kD50y = 0.3585;
constexpr double kAcesWx = 0.32168, kAcesWy = 0.33767;

Matrix3 multiply(Matrix3 const& a, Matrix3 const& b);
Matrix3 inverse(Matrix3 const& m, bool* ok = nullptr);
Matrix3 identity3();
// From linear RGB in these primaries to CIE XYZ (white at Y = 1).
Matrix3 rgbToXyz(Primaries const& p);
// Bradford chromatic adaptation from one white to another (in XYZ).
Matrix3 bradford(double fromX, double fromY, double toX, double toY);
// From linear RGB in one set of primaries to another, the white adapted by Bradford.
Matrix3 rgbToRgb(Primaries const& from, Primaries const& to);
// From linear RGB in these primaries to CIE XYZ with a D65 white, adapted.
Matrix3 rgbToXyzD65(Primaries const& p);

}  // namespace Cms
