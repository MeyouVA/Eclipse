// Eclipse - running the steps of a colour conversion, and turning them round.
#include "colour/maths/ColourOps.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using namespace Cms;

constexpr double kTiny = 1e-10;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

// A power with a straight piece near black (the curve of sRGB and its kind), coded values to light.
double monDecode(double v, double g, double o)
{
    double const breakAt = o / (g - 1.0);
    double const slope = ((g - 1.0) / o) * std::pow(o * g / ((g - 1.0) * (1.0 + o)), g);
    return v > breakAt ? std::pow((v + o) / (1.0 + o), g) : v * slope;
}

double monEncode(double v, double g, double o)
{
    double const breakAt = o / (g - 1.0);
    double const slope = ((g - 1.0) / o) * std::pow(o * g / ((g - 1.0) * (1.0 + o)), g);
    return v > breakAt * slope ? (1.0 + o) * std::pow(v, 1.0 / g) - o : v / slope;
}

double power(double v, double g, int style)
{
    if (v >= 0.0)
        return std::pow(v, g);
    return style == 1 ? -std::pow(-v, g) : style == 2 ? v : 0.0;
}

double logOf(double v, double base)
{
    return std::log(std::max(v, kTiny)) / std::log(base);
}

// LogCamera: the straight piece below the break, taken to meet the log piece there (and, unless given, at its slope).
void cameraLine(Op const& op, int i, double* slope, double* offset)
{
    double const at = op.e[i];
    double const inside = op.c[i] * at + op.d[i];
    double const logAt = op.a[i] * logOf(inside, op.base) + op.b[i];
    *slope = op.f[i] != 0.0 ? op.f[i] : op.a[i] * op.c[i] / (std::max(inside, kTiny) * std::log(op.base));
    *offset = logAt - *slope * at;
}

double lut1d(Op const& op, int channel, double x)
{
    QVector<float> const& t = *op.table;
    int const n = op.size;
    double const span = op.inHigh[channel] - op.inLow[channel];
    double const at = std::clamp((x - op.inLow[channel]) / (span != 0.0 ? span : 1.0), 0.0, 1.0) * (n - 1);
    int const i = std::min(int(at), n - 2);
    float const* const row = t.constData() + qsizetype(channel) * n;
    return lerp(row[i], row[i + 1], at - i);
}

double lut1dBack(Op const& op, int channel, double y)
{
    QVector<float> const& t = *op.table;
    int const n = op.size;
    float const* const row = t.constData() + qsizetype(channel) * n;
    bool const rising = row[n - 1] >= row[0];
    // (The table is taken to run one way: the place where it passes the value is searched for.)
    int lo = 0, hi = n - 1;
    auto before = [&](int i) { return rising ? row[i] <= y : row[i] >= y; };
    if (!before(0))
        return op.inLow[channel];
    if (before(n - 1))
        return op.inHigh[channel];
    while (hi - lo > 1) {
        int const mid = (lo + hi) / 2;
        if (before(mid))
            lo = mid;
        else
            hi = mid;
    }
    double const run = row[hi] - row[lo];
    double const t2 = run != 0.0 ? (y - row[lo]) / run : 0.0;
    return op.inLow[channel] + (lo + t2) / (n - 1) * (op.inHigh[channel] - op.inLow[channel]);
}

void lut3d(Op const& op, float* rgb)
{
    QVector<float> const& t = *op.table;
    int const n = op.size;
    double p[3];
    int i0[3], i1[3];
    double fr[3];
    for (int c = 0; c < 3; ++c) {
        double const span = op.inHigh[c] - op.inLow[c];
        p[c] = std::clamp((rgb[c] - op.inLow[c]) / (span != 0.0 ? span : 1.0), 0.0, 1.0) * (n - 1);
        i0[c] = std::min(int(p[c]), n - 2);
        i1[c] = i0[c] + 1;
        fr[c] = p[c] - i0[c];
    }
    auto at = [&](int r, int g, int b, int c) { return double(t[(qsizetype(b) * n * n + qsizetype(g) * n + r) * 3 + c]); };
    if (!op.tetrahedral) {
        for (int c = 0; c < 3; ++c) {
            double const c00 = lerp(at(i0[0], i0[1], i0[2], c), at(i1[0], i0[1], i0[2], c), fr[0]);
            double const c10 = lerp(at(i0[0], i1[1], i0[2], c), at(i1[0], i1[1], i0[2], c), fr[0]);
            double const c01 = lerp(at(i0[0], i0[1], i1[2], c), at(i1[0], i0[1], i1[2], c), fr[0]);
            double const c11 = lerp(at(i0[0], i1[1], i1[2], c), at(i1[0], i1[1], i1[2], c), fr[0]);
            rgb[c] = float(lerp(lerp(c00, c10, fr[1]), lerp(c01, c11, fr[1]), fr[2]));
        }
        return;
    }
    double const r = fr[0], g = fr[1], b = fr[2];
    for (int c = 0; c < 3; ++c) {
        double const v000 = at(i0[0], i0[1], i0[2], c), v111 = at(i1[0], i1[1], i1[2], c);
        double v;
        if (r > g) {
            if (g > b)
                v = (1 - r) * v000 + (r - g) * at(i1[0], i0[1], i0[2], c) + (g - b) * at(i1[0], i1[1], i0[2], c) + b * v111;
            else if (r > b)
                v = (1 - r) * v000 + (r - b) * at(i1[0], i0[1], i0[2], c) + (b - g) * at(i1[0], i0[1], i1[2], c) + g * v111;
            else
                v = (1 - b) * v000 + (b - r) * at(i0[0], i0[1], i1[2], c) + (r - g) * at(i1[0], i0[1], i1[2], c) + g * v111;
        } else {
            if (b > g)
                v = (1 - b) * v000 + (b - g) * at(i0[0], i0[1], i1[2], c) + (g - r) * at(i0[0], i1[1], i1[2], c) + r * v111;
            else if (b > r)
                v = (1 - g) * v000 + (g - b) * at(i0[0], i1[1], i0[2], c) + (b - r) * at(i0[0], i1[1], i1[2], c) + r * v111;
            else
                v = (1 - g) * v000 + (g - r) * at(i0[0], i1[1], i0[2], c) + (r - b) * at(i1[0], i1[1], i0[2], c) + b * v111;
        }
        rgb[c] = float(v);
    }
}

void hsv(float* rgb, bool back)
{
    double r = rgb[0], g = rgb[1], b = rgb[2];
    if (!back) {
        double const mx = std::max({r, g, b}), mn = std::min({r, g, b});
        double const range = mx - mn;
        double h = 0.0;
        if (range > 0.0) {
            if (mx == r)
                h = (g - b) / range;
            else if (mx == g)
                h = 2.0 + (b - r) / range;
            else
                h = 4.0 + (r - g) / range;
            h /= 6.0;
            if (h < 0.0)
                h += 1.0;
        }
        rgb[0] = float(h);
        rgb[1] = float(mx > 0.0 ? range / mx : 0.0);
        rgb[2] = float(mx);
        return;
    }
    double const h = (r - std::floor(r)) * 6.0, s = g, v = b;
    int const sector = int(h) % 6;
    double const f = h - std::floor(h);
    double const p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double const out[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    for (int c = 0; c < 3; ++c)
        rgb[c] = float(out[sector][c]);
}

void fixed(Op const& op, float* rgb, bool back)
{
    double const X = rgb[0], Y = rgb[1], Z = rgb[2];
    switch (FixedStyle(op.style)) {
    case FixedStyle::RgbToHsv:
        hsv(rgb, back);
        return;
    case FixedStyle::XyzToXyy:
        if (!back) {
            double const sum = X + Y + Z;
            rgb[0] = float(sum != 0.0 ? X / sum : 0.3127);
            rgb[1] = float(sum != 0.0 ? Y / sum : 0.3290);
            rgb[2] = float(Y);
        } else {
            double const x = X, y = Y, big = Z;
            rgb[0] = float(y != 0.0 ? x * big / y : 0.0);
            rgb[1] = float(big);
            rgb[2] = float(y != 0.0 ? (1.0 - x - y) * big / y : 0.0);
        }
        return;
    case FixedStyle::XyzToUvy:
    case FixedStyle::XyzToLuv: {
        constexpr double un = 0.19783982482140775, vn = 0.46833630293240970;
        bool const luv = FixedStyle(op.style) == FixedStyle::XyzToLuv;
        if (!back) {
            double const d = X + 15.0 * Y + 3.0 * Z;
            double const u = d != 0.0 ? 4.0 * X / d : un, v = d != 0.0 ? 9.0 * Y / d : vn;
            if (!luv) {
                rgb[0] = float(u);
                rgb[1] = float(v);
                rgb[2] = float(Y);
                return;
            }
            double const L = encode(Curve::LStar, Y);
            rgb[0] = float(L);
            rgb[1] = float(13.0 * L * (u - un));
            rgb[2] = float(13.0 * L * (v - vn));
            return;
        }
        double u = X, v = Y, big = Z;
        if (luv) {
            double const L = X;
            big = decode(Curve::LStar, L);
            u = L != 0.0 ? Y / (13.0 * L) + un : un;
            v = L != 0.0 ? Z / (13.0 * L) + vn : vn;
        }
        rgb[0] = float(v != 0.0 ? big * 9.0 * u / (4.0 * v) : 0.0);
        rgb[1] = float(big);
        rgb[2] = float(v != 0.0 ? big * (12.0 - 3.0 * u - 20.0 * v) / (4.0 * v) : 0.0);
        return;
    }
    case FixedStyle::Rec2100Surround: {
        double const gamma = back ? 1.0 / op.a[0] : op.a[0];
        double const y = std::max(0.2627 * X + 0.6780 * Y + 0.0593 * Z, 1e-6);
        double const scale = std::pow(y, gamma - 1.0);
        for (int c = 0; c < 3; ++c)
            rgb[c] = float(rgb[c] * scale);
        return;
    }
    }
}

Matrix3 inverse3(Matrix3 const& m, bool* ok)
{
    double const det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    *ok = std::abs(det) > 1e-14;
    if (!*ok)
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double const k = 1.0 / det;
    return {(m[4] * m[8] - m[5] * m[7]) * k, (m[2] * m[7] - m[1] * m[8]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
            (m[5] * m[6] - m[3] * m[8]) * k, (m[0] * m[8] - m[2] * m[6]) * k, (m[2] * m[3] - m[0] * m[5]) * k,
            (m[3] * m[7] - m[4] * m[6]) * k, (m[1] * m[6] - m[0] * m[7]) * k, (m[0] * m[4] - m[1] * m[3]) * k};
}

bool sameSteps(Op const& a, Op const& b)
{
    return a.kind == b.kind && a.m == b.m && a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.e == b.e && a.f == b.f && a.base == b.base
           && a.style == b.style && a.curve == b.curve && a.inLow == b.inLow && a.inHigh == b.inHigh && a.table == b.table;
}

}  // namespace

namespace Cms {

bool Op::perChannel() const
{
    switch (kind) {
    case Matrix:
        return m[1] == 0 && m[2] == 0 && m[3] == 0 && m[5] == 0 && m[6] == 0 && m[7] == 0;
    case Lut3D:
    case Fixed:
        return false;
    case Cdl:
        return e[0] == 1.0;
    default:
        return true;
    }
}

bool Op::inverted(Op* out, QString* error) const
{
    *out = *this;
    switch (kind) {
    case Matrix: {
        bool ok = false;
        Matrix3 const m3{m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]};
        Matrix3 const inv = inverse3(m3, &ok);
        if (!ok) {
            if (error)
                *error = said("The matrix cannot be turned round (it flattens the colours).");
            return false;
        }
        for (int i = 0; i < 9; ++i)
            out->m[size_t(i)] = inv[size_t(i)];
        for (int r = 0; r < 3; ++r)
            out->m[size_t(9 + r)] = -(inv[size_t(r * 3)] * m[9] + inv[size_t(r * 3 + 1)] * m[10] + inv[size_t(r * 3 + 2)] * m[11]);
        out->inverse = false;
        return true;
    }
    case Lut3D:
        out->inverse = !inverse;
        if (inverse)
            return true;
        if (error)
            *error = said("A 3D LUT cannot be run backwards.");
        return false;
    default:
        out->inverse = !inverse;
        return true;
    }
}

void runOp(Op const& op, float* rgb)
{
    bool const back = op.inverse;
    switch (op.kind) {
    case Op::Matrix: {
        double const r = rgb[0], g = rgb[1], b = rgb[2];
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(op.m[size_t(i * 3)] * r + op.m[size_t(i * 3 + 1)] * g + op.m[size_t(i * 3 + 2)] * b + op.m[size_t(9 + i)]);
        return;
    }
    case Op::Exponent:
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(power(rgb[i], back ? 1.0 / op.a[size_t(i)] : op.a[size_t(i)], op.style));
        return;
    case Op::MonCurve:
        for (int i = 0; i < 3; ++i) {
            double const v = rgb[i], g = op.a[size_t(i)], o = op.b[size_t(i)];
            double const outV = op.style == 1 && v < 0.0 ? -(back ? monEncode(-v, g, o) : monDecode(-v, g, o)) : back ? monEncode(v, g, o) : monDecode(v, g, o);
            rgb[i] = float(outV);
        }
        return;
    case Op::Log:
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(back ? std::pow(op.base, double(rgb[i])) : logOf(rgb[i], op.base));
        return;
    case Op::LogAffine:
        for (int i = 0; i < 3; ++i) {
            size_t const k = size_t(i);
            rgb[i] = float(back ? (std::pow(op.base, (rgb[i] - op.b[k]) / op.a[k]) - op.d[k]) / op.c[k] : op.a[k] * logOf(op.c[k] * rgb[i] + op.d[k], op.base) + op.b[k]);
        }
        return;
    case Op::LogCamera:
        for (int i = 0; i < 3; ++i) {
            size_t const k = size_t(i);
            double slope = 1.0, offset = 0.0;
            cameraLine(op, i, &slope, &offset);
            double const v = rgb[i];
            if (!back)
                rgb[i] = float(v <= op.e[k] ? slope * v + offset : op.a[k] * logOf(op.c[k] * v + op.d[k], op.base) + op.b[k]);
            else
                rgb[i] = float(v <= slope * op.e[k] + offset ? (v - offset) / slope : (std::pow(op.base, (v - op.b[k]) / op.a[k]) - op.d[k]) / op.c[k]);
        }
        return;
    case Op::Named:
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(back ? encode(op.curve, rgb[i]) : decode(op.curve, rgb[i]));
        return;
    case Op::Lut1D:
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(back ? lut1dBack(op, i, rgb[i]) : lut1d(op, i, rgb[i]));
        return;
    case Op::Lut3D:
        lut3d(op, rgb);
        return;
    case Op::Range:
        for (int i = 0; i < 3; ++i) {
            double lo = op.inLow[0], hi = op.inHigh[0], olo = op.outLow, ohi = op.outHigh;
            if (back) {
                std::swap(lo, olo);
                std::swap(hi, ohi);
            }
            double v = rgb[i];
            if (op.style == 0)
                v = (v - lo) * (ohi - olo) / (hi != lo ? hi - lo : 1.0) + olo;
            else
                v = v - (op.style == 1 ? lo : hi) + (op.style == 1 ? olo : ohi);
            if (op.clampLow && op.style != 2)
                v = std::max(v, std::min(olo, ohi));
            if (op.clampHigh && op.style != 1)
                v = std::min(v, std::max(olo, ohi));
            rgb[i] = float(v);
        }
        return;
    case Op::Cdl: {
        bool const clampIt = op.style == 0;
        double v[3] = {rgb[0], rgb[1], rgb[2]};
        double const sat = op.e[0];
        auto luma = [&v] { return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]; };
        if (!back) {
            for (int i = 0; i < 3; ++i) {
                v[i] = v[i] * op.a[size_t(i)] + op.b[size_t(i)];
                if (clampIt)
                    v[i] = std::clamp(v[i], 0.0, 1.0);
                v[i] = v[i] > 0.0 ? std::pow(v[i], op.c[size_t(i)]) : v[i];
            }
            double const l = luma();
            for (double& x : v)
                x = clampIt ? std::clamp(l + sat * (x - l), 0.0, 1.0) : l + sat * (x - l);
        } else {
            double const l = luma();
            for (int i = 0; i < 3; ++i) {
                double x = sat != 0.0 ? l + (v[i] - l) / sat : l;
                if (clampIt)
                    x = std::clamp(x, 0.0, 1.0);
                x = x > 0.0 ? std::pow(x, 1.0 / op.c[size_t(i)]) : x;
                x = (x - op.b[size_t(i)]) / (op.a[size_t(i)] != 0.0 ? op.a[size_t(i)] : 1.0);
                v[i] = x;
            }
        }
        for (int i = 0; i < 3; ++i)
            rgb[i] = float(v[i]);
        return;
    }
    case Op::Fixed:
        fixed(op, rgb, back);
        return;
    case Op::Exposure:
        for (int i = 0; i < 3; ++i) {
            double const gain = std::pow(2.0, op.a[0]), slope = op.a[1] * op.a[2], pivot = std::max(op.b[0], kTiny);
            double v = rgb[i];
            if (op.style == 2)
                v = back ? (v - pivot - op.a[0] * 0.088) / (slope != 0 ? slope : 1) + pivot : (v - pivot) * slope + pivot + op.a[0] * 0.088;
            else if (!back)
                v = std::pow(std::max(v * gain / pivot, 0.0), slope) * pivot;
            else
                v = std::pow(std::max(v / pivot, 0.0), 1.0 / (slope != 0 ? slope : 1)) * pivot / gain;
            rgb[i] = float(v);
        }
        return;
    case Op::Allocation:
        for (int i = 0; i < 3; ++i) {
            double const lo = op.inLow[0], hi = op.inHigh[0], span = hi != lo ? hi - lo : 1.0;
            double v = rgb[i];
            if (op.style == 1)
                v = back ? std::pow(2.0, v * span + lo) - op.a[0] : (std::log2(std::max(v + op.a[0], kTiny)) - lo) / span;
            else
                v = back ? v * span + lo : (v - lo) / span;
            rgb[i] = float(v);
        }
        return;
    }
}

void run(Chain const& chain, float* rgb)
{
    for (Op const& op : chain)
        runOp(op, rgb);
}

bool invert(Chain const& chain, Chain* out, QString* error)
{
    Chain back;
    back.reserve(chain.size());
    for (qsizetype i = chain.size() - 1; i >= 0; --i) {
        Op turned;
        if (!chain[i].inverted(&turned, error))
            return false;
        back.push_back(turned);
    }
    *out = back;
    return true;
}

Chain simplified(Chain chain)
{
    Chain out;
    for (Op const& op : chain) {
        if (op.kind == Op::Matrix && !out.isEmpty() && out.back().kind == Op::Matrix) {
            // Two matrices in a row are one: the second times the first.
            Op& prev = out.back();
            std::array<double, 12> joined{};
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    double sum = 0.0;
                    for (int k = 0; k < 3; ++k)
                        sum += op.m[size_t(r * 3 + k)] * prev.m[size_t(k * 3 + c)];
                    joined[size_t(r * 3 + c)] = sum;
                }
                double off = op.m[size_t(9 + r)];
                for (int k = 0; k < 3; ++k)
                    off += op.m[size_t(r * 3 + k)] * prev.m[size_t(9 + k)];
                joined[size_t(9 + r)] = off;
            }
            prev.m = joined;
        } else if (!out.isEmpty() && op.kind != Op::Matrix && op.kind != Op::Lut3D && sameSteps(op, out.back()) && op.inverse != out.back().inverse) {
            // A step and its own undoing.
            out.pop_back();
            continue;
        } else {
            out.push_back(op);
        }
        if (out.back().kind == Op::Matrix) {
            std::array<double, 12> const one{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
            bool same = true;
            for (size_t i = 0; i < 12; ++i)
                same = same && std::abs(out.back().m[i] - one[i]) < 1e-9;
            if (same)
                out.pop_back();
        }
    }
    return out;
}

bool isIdentity(Chain const& chain)
{
    return simplified(chain).isEmpty();
}

Op matrixOp(Matrix3 const& m, std::array<double, 3> const& offset)
{
    Op op;
    op.kind = Op::Matrix;
    for (size_t i = 0; i < 9; ++i)
        op.m[i] = m[i];
    for (size_t i = 0; i < 3; ++i)
        op.m[9 + i] = offset[i];
    return op;
}

Op curveOp(Curve curve, bool encodeIt)
{
    Op op;
    op.kind = Op::Named;
    op.curve = curve;
    op.inverse = encodeIt;
    return op;
}

Op gammaOp(double gamma, bool encodeIt)
{
    Op op;
    op.kind = Op::Exponent;
    op.a = {gamma, gamma, gamma};
    op.style = 1;
    op.inverse = encodeIt;
    return op;
}

Op monCurveOp(double gamma, double offset, bool encodeIt)
{
    Op op;
    op.kind = Op::MonCurve;
    op.a = {gamma, gamma, gamma};
    op.b = {offset, offset, offset};
    op.inverse = encodeIt;
    return op;
}

Op rangeOp(double inLow, double inHigh, double outLow, double outHigh, bool clampIt)
{
    Op op;
    op.kind = Op::Range;
    op.inLow = {inLow, inLow, inLow};
    op.inHigh = {inHigh, inHigh, inHigh};
    op.outLow = outLow;
    op.outHigh = outHigh;
    op.clampLow = op.clampHigh = clampIt;
    return op;
}

}  // namespace Cms
