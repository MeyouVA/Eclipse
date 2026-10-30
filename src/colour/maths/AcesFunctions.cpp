// Eclipse - the pieces of the ACES 1 output transforms (glow, red modifier, tone scales, surround, gamut compression), forwards and back.
#include "colour/maths/ColourOps.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using namespace Cms;

constexpr double kTiny = 1e-10;
constexpr double kHalfMin = 5.96046448e-08;
constexpr double kPi = 3.14159265358979323846;
// The luminance of ACES AP1 (the rendering primaries).
constexpr double kAp1Y[3] = {0.27222871678091454, 0.67408176581114831, 0.053689517407937051};

// The quadratic B-spline basis the ACES tone scales are made of.
constexpr double kSpline[3][3] = {{0.5, -1.0, 0.5}, {-1.0, 1.0, 0.5}, {0.5, 0.0, 0.0}};

double splineAt(double const* coefs, int j, double t)
{
    double out = 0.0;
    double const mono[3] = {t * t, t, 1.0};
    for (int i = 0; i < 3; ++i) {
        double s = 0.0;
        for (int k = 0; k < 3; ++k)
            s += coefs[j + k] * kSpline[k][i];
        out += mono[i] * s;
    }
    return out;
}

// A segmented spline: a straight log-log line below min and above max, the low and high B-splines between.
struct Spline {
    std::array<double, 10> low{}, high{};
    int knotsLow = 4, knotsHigh = 4;
    double minX = 0, minY = 0, midX = 0, midY = 0, maxX = 0, maxY = 0;
    double slopeLow = 0, slopeHigh = 0;

    double at(double x) const
    {
        double const lx = std::log10(std::max(x, kHalfMin));
        double const l0 = std::log10(minX), l1 = std::log10(midX), l2 = std::log10(maxX);
        double ly = 0.0;
        if (lx <= l0) {
            ly = lx * slopeLow + (std::log10(minY) - slopeLow * l0);
        } else if (lx < l1) {
            double const coord = (knotsLow - 1) * (lx - l0) / (l1 - l0);
            int const j = std::min(int(coord), knotsLow - 2);
            ly = splineAt(low.data(), j, coord - j);
        } else if (lx < l2) {
            double const coord = (knotsHigh - 1) * (lx - l1) / (l2 - l1);
            int const j = std::min(int(coord), knotsHigh - 2);
            ly = splineAt(high.data(), j, coord - j);
        } else {
            ly = lx * slopeHigh + (std::log10(maxY) - slopeHigh * l2);
        }
        return std::pow(10.0, ly);
    }
};

Spline rrtSpline()
{
    Spline s;
    s.low = {-4.0000000000, -4.0000000000, -3.1573765773, -0.4852499958, 1.8477324706, 1.8477324706};
    s.high = {-0.7185482425, 2.0810307172, 3.6681241237, 4.0000000000, 4.0000000000, 4.0000000000};
    s.minX = 0.18 * std::pow(2.0, -15.0);
    s.minY = 0.0001;
    s.midX = 0.18;
    s.midY = 4.8;
    s.maxX = 0.18 * std::pow(2.0, 18.0);
    s.maxY = 10000.0;
    return s;
}

Spline const& rrt()
{
    static Spline const s = rrtSpline();
    return s;
}

Spline odt48Spline()
{
    Spline s;
    s.knotsLow = s.knotsHigh = 8;
    s.low = {-1.6989700043, -1.6989700043, -1.4779000000, -1.2291000000, -0.8648000000, -0.4480000000, 0.0051800000, 0.4511080334, 0.9113744414, 0.9113744414};
    s.high = {0.5154386965, 0.8470437783, 1.1358000000, 1.3802000000, 1.5197000000, 1.5985000000, 1.6467000000, 1.6746091357, 1.6878733390, 1.6878733390};
    s.minX = rrt().at(0.18 * std::pow(2.0, -6.5));
    s.minY = 0.02;
    s.midX = rrt().at(0.18);
    s.midY = 4.8;
    s.maxX = rrt().at(0.18 * std::pow(2.0, 6.5));
    s.maxY = 48.0;
    s.slopeHigh = 0.04;
    return s;
}

Spline const& odt48()
{
    static Spline const s = odt48Spline();
    return s;
}

// Linear interpolation in a table of (x, y) rows, held at its ends.
double interpolate(double const (*table)[2], int n, double p)
{
    if (p <= table[0][0])
        return table[0][1];
    if (p >= table[n - 1][0])
        return table[n - 1][1];
    for (int i = 0; i < n - 1; ++i)
        if (table[i][0] <= p && p < table[i + 1][0]) {
            double const s = (p - table[i][0]) / (table[i + 1][0] - table[i][0]);
            return table[i][1] * (1.0 - s) + table[i + 1][1] * s;
        }
    return table[n - 1][1];
}

// The single-stage tone scale of the ACES 1.1 output transforms, for a display's luminances.
Spline sstsSpline(double minLum, double maxLum, double expShift)
{
    constexpr double minStopSdr = -6.5, maxStopSdr = 6.5, minStopRrt = -15.0, maxStopRrt = 18.0;
    constexpr double minLumSdr = 0.02, maxLumSdr = 48.0, minLumRrt = 0.0001, maxLumRrt = 10000.0;
    double const minTable[2][2] = {{std::log10(minLumRrt), minStopRrt}, {std::log10(minLumSdr), minStopSdr}};
    double const maxTable[2][2] = {{std::log10(maxLumSdr), maxStopSdr}, {std::log10(maxLumRrt), maxStopRrt}};
    double const acesMin = 0.18 * std::pow(2.0, interpolate(minTable, 2, std::log10(minLum)));
    double const acesMax = 0.18 * std::pow(2.0, interpolate(maxTable, 2, std::log10(maxLum)));
    struct Point {
        double x, y, slope;
    };
    Point const lo{acesMin, minLum, 0.0}, mid{0.18, 4.8, 1.55}, hi{acesMax, maxLum, 0.0};
    auto straight = [](Point const& p, double at) { return p.slope * at + (std::log10(p.y) - p.slope * std::log10(p.x)); };
    Spline s;
    double const incLow = (std::log10(mid.x) - std::log10(lo.x)) / 3.0;
    s.low[0] = straight(lo, std::log10(lo.x) - 0.5 * incLow);
    s.low[1] = straight(lo, std::log10(lo.x) + 0.5 * incLow);
    s.low[3] = straight(mid, std::log10(mid.x) - 0.5 * incLow);
    s.low[4] = straight(mid, std::log10(mid.x) + 0.5 * incLow);
    double const bendsLow[2][2] = {{minStopRrt, 0.18}, {minStopSdr, 0.35}};
    double const pctLow = interpolate(bendsLow, 2, std::log2(lo.x / 0.18));
    s.low[2] = std::log10(lo.y) + pctLow * (std::log10(mid.y) - std::log10(lo.y));
    s.low[5] = s.low[4];
    double const incHigh = (std::log10(hi.x) - std::log10(mid.x)) / 3.0;
    s.high[0] = straight(mid, std::log10(mid.x) - 0.5 * incHigh);
    s.high[1] = straight(mid, std::log10(mid.x) + 0.5 * incHigh);
    s.high[3] = straight(hi, std::log10(hi.x) - 0.5 * incHigh);
    s.high[4] = straight(hi, std::log10(hi.x) + 0.5 * incHigh);
    double const bendsHigh[2][2] = {{maxStopSdr, 0.89}, {maxStopRrt, 0.90}};
    double const pctHigh = interpolate(bendsHigh, 2, std::log2(hi.x / 0.18));
    s.high[2] = std::log10(mid.y) + pctHigh * (std::log10(hi.y) - std::log10(mid.y));
    s.high[5] = s.high[4];
    auto shifted = [expShift](double x) { return std::pow(2.0, std::log2(x) - expShift); };
    s.minX = shifted(lo.x);
    s.minY = lo.y;
    s.midX = shifted(mid.x);
    s.midY = mid.y;
    s.maxX = shifted(hi.x);
    s.maxY = hi.y;
    s.slopeLow = lo.slope;
    s.slopeHigh = hi.slope;
    return s;
}

// The x a rising function gives y at, found by halving (in log x, over the range a tone scale can take).
template <typename F>
double rising(F f, double y)
{
    double lo = -12.0, hi = 12.0;
    if (y <= f(std::pow(10.0, lo)))
        return 0.0;
    for (int i = 0; i < 80; ++i) {
        double const m = 0.5 * (lo + hi);
        (f(std::pow(10.0, m)) < y ? lo : hi) = m;
    }
    return std::pow(10.0, 0.5 * (lo + hi));
}

// (Kept for the last luminances asked for on this thread: finding the shift takes a search.)
Spline const& ssts(double minLum, double midLum, double maxLum)
{
    thread_local std::array<double, 3> last{-1, -1, -1};
    thread_local Spline made;
    if (last != std::array<double, 3>{minLum, midLum, maxLum}) {
        Spline const plain = sstsSpline(minLum, maxLum, 0.0);
        double const expShift = std::log2(rising([&plain](double x) { return plain.at(x); }, midLum)) - std::log2(0.18);
        made = sstsSpline(minLum, maxLum, expShift);
        last = {minLum, midLum, maxLum};
    }
    return made;
}

double sigmoidShaper(double x)
{
    double const t = std::max(1.0 - std::abs(x / 2.0), 0.0);
    double const y = 1.0 + (x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0) * (1.0 - t * t);
    return y / 2.0;
}

double saturationOf(double const* rgb)
{
    double const mx = std::max({rgb[0], rgb[1], rgb[2]}), mn = std::min({rgb[0], rgb[1], rgb[2]});
    return (std::max(mx, kTiny) - std::max(mn, kTiny)) / std::max(mx, 1e-2);
}

double ycOf(double const* rgb)
{
    double const r = rgb[0], g = rgb[1], b = rgb[2];
    double const chroma = std::sqrt(std::max(b * (b - g) + g * (g - r) + r * (r - b), 0.0));
    return (b + g + r + 1.75 * chroma) / 3.0;
}

double hueOf(double const* rgb)
{
    if (rgb[0] == rgb[1] && rgb[1] == rgb[2])
        return 0.0;
    double h = (180.0 / kPi) * std::atan2(std::sqrt(3.0) * (rgb[1] - rgb[2]), 2.0 * rgb[0] - rgb[1] - rgb[2]);
    return h < 0.0 ? h + 360.0 : h;
}

double centred(double hue)
{
    return hue < -180.0 ? hue + 360.0 : hue > 180.0 ? hue - 360.0 : hue;
}

double cubicBasisShaper(double x, double w)
{
    constexpr double m[4][4] = {{-1. / 6, 3. / 6, -3. / 6, 1. / 6}, {3. / 6, -6. / 6, 3. / 6, 0.}, {-3. / 6, 0., 3. / 6, 0.}, {1. / 6, 4. / 6, 1. / 6, 0.}};
    double const k0 = -w / 2.0, k4 = w / 2.0;
    if (!(x > k0 && x < k4))
        return 0.0;
    double const coord = (x - k0) * 4.0 / w;
    int const j = std::clamp(int(coord), 0, 3);
    double const t = coord - j;
    double const mono[4] = {t * t * t, t * t, t, 1.0};
    int const col = 3 - j;
    double y = 0.0;
    for (int i = 0; i < 4; ++i)
        y += mono[i] * m[i][col];
    return y * 1.5;
}

void glow(Op const& op, double* rgb, bool back)
{
    double const gainIn = op.a[0] * sigmoidShaper((saturationOf(rgb) - 0.4) / 0.2), mid = op.a[1];
    double const yc = ycOf(rgb);
    double gain = 0.0;
    if (!back) {
        gain = yc <= 2.0 / 3.0 * mid ? gainIn : yc >= 2.0 * mid ? 0.0 : gainIn * (mid / yc - 0.5);
    } else {
        gain = yc <= (1.0 + gainIn) * 2.0 / 3.0 * mid ? -gainIn / (1.0 + gainIn) : yc >= 2.0 * mid ? 0.0 : gainIn * (mid / yc - 0.5) / (gainIn / 2.0 - 1.0);
    }
    for (int i = 0; i < 3; ++i)
        rgb[i] *= 1.0 + gain;
}

void redMod(Op const& op, double* rgb, bool back)
{
    double const scale = op.a[0], pivot = op.a[1], width = op.a[2];
    double const hue = centred(hueOf(rgb));
    double const weight = cubicBasisShaper(hue, width);
    if (weight <= 0.0)
        return;
    double const mn = std::min({rgb[0], rgb[1], rgb[2]}), mx = std::max({rgb[0], rgb[1], rgb[2]});
    double const oldRed = rgb[0];
    if (!back) {
        rgb[0] = rgb[0] + weight * saturationOf(rgb) * (pivot - rgb[0]) * (1.0 - scale);
    } else {
        // (Red is the largest where the weight is not 0; the smallest is green or blue by the side of red the hue is on.)
        double const minChan = hue < 0.0 ? rgb[1] : rgb[2];
        double const a = weight * (1.0 - scale) - 1.0;
        double const b = rgb[0] - weight * (pivot + minChan) * (1.0 - scale);
        double const c = weight * pivot * minChan * (1.0 - scale);
        rgb[0] = (-b - std::sqrt(std::max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
    }
    if (op.b[0] == 1.0 && mx - mn > kTiny) {
        // The 0.3 version keeps the hue: the middle channel keeps its place between the others.
        double const newMax = rgb[0];
        int const midChan = rgb[1] >= rgb[2] ? 1 : 2;
        rgb[midChan] = mn + (rgb[midChan] - mn) * (newMax - mn) / std::max(oldRed - mn, kTiny);
    }
}

void darkToDim(Op const& op, double* rgb, bool back)
{
    double const gamma = back ? 1.0 / op.a[0] : op.a[0];
    double const y = std::max(kTiny, kAp1Y[0] * rgb[0] + kAp1Y[1] * rgb[1] + kAp1Y[2] * rgb[2]);
    double const k = std::pow(y, gamma - 1.0);
    for (int i = 0; i < 3; ++i)
        rgb[i] *= k;
}

double compress(double dist, double lim, double thr, double pwr, bool back)
{
    if (dist < thr)
        return dist;
    double const scl = (lim - thr) / std::pow(std::pow((1.0 - thr) / (lim - thr), -pwr) - 1.0, 1.0 / pwr);
    double const nd = (dist - thr) / scl;
    double const p = std::pow(nd, pwr);
    if (!back)
        return thr + scl * nd / std::pow(1.0 + p, 1.0 / pwr);
    if (dist > thr + scl)
        return dist;
    return thr + scl * std::pow(-(p / (p - 1.0)), 1.0 / pwr);
}

void gamutComp(Op const& op, double* rgb, bool back)
{
    double const ach = std::max({rgb[0], rgb[1], rgb[2]});
    if (ach == 0.0)
        return;
    for (int i = 0; i < 3; ++i) {
        size_t const k = size_t(i);
        double const dist = (ach - rgb[i]) / std::abs(ach);
        rgb[i] = ach - compress(dist, op.a[k], op.b[k], op.c[0], back) * std::abs(ach);
    }
}

double rollWhite(double in, double newWhite, double width, bool back)
{
    double const x0 = -1.0, x1 = x0 + width, y0 = -newWhite, y1 = x1, m1 = x1 - x0;
    double const a = y0 - y1 + m1, b = 2.0 * (y1 - y0) - m1, c = y0;
    if (back) {
        if (-in < y0)
            return -x0;
        if (-in > y1)
            return in;
        double const cc = c + in;
        double const t = 2.0 * cc / (-std::sqrt(std::max(b * b - 4.0 * a * cc, 0.0)) - b);
        return -(t * (x1 - x0) + x0);
    }
    double const t = (-in - x0) / (x1 - x0);
    if (t < 0.0)
        return -(t * b + c);
    if (t > 1.0)
        return in;
    return -((t * a + b) * t + c);
}

double adxExposure(double cid, bool back)
{
    static double const table[11][2] = {{-0.190000000000000, -6.000000000000000}, {0.010000000000000, -2.721718645000000}, {0.028000000000000, -2.521718645000000},
                                        {0.054000000000000, -2.321718645000000},  {0.095000000000000, -2.121718645000000}, {0.145000000000000, -1.921718645000000},
                                        {0.220000000000000, -1.721718645000000},  {0.300000000000000, -1.521718645000000}, {0.400000000000000, -1.321718645000000},
                                        {0.500000000000000, -1.121718645000000},  {0.600000000000000, -0.926545676714876}};
    double const refPt = (7120.0 - 1520.0) / 8000.0 * (100.0 / 55.0) - std::log10(0.18);
    double const firstSlope = (table[1][1] - table[0][1]) / (table[1][0] - table[0][0]);
    double const top = std::log10(65504.0);
    if (!back) {
        if (cid < table[0][0])
            return std::max(table[0][1] + (cid - table[0][0]) * firstSlope, -10.0);
        return cid <= 0.6 ? interpolate(table, 11, cid) : std::min((100.0 / 55.0) * cid - refPt, top);
    }
    if (cid > table[10][1])
        return (std::min(cid, top) + refPt) * 55.0 / 100.0;
    if (cid < table[0][1])
        return table[0][0] + (std::max(cid, -10.0) - table[0][1]) / firstSlope;
    double swapped[11][2];
    for (int i = 0; i < 11; ++i) {
        swapped[i][0] = table[i][1];
        swapped[i][1] = table[i][0];
    }
    return interpolate(swapped, 11, cid);
}

}  // namespace

namespace Cms {

bool runAces(Op const& op, float* rgb, bool back)
{
    double v[3] = {rgb[0], rgb[1], rgb[2]};
    switch (FixedStyle(op.style)) {
    case FixedStyle::AcesGlow:
        glow(op, v, back);
        break;
    case FixedStyle::AcesRedMod:
        redMod(op, v, back);
        break;
    case FixedStyle::AcesDarkToDim:
        darkToDim(op, v, back);
        break;
    case FixedStyle::AcesGamutComp:
        gamutComp(op, v, back);
        break;
    case FixedStyle::AcesToneScale: {
        int const which = int(op.b[0]);
        Spline const& s = which == 0 ? rrt() : which == 1 ? odt48() : ssts(op.a[0], op.a[1], op.a[2]);
        for (double& c : v)
            c = back ? rising([&s](double x) { return s.at(x); }, c) : s.at(c);
        break;
    }
    case FixedStyle::AcesRollWhite:
        for (double& c : v)
            c = rollWhite(c, op.a[0], op.a[1], back);
        break;
    case FixedStyle::AdxDensity:
        for (double& c : v)
            c = adxExposure(c, back);
        break;
    case FixedStyle::AcesOutputTwo:
        runAcesTwo(op, rgb, back);
        return true;
    default:
        return false;
    }
    for (int i = 0; i < 3; ++i)
        rgb[i] = float(v[i]);
    return true;
}

}  // namespace Cms
