// Eclipse - the ACES 2 output transform: a colour appearance model (J, M, h), a tone scale on J, chroma compression and gamut mapping to a display.
#include "colour/maths/ColourOps.h"
#include "colour/maths/ColourPrimaries.h"

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>

namespace {

using namespace Cms;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRefLum = 100.0;
constexpr double kAdaptingLum = 100.0;
constexpr double kBackground = 20.0;
constexpr double kSurroundC = 0.59, kSurroundNc = 0.9;
constexpr double kRa = 2.0, kBa = 0.05;
constexpr int kTable = 362;     // a value per whole degree, with one more at each end (entry i is hue i - 1)

using V3 = std::array<double, 3>;

V3 times(Matrix3 const& m, V3 const& v)
{
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

// The parts of the appearance model that do not change with the primaries.
struct Model {
    double fl = 0, cz = 0, czInv = 0, wa = 0;
    Matrix3 toAab{}, fromAab{};
    Matrix3 cam16{};

    Model()
    {
        double const k = 1.0 / (5.0 * kAdaptingLum + 1.0);
        double const k4 = k * k * k * k;
        fl = 0.2 * k4 * (5.0 * kAdaptingLum) + 0.1 * std::pow(1.0 - k4, 2.0) * std::pow(5.0 * kAdaptingLum, 1.0 / 3.0);
        cz = kSurroundC * (1.48 + std::sqrt(kBackground / kAdaptingLum));
        czInv = 1.0 / cz;
        wa = compress(fl);
        double const aw = (kRa + 1.0 + kBa) * wa;
        double const s = 43.0 * kSurroundNc * 400.0;
        toAab = {kRa / aw, 1.0 / aw, kBa / aw, s, -s * 12.0 / 11.0, s / 11.0, s / 9.0, s / 9.0, -2.0 * s / 9.0};
        fromAab = inverse(toAab);
        Primaries const p{0.8336, 0.1735, 2.3854, -1.4659, 0.087, -0.125, 0.333, 0.333};
        cam16 = inverse(rgbToXyz(p));
    }

    static double compress(double v)
    {
        double const f = std::pow(std::abs(v), 0.42);
        return (v < 0 ? -f : f) / (27.13 + f);
    }

    static double expand(double v)
    {
        double const lim = std::min(std::abs(v), 0.99);
        double const out = std::pow(27.13 * lim / (1.0 - lim), 1.0 / 0.42);
        return v < 0 ? -out : out;
    }

    double jOfY(double y) const
    {
        double const f = std::pow(fl * y, 0.42);
        return 100.0 * std::pow(f / (27.13 + f) / wa, cz);
    }
};

Model const& model()
{
    static Model const m;
    return m;
}

// The model for one set of primaries: linear RGB (1.0 = 100 nits) to cone responses, adapted to their white.
struct Cam {
    Matrix3 toLms{}, fromLms{};

    explicit Cam(Primaries const& p)
    {
        Model const& m = model();
        Matrix3 const toXyz = rgbToXyz(p);
        V3 const white = times(m.cam16, times(toXyz, {kRefLum, kRefLum, kRefLum}));
        Matrix3 scaled = multiply(m.cam16, toXyz);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                scaled[size_t(r * 3 + c)] *= kRefLum * m.fl / white[size_t(r)];
        toLms = scaled;
        fromLms = inverse(scaled);
    }

    V3 jmh(V3 const& rgb) const
    {
        Model const& m = model();
        V3 const lms = times(toLms, rgb);
        V3 const aab = times(m.toAab, {Model::compress(lms[0]), Model::compress(lms[1]), Model::compress(lms[2])});
        if (aab[0] <= 0.0)
            return {0, 0, 0};
        double const j = 100.0 * std::pow(aab[0], m.cz);
        double const mm = j == 0.0 ? 0.0 : std::sqrt(aab[1] * aab[1] + aab[2] * aab[2]);
        double h = aab[1] == 0.0 ? 0.0 : std::atan2(aab[2], aab[1]) * 180.0 / kPi;
        h -= std::floor(h / 360.0) * 360.0;
        return {j, mm, h < 0.0 ? h + 360.0 : h};
    }

    V3 rgb(V3 const& jmh) const
    {
        Model const& m = model();
        double const hr = jmh[2] * kPi / 180.0;
        V3 const aab{std::pow(std::max(jmh[0], 0.0) / 100.0, m.czInv), jmh[1] * std::cos(hr), jmh[1] * std::sin(hr)};
        V3 const a = times(m.fromAab, aab);
        return times(fromLms, {Model::expand(a[0]), Model::expand(a[1]), Model::expand(a[2])});
    }
};

Primaries ap0()
{
    return ap0Primaries();
}

// Everything the transform needs for one display: its peak and limiting primaries.
struct Display {
    double peak = 100;
    Cam input{ap0()}, limit{ap0()}, reach{ap1Primaries()};
    double limitJmax = 100, midJ = 0, focusDist = 1.35, gammaBottomInv = 1.0;
    double m2 = 0, s2 = 0, g = 1.15, t1 = 0.04, inverseLimit = 1;
    double compr = 0, sat = 0, satThr = 0;
    std::array<double, 7> norm{};
    std::array<double, kTable> reachM{};
    std::array<V3, kTable> cusp{};      // J, M and the inverse of the upper hull's gamma

    double reachAt(double h) const
    {
        double const base = std::floor(h);
        int const i = std::clamp(int(base) + 1, 0, kTable - 2);
        return lerp(reachM[size_t(i)], reachM[size_t(i + 1)], h - base);
    }

    V3 cuspAt(double h) const
    {
        double const base = std::floor(h);
        int const i = std::clamp(int(base) + 1, 0, kTable - 2);
        double const t = h - base;
        V3 const& a = cusp[size_t(i)];
        V3 const& b = cusp[size_t(i + 1)];
        return {lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)};
    }

    double mnorm(double cosH, double sinH) const
    {
        double const c2 = 2.0 * cosH * cosH - 1.0, s2v = 2.0 * cosH * sinH;
        double const c3 = 4.0 * cosH * cosH * cosH - 3.0 * cosH, s3 = 3.0 * sinH - 4.0 * sinH * sinH * sinH;
        return norm[0] * cosH + norm[1] * c2 + norm[2] * c3 + norm[3] * sinH + norm[4] * s2v + norm[5] * s3 + norm[6];
    }

    double toneFwd(double j) const
    {
        Model const& m = model();
        double const a = m.wa * std::pow(std::abs(j) / 100.0, m.czInv);
        double const y = std::pow(27.13 * a / (1.0 - a), 1.0 / 0.42);
        double const f = m2 * std::pow(y / (y + s2 * m.fl), g);
        double const yts = std::max(0.0, f * f / (f + t1));
        double const fly = std::pow(m.fl * yts, 0.42);
        double const out = 100.0 * std::pow(fly / (27.13 + fly) / m.wa, m.cz);
        return j < 0 ? -out : out;
    }

    double toneInv(double j) const
    {
        Model const& m = model();
        double const a = m.wa * std::pow(std::abs(j) / 100.0, m.czInv);
        double const y = std::pow(27.13 * a / (1.0 - a), 1.0 / 0.42);
        double const z = std::clamp(y / m.fl, 0.0, inverseLimit);
        double const ht = 0.5 * (z + std::sqrt(z * (4.0 * t1 + z)));
        double const yo = s2 * m.fl / (std::pow(m2 / ht, 1.0 / g) - 1.0);
        double const fly = std::pow(std::abs(yo), 0.42);
        double const out = 100.0 * std::pow(fly / (27.13 + fly) / m.wa, m.cz);
        return j < 0 ? -out : out;
    }

    double focusGain(double j, double cuspJ) const
    {
        double const thr = lerp(cuspJ, limitJmax, 0.3);
        if (j <= thr)
            return 1.0;
        double const gain = std::log10((limitJmax - thr) / std::max(0.0001, limitJmax - j));
        return gain * gain + 1.0;
    }

    double solveJ(double j, double m, double focusJ, double slopeGain) const
    {
        double const ms = m / slopeGain;
        double const a = ms / focusJ;
        if (j < focusJ) {
            double const b = 1.0 - ms, c = -j;
            return -2.0 * c / (b + std::sqrt(b * b - 4.0 * a * c));
        }
        double const b = -(1.0 + ms + limitJmax * a), c = limitJmax * ms + j;
        return -2.0 * c / (b - std::sqrt(b * b - 4.0 * a * c));
    }

    double boundary(V3 const& jmCusp, double gammaTopInv, double jSource, double jCusp, double slope) const
    {
        double const lower = jCusp * std::pow(jSource / jCusp, gammaBottomInv) / (jmCusp[0] / jmCusp[1] - slope);
        double const upper = jmCusp[1] * (limitJmax - jCusp) * std::pow((limitJmax - jSource) / (limitJmax - jCusp), gammaTopInv) / (slope * jmCusp[1] + limitJmax - jmCusp[0]);
        double const s = 0.12 * jmCusp[1];
        double const k = std::max(s - std::abs(lower - upper), 0.0) / s;
        return std::min(lower, upper) - k * k * k * s / 6.0;
    }

    double focusJOf(double cuspJ) const
    {
        return lerp(cuspJ, midJ, std::min(1.0, 1.3 - cuspJ / limitJmax));
    }

    // The gamut compression along the line to the focus point; `jx` sets how strongly the line leans.
    V3 compress(V3 const& jmh, double jx, V3 const& jmgCusp, double reachMax, bool back) const
    {
        double const j = jmh[0], m = jmh[1], h = jmh[2];
        if (m <= 0.0 || j > limitJmax)
            return {j, 0.0, h};
        double const focusJ = focusJOf(jmgCusp[0]);
        double const slopeGain = limitJmax * focusDist * focusGain(jx, jmgCusp[0]);
        double const jSource = solveJ(j, m, focusJ, slopeGain);
        double slope = jSource < focusJ ? jSource : limitJmax - jSource;
        slope = slope * (jSource - focusJ) / (focusJ * slopeGain);
        double const jCusp = solveJ(jmgCusp[0], jmgCusp[1], focusJ, slopeGain);
        double const gamutM = boundary(jmgCusp, jmgCusp[2], jSource, jCusp, slope);
        if (gamutM <= 0.0)
            return {j, 0.0, h};
        double reachBoundary = limitJmax * std::pow(jSource / limitJmax, model().czInv);
        reachBoundary = reachBoundary / (limitJmax / reachMax - slope);
        double const mOut = remap(m, gamutM, reachBoundary, back);
        return {jSource + mOut * slope, mOut, h};
    }

    static double remap(double m, double gamutM, double reachM, bool back)
    {
        double const proportion = std::max(gamutM / reachM, 0.75);
        double const threshold = proportion * gamutM;
        if (proportion >= 1.0 || m <= threshold)
            return m;
        double const mOffset = m - threshold, gamutOffset = gamutM - threshold, reachOffset = reachM - threshold;
        double const scale = reachOffset / (reachOffset / gamutOffset - 1.0);
        double const nd = mOffset / scale;
        if (!back)
            return threshold + scale * nd / (1.0 + nd);
        return nd >= 1.0 ? threshold + scale : threshold + scale * -(nd / (nd - 1.0));
    }

    static double toeFwd(double x, double limitV, double k1In, double k2In)
    {
        double const k2 = std::max(k2In, 0.001), k1 = std::sqrt(k1In * k1In + k2 * k2), k3 = (limitV + k1) / (limitV + k2);
        return x > limitV ? x : 0.5 * (k3 * x - k1 + std::sqrt((k3 * x - k1) * (k3 * x - k1) + 4.0 * k2 * k3 * x));
    }

    static double toeInv(double x, double limitV, double k1In, double k2In)
    {
        double const k2 = std::max(k2In, 0.001), k1 = std::sqrt(k1In * k1In + k2 * k2), k3 = (limitV + k1) / (limitV + k2);
        return x > limitV ? x : (x * x + k1 * x) / (k3 * (x + k2));
    }

    void forward(double* rgb) const
    {
        V3 jmh = input.jmh({rgb[0], rgb[1], rgb[2]});
        double const hr = jmh[2] * kPi / 180.0, cosH = std::cos(hr), sinH = std::sin(hr);
        double const jts = toneFwd(jmh[0]);
        double const reachMax = reachAt(jmh[2]);
        double mcp = jmh[1];
        if (mcp != 0.0) {
            double const nj = jts / limitJmax, snj = std::max(0.0, 1.0 - nj);
            double const mn = mnorm(cosH, sinH);
            double const lim = std::pow(nj, model().czInv) * reachMax / mn;
            mcp = jmh[1] * std::pow(jts / jmh[0], model().czInv) / mn;
            mcp = lim - toeFwd(lim - mcp, lim - 0.001, snj * sat, std::sqrt(nj * nj + satThr));
            mcp = toeFwd(mcp, lim, nj * compr, snj);
            mcp *= mn;
        }
        jmh = compress({jts, mcp, jmh[2]}, jts, cuspAt(jmh[2]), reachMax, false);
        V3 const out = limit.rgb(jmh);
        for (int i = 0; i < 3; ++i)
            rgb[i] = out[size_t(i)];
    }

    void backward(double* rgb) const
    {
        V3 jmh = limit.jmh({rgb[0], rgb[1], rgb[2]});
        double const hr = jmh[2] * kPi / 180.0, cosH = std::cos(hr), sinH = std::sin(hr);
        double const reachMax = reachAt(jmh[2]);
        V3 const jmgCusp = cuspAt(jmh[2]);
        double jx = jmh[0];
        if (jx > lerp(jmgCusp[0], limitJmax, 0.3))
            jx = compress(jmh, jx, jmgCusp, reachMax, true)[0];
        jmh = compress(jmh, jx, jmgCusp, reachMax, true);
        double const jts = jmh[0];
        double const j = toneInv(jts);
        double m = jmh[1];
        if (m != 0.0) {
            double const nj = jts / limitJmax, snj = std::max(0.0, 1.0 - nj);
            double const mn = mnorm(cosH, sinH);
            double const lim = std::pow(nj, model().czInv) * reachMax / mn;
            m = m / mn;
            m = toeInv(m, lim, nj * compr, snj);
            m = lim - toeInv(lim - m, lim - 0.001, snj * sat, std::sqrt(nj * nj + satThr));
            m *= mn;
            m *= std::pow(jts / j, -model().czInv);
        }
        V3 const out = input.rgb({j, m, jmh[2]});
        for (int i = 0; i < 3; ++i)
            rgb[i] = out[size_t(i)];
    }
};

bool anyBelowZero(V3 const& v)
{
    return v[0] < 0.0 || v[1] < 0.0 || v[2] < 0.0;
}

// The point of a fully saturated, fully bright colour of the display gamut at a place round the edge of its cube (0..6).
V3 ringColour(double at, double scale)
{
    double const t = at - std::floor(at / 6.0) * 6.0;
    int const side = std::min(int(t), 5);
    double const f = t - side;
    V3 c;
    switch (side) {
    case 0: c = {1, f, 0}; break;
    case 1: c = {1 - f, 1, 0}; break;
    case 2: c = {0, 1, f}; break;
    case 3: c = {0, 1 - f, 1}; break;
    case 4: c = {f, 0, 1}; break;
    default: c = {1, 0, 1 - f}; break;
    }
    return {c[0] * scale, c[1] * scale, c[2] * scale};
}

double hueGap(double a, double b)
{
    double d = b - a;
    d -= std::round(d / 360.0) * 360.0;
    return d;
}

std::unique_ptr<Display> makeDisplay(double peak, Primaries const& limitP)
{
    auto d = std::make_unique<Display>();
    Model const& m = model();
    d->peak = peak;
    d->limit = Cam(limitP);
    // The tone scale.
    double const n = peak, nr = 100.0, c = 0.18, cd = 10.013, wg = 0.14, rHitMin = 128.0, rHitMax = 896.0;
    double const rHit = rHitMin + (rHitMax - rHitMin) * (std::log(n / nr) / std::log(10000.0 / 100.0));
    double const m0 = n / nr;
    double const m1 = 0.5 * (m0 + std::sqrt(m0 * (m0 + 4.0 * d->t1)));
    double const u = std::pow((rHit / m1) / ((rHit / m1) + 1.0), d->g);
    double const mm = m1 / u;
    double const wi = std::log2(n / 100.0);
    double const ct = cd / nr * (1.0 + wi * wg);
    double const gip = 0.5 * (ct + std::sqrt(ct * (ct + 4.0 * d->t1)));
    double const gipp2 = -(m1 * std::pow(gip / mm, 1.0 / d->g)) / (std::pow(gip / mm, 1.0 / d->g) - 1.0);
    double const w2 = c / gipp2;
    double const u2 = std::pow((rHit / m1) / ((rHit / m1) + w2), d->g);
    d->s2 = w2 * m1;
    d->m2 = m1 / u2;
    d->inverseLimit = n / (u2 * nr);
    double const logPeak = std::log10(n / nr);
    d->limitJmax = m.jOfY(n / kRefLum);
    d->midJ = m.jOfY(ct);
    d->focusDist = 1.35 + 1.35 * 1.75 * logPeak;
    d->gammaBottomInv = 1.0 / (1.14 + 0.07 * logPeak);
    d->compr = 2.4 + 2.4 * 3.3 * logPeak;
    d->sat = std::max(0.2, 1.3 - 1.3 * 0.69 * logPeak);
    d->satThr = 0.5 / n;
    double const scale = std::pow(0.03379 * n, 0.30596) - 0.45135;
    std::array<double, 7> const w{11.34072, 16.46899, 7.88380, 14.66441, -6.37224, 9.19364, 77.12896};
    for (size_t i = 0; i < 7; ++i)
        d->norm[i] = w[i] * scale;
    // The reach of AP1 at the top of the display's range, per degree of hue.
    for (int i = 0; i < kTable; ++i) {
        double const hue = i - 1.0 - std::floor((i - 1.0) / 360.0) * 360.0;
        double low = 0.0, high = 50.0;
        while (high < 1300.0 && !anyBelowZero(d->reach.rgb({d->limitJmax, high, hue}))) {
            low = high;
            high += 50.0;
        }
        while (high - low > 1e-2) {
            double const mid = 0.5 * (low + high);
            (anyBelowZero(d->reach.rgb({d->limitJmax, mid, hue})) ? high : low) = mid;
        }
        d->reachM[size_t(i)] = high;
    }
    // The cusp of the display gamut (its most colourful colour) per degree of hue, found round the edge of the cube.
    double const top = peak / kRefLum;
    constexpr int ringSteps = 6 * 64;
    std::array<double, ringSteps + 1> ringHue{};
    for (int s = 0; s <= ringSteps; ++s)
        ringHue[size_t(s)] = d->limit.jmh(ringColour(6.0 * s / ringSteps, top))[2];
    for (int i = 0; i < kTable; ++i) {
        double const hue = i - 1.0 - std::floor((i - 1.0) / 360.0) * 360.0;
        int found = 0;
        for (int s = 0; s < ringSteps; ++s) {
            double const a = hueGap(ringHue[size_t(s)], hue), b = hueGap(ringHue[size_t(s + 1)], hue);
            if (a >= 0.0 && b < 0.0 && a - b < 180.0) {
                found = s;
                break;
            }
        }
        double lo = 6.0 * found / ringSteps, hi = 6.0 * (found + 1) / ringSteps;
        for (int k = 0; k < 50; ++k) {
            double const mid = 0.5 * (lo + hi);
            (hueGap(d->limit.jmh(ringColour(mid, top))[2], hue) >= 0.0 ? lo : hi) = mid;
        }
        V3 const jmh = d->limit.jmh(ringColour(0.5 * (lo + hi), top));
        // (Its M is widened a little, as the smoothing of the boundary at the cusp would otherwise cut into the gamut.)
        d->cusp[size_t(i)] = {jmh[0], jmh[1] * (1.0 + 0.27 * 0.12), 1.0};
    }
    // The upper hull's gamma per degree: the smallest whose boundary leaves the gamut at three heights over the cusp.
    double const testAt[3] = {0.01, 0.5, 0.99};
    for (int i = 0; i < kTable; ++i) {
        double const hue = i - 1.0 - std::floor((i - 1.0) / 360.0) * 360.0;
        V3 const jmCusp = d->cusp[size_t(i)];
        auto fits = [&](double gammaTop) {
            double const focusJ = d->focusJOf(jmCusp[0]);
            for (double at : testAt) {
                double const testJ = jmCusp[0] + (d->limitJmax - jmCusp[0]) * at;
                double const slopeGain = d->limitJmax * d->focusDist * d->focusGain(testJ, jmCusp[0]);
                double const jSource = d->solveJ(testJ, jmCusp[1], focusJ, slopeGain);
                double slope = jSource < focusJ ? jSource : d->limitJmax - jSource;
                slope = slope * (jSource - focusJ) / (focusJ * slopeGain);
                double const jCusp = d->solveJ(jmCusp[0], jmCusp[1], focusJ, slopeGain);
                double const mB = d->boundary({jmCusp[0], jmCusp[1], 0}, 1.0 / gammaTop, jSource, jCusp, slope);
                V3 const rgb = d->limit.rgb({jSource + mB * slope, mB, hue});
                if (!(rgb[0] / top > 1.0 || rgb[1] / top > 1.0 || rgb[2] / top > 1.0))
                    return false;
            }
            return true;
        };
        double low = 0.0, high = 0.4;
        while (high < 5.0 && !fits(high)) {
            low = high;
            high += 0.4;
        }
        while (high - low > 1e-5) {
            double const mid = 0.5 * (low + high);
            (fits(mid) ? high : low) = mid;
        }
        d->cusp[size_t(i)][2] = 1.0 / high;
    }
    return d;
}

// (Made once per display and kept: building the tables takes a few milliseconds.)
Display const& displayFor(Op const& op)
{
    static QMutex lock;
    static std::map<std::array<double, 9>, std::unique_ptr<Display>> made;
    std::array<double, 9> const key{op.a[0], op.a[1], op.a[2], op.b[0], op.b[1], op.b[2], op.c[0], op.c[1], op.c[2]};
    thread_local std::array<double, 9> lastKey{};
    thread_local Display const* last = nullptr;
    if (last && lastKey == key)
        return *last;
    QMutexLocker locker(&lock);
    auto it = made.find(key);
    if (it == made.end()) {
        Primaries const p{op.a[1], op.a[2], op.b[0], op.b[1], op.b[2], op.c[0], op.c[1], op.c[2]};
        it = made.emplace(key, makeDisplay(op.a[0], p)).first;
    }
    last = it->second.get();
    lastKey = key;
    return *last;
}

}  // namespace

namespace Cms {

void runAcesTwo(Op const& op, float* rgb, bool back)
{
    Display const& d = displayFor(op);
    double v[3] = {rgb[0], rgb[1], rgb[2]};
    if (back)
        d.backward(v);
    else
        d.forward(v);
    for (int i = 0; i < 3; ++i)
        rgb[i] = float(v[i]);
}

std::array<double, 3> acesTwoTables(Op const& op, int entry)
{
    Display const& d = displayFor(op);
    return {d.reachM[size_t(entry)], d.cusp[size_t(entry)][0], d.cusp[size_t(entry)][1]};
}

}  // namespace Cms
