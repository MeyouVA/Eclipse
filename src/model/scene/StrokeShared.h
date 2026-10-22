// Eclipse - a line: what its files share.
#pragma once

#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"

namespace stroke_shared {

inline void controls(Stroke const& stroke, int s, QPointF& c1, QPointF& c2)
{
    QVector<StrokePoint> const& points = stroke.points;
    int const n = int(points.size());
    bool const loop = stroke.closed && n >= 2;
    StrokePoint const& a = points[s];
    StrokePoint const& b = points[(s + 1) % n];
    // The neighbour beyond an end: round the loop when closed, else the end mirrored.
    auto at = [&](int i) -> QPointF {
        if (loop)
            return points[((i % n) + n) % n].pos;
        if (i < 0)
            return points[0].pos * 2.0 - points[1].pos;
        if (i >= n)
            return points[n - 1].pos * 2.0 - points[n - 2].pos;
        return points[i].pos;
    };
    // At a corner the curve does not look past the point: it leaves as if the line ended there.
    QPointF const p0 = a.corner ? a.pos * 2.0 - b.pos : at(s - 1);
    QPointF const p1 = a.pos, p2 = b.pos;
    QPointF const p3 = b.corner ? b.pos * 2.0 - a.pos : at(s + 2);
    auto knot = [](QPointF u, QPointF v) { return std::max(1e-4, std::sqrt(std::hypot(v.x() - u.x(), v.y() - u.y()))); };
    double const d01 = knot(p0, p1), d12 = knot(p1, p2), d23 = knot(p2, p3);
    // The speed of the spline where it leaves p1 and where it arrives at p2.
    QPointF const m1 = d12 * ((p1 - p0) / d01 - (p2 - p0) / (d01 + d12) + (p2 - p1) / d12);
    QPointF const m2 = d12 * ((p2 - p1) / d12 - (p3 - p1) / (d12 + d23) + (p3 - p2) / d23);
    c1 = a.handles ? a.pos + a.out : p1 + m1 / 3.0;
    c2 = b.handles ? b.pos + b.in : p2 - m2 / 3.0;
}


// The line as drawn, with how far along it each place is.
struct Lengths {
    QVector<double> places, along;
    double total = 0.0;
};

inline Lengths lengthsOf(Stroke const& line)
{
    Lengths l;
    QVector<StrokePoint> const run = line.pathWithPlaces(l.places);
    l.along.resize(run.size());
    for (int i = 0; i < run.size(); ++i)
        l.along[i] = i == 0 ? 0.0 : l.along[i - 1] + QLineF(run[i - 1].pos, run[i].pos).length();
    l.total = l.along.isEmpty() ? 0.0 : l.along.last();
    return l;
}

inline double lengthAtPlace(Lengths const& l, double at)
{
    if (l.places.isEmpty())
        return 0.0;
    if (at <= l.places.first())
        return 0.0;
    for (int i = 1; i < l.places.size(); ++i)
        if (at <= l.places[i]) {
            double const span = l.places[i] - l.places[i - 1];
            double const t = span > 1e-12 ? (at - l.places[i - 1]) / span : 0.0;
            return l.along[i - 1] + (l.along[i] - l.along[i - 1]) * t;
        }
    return l.total;
}

inline double placeAtLength(Lengths const& l, double length)
{
    if (l.places.isEmpty())
        return 0.0;
    if (length <= 0.0)
        return l.places.first();
    for (int i = 1; i < l.along.size(); ++i)
        if (length <= l.along[i]) {
            double const span = l.along[i] - l.along[i - 1];
            double const t = span > 1e-12 ? (length - l.along[i - 1]) / span : 0.0;
            return l.places[i - 1] + (l.places[i] - l.places[i - 1]) * t;
        }
    return l.places.last();
}


}  // namespace stroke_shared
