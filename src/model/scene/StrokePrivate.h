// Eclipse - what the parts of a line (Stroke*.cpp) have in common and nobody else needs.
#pragma once
#include <QPointF>
#include <QVector>

#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"
#include <QBuffer>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPainterPath>
#include <QHash>
#include <QFileInfo>
#include <QDir>
#include "model/geometry/Deform.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <cstdio>

inline QPointF cubicAt(QPointF p0, QPointF c1, QPointF c2, QPointF p3, double t)
{
    double const u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * c1 + 3.0 * u * t * t * c2 + t * t * t * p3;
}

//
// The samples are cut where the pen turned a corner; each run between two corners is then
// fitted with cubic Bezier pieces by least squares: one piece is tried, and where it strays
// further from the samples than the tolerance the run is cut in two at the worst place and
// each half fitted again, the two halves sharing their direction at the cut so that the
// line is smooth through it.

namespace {

struct Piece {
    int first = 0, last = 0;    // the samples it runs between
    QPointF c1, c2;             // its two control points
};

double dot(QPointF a, QPointF b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double length(QPointF v)
{
    return std::hypot(v.x(), v.y());
}

QPointF unit(QPointF v)
{
    double const l = length(v);
    return l > 1e-12 ? v / l : QPointF();
}

// The direction the line takes from sample `from` towards `to`, judged over a little
// distance rather than from the very next sample (which jitters).
QPointF directionFrom(QVector<StrokePoint> const& d, int from, int to, double reach)
{
    int const step = to > from ? 1 : -1;
    for (int i = from + step; i != to + step; i += step)
        if (length(d[i].pos - d[from].pos) >= reach || i == to)
            return unit(d[i].pos - d[from].pos);
    return QPointF();
}

// The direction the line has as it passes sample `at`, judged between the samples a little
// way off on either side of it (no further than `low` and `high`).
QPointF directionThrough(QVector<StrokePoint> const& d, int at, int low, int high, double reach)
{
    int before = at, after = at;
    while (before > low && length(d[before].pos - d[at].pos) < reach)
        --before;
    while (after < high && length(d[after].pos - d[at].pos) < reach)
        ++after;
    return unit(d[after].pos - d[before].pos);
}

// One cubic through the two ends of the run with the given directions there, as close to
// the samples between as its two handle lengths allow.
Piece shape(QVector<StrokePoint> const& d, int first, int last, QVector<double> const& u, QPointF t1, QPointF t2)
{
    QPointF const p0 = d[first].pos, p3 = d[last].pos;
    double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
    for (int i = first; i <= last; ++i) {
        double const t = u[i - first], v = 1.0 - t;
        double const b0 = v * v * v, b1 = 3 * t * v * v, b2 = 3 * t * t * v, b3 = t * t * t;
        QPointF const a1 = t1 * b1, a2 = t2 * b2;
        c00 += dot(a1, a1);
        c01 += dot(a1, a2);
        c11 += dot(a2, a2);
        QPointF const rest = d[i].pos - (p0 * (b0 + b1) + p3 * (b2 + b3));
        x0 += dot(a1, rest);
        x1 += dot(a2, rest);
    }
    double const det = c00 * c11 - c01 * c01;
    double const chord = length(p3 - p0);
    double alpha1 = std::abs(det) > 1e-12 ? (x0 * c11 - x1 * c01) / det : 0.0;
    double alpha2 = std::abs(det) > 1e-12 ? (c00 * x1 - c01 * x0) / det : 0.0;
    // Handles that came out backwards, or absurdly long, are no use: a third of the way each.
    if (alpha1 < chord * 1e-6 || alpha2 < chord * 1e-6 || alpha1 > chord * 4 || alpha2 > chord * 4)
        alpha1 = alpha2 = chord / 3.0;
    Piece piece;
    piece.first = first;
    piece.last = last;
    piece.c1 = p0 + t1 * alpha1;
    piece.c2 = p3 + t2 * alpha2;
    return piece;
}

// How far along the run each sample is, 0 to 1, by the length of line before it.
QVector<double> byLength(QVector<StrokePoint> const& d, int first, int last)
{
    QVector<double> u(last - first + 1, 0.0);
    for (int i = first + 1; i <= last; ++i)
        u[i - first] = u[i - first - 1] + length(d[i].pos - d[i - 1].pos);
    double const total = u.last();
    for (double& v : u)
        v = total > 0 ? v / total : 0.0;
    return u;
}

// The furthest any sample is from the piece - from the curve itself, wherever on it is
// nearest, not from a place guessed for the sample - and which sample that is.  Each
// sample is also told where along the piece (0 to 1) it was found nearest, which is the
// place to give it when the piece is shaped again.
double worst(QVector<StrokePoint> const& d, Piece const& piece, QVector<double>& u, int& at)
{
    QPointF const p0 = d[piece.first].pos, p3 = d[piece.last].pos;
    int const samples = piece.last - piece.first + 1;
    // The piece as a line of short straight bits.
    int const bits = std::clamp(samples * 2, 24, 480);
    QVector<QPointF> line(bits + 1);
    for (int k = 0; k <= bits; ++k)
        line[k] = cubicAt(p0, piece.c1, piece.c2, p3, double(k) / bits);
    // Every bit is looked at when that is not much work; else only those about where the
    // sample was last placed.
    bool const all = double(samples) * bits <= 600000.0;
    double most = 0.0;
    at = (piece.first + piece.last) / 2;
    for (int i = piece.first + 1; i < piece.last; ++i) {
        int low = 0, high = bits - 1;
        if (!all) {
            int const about = int(u[i - piece.first] * bits);
            low = std::max(0, about - bits / 6);
            high = std::min(bits - 1, about + bits / 6);
        }
        double nearest = 1e300, where = u[i - piece.first];
        for (int k = low; k <= high; ++k) {
            QPointF const a = line[k], ab = line[k + 1] - a;
            double const len2 = dot(ab, ab);
            double const t = len2 > 0 ? std::clamp(dot(d[i].pos - a, ab) / len2, 0.0, 1.0) : 0.0;
            double const distance = length(d[i].pos - (a + ab * t));
            if (distance < nearest) {
                nearest = distance;
                where = (k + t) / bits;
            }
        }
        u[i - piece.first] = where;
        if (nearest > most) {
            most = nearest;
            at = i;
        }
    }
    return most;
}

void fitRun(QVector<StrokePoint> const& d, int first, int last, QPointF t1, QPointF t2, double tolerance, double reach, QVector<Piece>& out, int depth)
{
    QPointF const p0 = d[first].pos, p3 = d[last].pos;
    if (last - first < 2 || depth > 24) {
        // Nothing between to fit: a straight piece, its handles a third of the way along it.
        Piece piece;
        piece.first = first;
        piece.last = last;
        double const third = length(p3 - p0) / 3.0;
        piece.c1 = p0 + t1 * third;
        piece.c2 = p3 + t2 * third;
        out.push_back(piece);
        return;
    }
    QVector<double> u = byLength(d, first, last);
    Piece piece = shape(d, first, last, u, t1, t2);
    int split = 0;
    double error = worst(d, piece, u, split);
    // The samples were placed along the piece by guess (by the length of line before each);
    // measuring has put each where it really is nearest the curve.  Shaped again with those
    // places the piece often comes within the tolerance - above all on a shaky line.
    for (int round = 0; round < 4 && error > tolerance; ++round) {
        Piece const again = shape(d, first, last, u, t1, t2);
        int where = 0;
        QVector<double> placed = u;
        double const now = worst(d, again, placed, where);
        if (now >= error)
            break;      // no better: it will not get there this way
        piece = again;
        u = placed;
        error = now;
        split = where;
    }
    if (error <= tolerance) {
        out.push_back(piece);
        return;
    }
    // Cut in two where it is worst - but not out towards an end of the run: a cut is kept
    // within the middle part of it (by length of line), which spreads the points evenly
    // along a long bend instead of crowding them at its ends.  The line goes straight
    // through the cut.
    {
        QVector<double> const along = byLength(d, first, last);
        int low = first + 1, high = last - 1;
        while (low < last - 1 && along[low - first] < 0.3)
            ++low;
        while (high > first + 1 && along[high - first] > 0.7)
            --high;
        if (low <= high)
            split = std::clamp(split, low, high);
    }
    split = std::clamp(split, first + 1, last - 1);
    QPointF through = directionThrough(d, split, first, last, reach);
    if (through.isNull())
        through = unit(p3 - p0);
    fitRun(d, first, split, t1, -through, tolerance, reach, out, depth + 1);
    fitRun(d, split, last, through, t2, tolerance, reach, out, depth + 1);
}

}  // namespace

// A line whose thickness leans to one side: each place moved that way by its lean times half the thickness (the
// thickness then lies about the moved place as usual - so at a lean of 1 all of it is on the one side of the line as
// drawn).  Done on the places of the line before its edges are found.
inline void leanAside(QVector<QPointF>& pos, QVector<double> const& half, QVector<double> const& lean, bool closed)
{
    int const n = int(pos.size());
    if (n < 2)
        return;
    bool any = false;
    for (double l : lean)
        any = any || std::abs(l) > 1e-6;
    if (!any)
        return;
    QVector<QPointF> const was = pos;
    for (int i = 0; i < n; ++i) {
        QPointF const before = i > 0 ? was[i - 1] : (closed ? was[n - 2 >= 0 ? n - 2 : 0] : was[i]);
        QPointF const after = i + 1 < n ? was[i + 1] : (closed ? was[1] : was[i]);
        QPointF way = after - before;
        double length = std::hypot(way.x(), way.y());
        if (length < 1e-9)
            continue;
        QPointF const left(way.y() / length, -way.x() / length);     // to the left as the line goes (y grows downwards)
        pos[i] = was[i] + left * (half[i] * lean[i]);
    }
}
