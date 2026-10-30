// Eclipse - matrices between colour spaces made from their primaries.
#include "colour/maths/ColourPrimaries.h"

#include <cmath>

namespace Cms {

Primaries rec709Primaries() { return {0.64, 0.33, 0.30, 0.60, 0.15, 0.06, kD65x, kD65y}; }
Primaries p3d65Primaries() { return {0.680, 0.320, 0.265, 0.690, 0.150, 0.060, kD65x, kD65y}; }
Primaries p3dciPrimaries() { return {0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.314, 0.351}; }
Primaries rec2020Primaries() { return {0.708, 0.292, 0.170, 0.797, 0.131, 0.046, kD65x, kD65y}; }
Primaries ap0Primaries() { return {0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.0770, kAcesWx, kAcesWy}; }
Primaries ap1Primaries() { return {0.713, 0.293, 0.165, 0.830, 0.128, 0.044, kAcesWx, kAcesWy}; }
Primaries adobeRgbPrimaries() { return {0.64, 0.33, 0.21, 0.71, 0.15, 0.06, kD65x, kD65y}; }
Primaries proPhotoPrimaries() { return {0.734699, 0.265301, 0.159597, 0.840403, 0.036598, 0.000105, kD50x, kD50y}; }

Matrix3 identity3()
{
    return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

Matrix3 multiply(Matrix3 const& a, Matrix3 const& b)
{
    Matrix3 out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k)
                sum += a[size_t(r * 3 + k)] * b[size_t(k * 3 + c)];
            out[size_t(r * 3 + c)] = sum;
        }
    return out;
}

Matrix3 inverse(Matrix3 const& m, bool* ok)
{
    double const det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (ok)
        *ok = std::abs(det) > 1e-14;
    if (std::abs(det) <= 1e-14)
        return identity3();
    double const k = 1.0 / det;
    return {(m[4] * m[8] - m[5] * m[7]) * k, (m[2] * m[7] - m[1] * m[8]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
            (m[5] * m[6] - m[3] * m[8]) * k, (m[0] * m[8] - m[2] * m[6]) * k, (m[2] * m[3] - m[0] * m[5]) * k,
            (m[3] * m[7] - m[4] * m[6]) * k, (m[1] * m[6] - m[0] * m[7]) * k, (m[0] * m[4] - m[1] * m[3]) * k};
}

Matrix3 rgbToXyz(Primaries const& p)
{
    auto column = [](double x, double y) { return std::array<double, 3>{x / y, 1.0, (1.0 - x - y) / y}; };
    auto const r = column(p.rx, p.ry), g = column(p.gx, p.gy), b = column(p.bx, p.by), w = column(p.wx, p.wy);
    Matrix3 const m{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    // How much of each primary makes the white.
    Matrix3 const inv = inverse(m);
    double const s[3] = {inv[0] * w[0] + inv[1] * w[1] + inv[2] * w[2], inv[3] * w[0] + inv[4] * w[1] + inv[5] * w[2],
                         inv[6] * w[0] + inv[7] * w[1] + inv[8] * w[2]};
    Matrix3 out = m;
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c)
            out[size_t(row * 3 + c)] *= s[c];
    return out;
}

Matrix3 bradford(double fromX, double fromY, double toX, double toY)
{
    if (std::abs(fromX - toX) < 1e-12 && std::abs(fromY - toY) < 1e-12)
        return identity3();
    Matrix3 const cone{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    auto white = [](double x, double y) { return std::array<double, 3>{x / y, 1.0, (1.0 - x - y) / y}; };
    auto toCone = [&cone](std::array<double, 3> const& v) {
        return std::array<double, 3>{cone[0] * v[0] + cone[1] * v[1] + cone[2] * v[2], cone[3] * v[0] + cone[4] * v[1] + cone[5] * v[2],
                                     cone[6] * v[0] + cone[7] * v[1] + cone[8] * v[2]};
    };
    auto const a = toCone(white(fromX, fromY)), b = toCone(white(toX, toY));
    Matrix3 const scale{b[0] / a[0], 0, 0, 0, b[1] / a[1], 0, 0, 0, b[2] / a[2]};
    return multiply(inverse(cone), multiply(scale, cone));
}

Matrix3 rgbToXyzD65(Primaries const& p)
{
    return multiply(bradford(p.wx, p.wy, kD65x, kD65y), rgbToXyz(p));
}

Matrix3 rgbToRgb(Primaries const& from, Primaries const& to)
{
    Matrix3 const there = multiply(bradford(from.wx, from.wy, to.wx, to.wy), rgbToXyz(from));
    return multiply(inverse(rgbToXyz(to)), there);
}

}  // namespace Cms
