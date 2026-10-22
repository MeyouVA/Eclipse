// Eclipse - a line: its points and handles, its path, inserting points, corners, bounds, hit-testing.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"
#include "model/scene/StrokeShared.h"

using namespace stroke_shared;

void Stroke::handlesAt(int index, QPointF& in, QPointF& out) const
{
    in = out = QPointF();
    int const n = int(points.size());
    if (index < 0 || index >= n || !curve || n < 2)
        return;
    if (points[index].handles) {
        in = points[index].in;
        out = points[index].out;
        return;
    }
    bool const loop = closed && n >= 2;
    QPointF c1, c2;
    if (index + 1 < n || loop) {
        controls(*this, index, c1, c2);
        out = c1 - points[index].pos;
    }
    if (index > 0 || loop) {
        controls(*this, (index + n - 1) % n, c1, c2);
        in = c2 - points[index].pos;
    }
}

void Stroke::setHandles(int index, QPointF in, QPointF out)
{
    if (index < 0 || index >= points.size())
        return;
    if (!curve) {
        // A line of straight pieces: every other point stays the corner it was.
        for (StrokePoint& p : points)
            p.corner = true;
        curve = true;
    }
    points[index].handles = true;
    points[index].in = in;
    points[index].out = out;
}

QVector<StrokePoint> Stroke::path(QVector<int>* segmentOf) const
{
    QVector<double> places;
    QVector<StrokePoint> out = pathWithPlaces(places);
    if (segmentOf) {
        segmentOf->clear();
        int const last = std::max(0, pieces() - 1);
        for (double at : std::as_const(places))
            segmentOf->push_back(std::clamp(int(std::floor(at)), 0, last));
    }
    return out;
}

QVector<StrokePoint> Stroke::shapeWithPlaces(QVector<double>& places) const
{
    int const n = int(points.size());
    QVector<StrokePoint> out;
    places.clear();
    bool const profiled = !thickness.isEmpty();
    bool evenly = true;
    for (ThicknessPoint const& key : thickness)
        evenly = evenly && std::abs(key.left - thickness[0].left) < 1e-5f && std::abs(key.right - thickness[0].right) < 1e-5f;
    if (n < 2 || (!curve && !closed && (!profiled || evenly))) {
        out = points;
        for (int i = 0; i < n; ++i)
            places.push_back(i);
        return out;
    }
    int const spans = closed && n >= 2 ? n : n - 1;
    for (int s = 0; s < spans; ++s) {
        StrokePoint const& a = points[s];
        StrokePoint const& b = points[(s + 1) % n];
        int steps = 1;
        QPointF c1 = a.pos, c2 = b.pos;
        double length = QLineF(a.pos, b.pos).length();
        if (curve) {
            controls(*this, s, c1, c2);
            QLineF const first(a.pos, c1), middle(c1, c2), last(c2, b.pos);
            length = first.length() + middle.length() + last.length();
            auto turn = [](QLineF const& u, QLineF const& v) {
                if (u.length() < 1e-9 || v.length() < 1e-9)
                    return 0.0;
                double const angle = std::abs(u.angleTo(v));
                return angle > 180.0 ? 360.0 - angle : angle;
            };
            // (A handle of no length takes no part in the turning.)
            double const turning = first.length() < 1e-9   ? turn(middle, last)
                                   : last.length() < 1e-9  ? turn(first, middle)
                                   : middle.length() < 1e-9 ? turn(first, last)
                                                            : turn(first, middle) + turn(middle, last);
            steps = std::clamp(std::max(int(std::ceil(length / 2.0)), int(std::ceil(turning / 3.0))), 4, 96);
            // A piece whose handles lie along it is straight: two points say it all.
            if (turning < 0.01 && std::abs(length - QLineF(a.pos, b.pos).length()) < 1e-6)
                steps = 1;
        }
        QVector<double> at;
        for (int k = 0; k < steps; ++k)
            at.push_back(double(k) / steps);
        if (profiled && !evenly) {
            for (ThicknessPoint const& key : thickness)
                if (key.at > s && key.at < s + 1)
                    at.push_back(key.at - s);
            int const fine = std::clamp(int(std::ceil(length / 3.0)), 6, 64);
            for (int k = 1; k < fine; ++k)
                at.push_back(double(k) / fine);
            std::sort(at.begin(), at.end());
            at.erase(std::unique(at.begin(), at.end(), [](double x, double y) { return y - x < 1e-6; }), at.end());
        }
        for (double u : std::as_const(at)) {
            StrokePoint p;
            p.pos = !curve ? a.pos + (b.pos - a.pos) * u : (u == 0.0 ? a.pos : cubicAt(a.pos, c1, c2, b.pos, u));
            if (!profiled) {
                p.pressure = float(a.pressure + (b.pressure - a.pressure) * u);
                p.lean = float(a.lean + (b.lean - a.lean) * u);
            }
            p.opacity = float(a.opacity + (b.opacity - a.opacity) * u);
            out.push_back(p);
            places.push_back(s + u);
        }
    }
    StrokePoint end = points[spans % n];  // the far end: the last point, or the first again
    end.handles = false;
    out.push_back(end);
    places.push_back(spans);
    return out;
}

QVector<StrokePoint> Stroke::pathWithPlaces(QVector<double>& places) const
{
    QVector<StrokePoint> out = shapeWithPlaces(places);
    if (thickness.isEmpty() || out.isEmpty())
        return out;
    QVector<double> along(out.size(), 0.0);
    for (int i = 1; i < out.size(); ++i)
        along[i] = along[i - 1] + QLineF(out[i - 1].pos, out[i].pos).length();
    QVector<ThicknessPoint> const keys = keysByLength(places, along);
    bool const loop = closed && points.size() >= 2;
    for (int i = 0; i < out.size(); ++i) {
        ThicknessPoint const sides = sidesAlong(keys, along[i], loop, along.last());
        float const sum = sides.left + sides.right;
        out[i].pressure = sum / 2.0f;
        out[i].lean = sum > 1e-9f ? (sides.left - sides.right) / sum : 0.0f;
    }
    return out;
}

int Stroke::insertPoint(QPointF p)
{
    int const n = int(points.size());
    if (n < 2)
        return -1;
    int const s = segmentAt(p);
    StrokePoint const a = points[s];
    StrokePoint const b = points[(s + 1) % n];
    if (!curve) {
        // Straight pieces: the point goes on the piece, where it is nearest to `p`.
        QPointF const ab = b.pos - a.pos;
        double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
        double const t = len2 > 0 ? std::clamp(((p.x() - a.pos.x()) * ab.x() + (p.y() - a.pos.y()) * ab.y()) / len2, 0.0, 1.0) : 0.5;
        StrokePoint between{a.pos + ab * t, float(a.pressure + (b.pressure - a.pressure) * t)};
        between.lean = float(a.lean + (b.lean - a.lean) * t);
        between.opacity = float(a.opacity + (b.opacity - a.opacity) * t);
        if (!thickness.isEmpty()) {
            between.pressure = 1.0f;
            between.lean = 0.0f;
        }
        points.insert(s + 1, between);
        splitThickness(s, t);
        return s + 1;
    }
    // Where on the piece: the nearest of many places along it.
    QPointF c1, c2;
    controls(*this, s, c1, c2);
    double best = 0.5, bestDistance = 1e300;
    for (int k = 1; k < 200; ++k) {
        double const t = k / 200.0;
        QPointF const q = cubicAt(a.pos, c1, c2, b.pos, t);
        double const d = std::hypot(q.x() - p.x(), q.y() - p.y());
        if (d < bestDistance) {
            bestDistance = d;
            best = t;
        }
    }
    double const t = best;
    QPointF const ab = a.pos + (c1 - a.pos) * t, bc = c1 + (c2 - c1) * t, cd = c2 + (b.pos - c2) * t;
    QPointF const abc = ab + (bc - ab) * t, bcd = bc + (cd - bc) * t;
    QPointF const on = abc + (bcd - abc) * t;
    // The far handles of the two ends are as the curve had them.
    QPointF aIn, aOut, bIn, bOut;
    handlesAt(s, aIn, aOut);
    handlesAt((s + 1) % n, bIn, bOut);
    StrokePoint& first = points[s];
    first.handles = true;
    first.in = aIn;
    first.out = ab - a.pos;
    StrokePoint& second = points[(s + 1) % n];
    second.handles = true;
    second.in = cd - b.pos;
    second.out = bOut;
    StrokePoint fresh;
    fresh.pos = on;
    fresh.pressure = float(a.pressure + (b.pressure - a.pressure) * t);
    fresh.lean = float(a.lean + (b.lean - a.lean) * t);
    fresh.opacity = float(a.opacity + (b.opacity - a.opacity) * t);
    fresh.handles = true;
    fresh.in = abc - on;
    fresh.out = bcd - on;
    if (!thickness.isEmpty()) {
        fresh.pressure = 1.0f;
        fresh.lean = 0.0f;
    }
    points.insert(s + 1, fresh);
    splitThickness(s, t);
    return s + 1;
}

int Stroke::segmentAt(QPointF p) const
{
    QVector<int> segmentOf;
    QVector<StrokePoint> const line = path(&segmentOf);
    int best = 0;
    double bestDistance = 1e300;
    for (int i = 1; i < line.size(); ++i) {
        double const d = distanceToSegment(p, line[i - 1].pos, line[i].pos);
        if (d < bestDistance) {
            bestDistance = d;
            best = segmentOf[i - 1];
        }
    }
    return best;
}

void Stroke::splitThickness(int s, double t)
{
    // Piece `s` is two now, cut at `t`: the places on it are on one or the other, those after it a piece further on.
    for (ThicknessPoint& key : thickness) {
        if (key.at <= s)
            continue;
        if (key.at >= s + 1)
            key.at += 1.0;
        else {
            double const v = key.at - s;
            key.at = v <= t ? s + (t > 1e-12 ? v / t : 0.0) : s + 1 + (1.0 - t > 1e-12 ? (v - t) / (1.0 - t) : 0.0);
        }
    }
}

bool Stroke::uniform() const
{
    if (!thickness.isEmpty()) {
        for (ThicknessPoint const& key : thickness)
            if (std::abs(key.left - thickness[0].left) > 0.001f || std::abs(key.right - thickness[0].right) > 0.001f
                || std::abs(key.left - key.right) > 0.001f)
                return false;
        return true;
    }
    for (StrokePoint const& p : points)
        if (std::abs(p.pressure - points[0].pressure) > 0.001f || std::abs(p.lean) > 0.001f)
            return false;
    return true;
}

bool Stroke::leans() const
{
    for (ThicknessPoint const& key : thickness)
        if (std::abs(key.left - key.right) > 0.001f)
            return true;
    for (StrokePoint const& p : points)
        if (std::abs(p.lean) > 0.001f)
            return true;
    return false;
}

bool Stroke::fades() const
{
    for (StrokePoint const& p : points)
        if (p.opacity < 0.999f)
            return true;
    return false;
}

bool Stroke::isCorner(int index) const
{
    return index >= 0 && index < points.size() && (!curve || points[index].corner);
}

void Stroke::setCorner(int index, bool corner)
{
    if (index < 0 || index >= points.size())
        return;
    if (!curve) {
        for (StrokePoint& p : points)
            p.corner = true;
        curve = true;
    }
    points[index].corner = corner;
    points[index].handles = false;      // a corner or a smooth point as the curve makes it: no handles of its own
}

QRectF Stroke::bounds() const
{
    if (points.isEmpty())
        return QRectF();
    QVector<StrokePoint> const line = path();
    double l = line[0].pos.x(), r = l, t = line[0].pos.y(), b = t;
    for (StrokePoint const& p : line) {
        l = std::min(l, p.pos.x());
        r = std::max(r, p.pos.x());
        t = std::min(t, p.pos.y());
        b = std::max(b, p.pos.y());
    }
    // (As far out as the thickness reaches at its thickest, to either side.)
    double most = 1.0;
    for (StrokePoint const& p : line)
        most = std::max(most, double(p.pressure) * (1.0 + std::abs(double(p.lean))));
    double const half = width / 2.0 * most;
    QRectF box(l - half, t - half, r - l + 2 * half, b - t + 2 * half);
    // A mitre reaches further out than half the thickness, at its corners: where its points are.
    if (join == MitreJoin)
        for (int i = 1; i + 1 < line.size(); ++i) {
            QPointF const a = line[i].pos - line[i - 1].pos, c = line[i + 1].pos - line[i].pos;
            double const la = std::hypot(a.x(), a.y()), lc = std::hypot(c.x(), c.y());
            if (la < 1e-9 || lc < 1e-9)
                continue;
            double const cosine = (a.x() * c.x() + a.y() * c.y()) / (la * lc);
            double const along = std::sqrt(std::max(0.0, (1.0 + cosine) / 2.0));     // the cosine of half the turn
            double const reach = std::min(half / std::max(along, 1e-9), kMitreLimit * half);
            if (reach > half)
                box = box.united(QRectF(line[i].pos - QPointF(reach, reach), QSizeF(2 * reach, 2 * reach)));
        }
    return box;
}

double distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    QPointF const ab = b - a;
    double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    QPointF const q = a + t * ab;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

bool Stroke::hit(QPointF p, double radius) const
{
    if (points.isEmpty() || !bounds().adjusted(-radius, -radius, radius, radius).contains(p))
        return false;
    double const reach = radius + width / 2.0;
    if (points.size() == 1)
        return std::hypot(p.x() - points[0].pos.x(), p.y() - points[0].pos.y()) <= reach;
    if (width > 0.0f) {
        QVector<QPolygonF> const area = outlinePolygons();
        if (!area.isEmpty()) {
            QPainterPath shape;
            shape.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& outline : area)
                shape.addPolygon(outline);
            if (shape.contains(p))
                return true;
            for (QPolygonF const& outline : area)
                for (int i = 0; i < outline.size(); ++i)
                    if (distanceToSegment(p, outline[i], outline[(i + 1) % outline.size()]) <= radius)
                        return true;
            return false;
        }
    }
    QVector<StrokePoint> const line = path();
    for (int i = 1; i < line.size(); ++i) {
        if (distanceToSegment(p, line[i - 1].pos, line[i].pos) <= reach)
            return true;
    }
    return false;
}

void Stroke::transform(QTransform const& t)
{
    for (StrokePoint& p : points) {
        if (p.handles) {
            // Mapped as the places they reach to, so that a perspective pull is followed too.
            QPointF const in = t.map(p.pos + p.in), out = t.map(p.pos + p.out), at = t.map(p.pos);
            p.in = in - at;
            p.out = out - at;
        }
        p.pos = t.map(p.pos);
    }
    if (gradPlaced) {
        gradFrom = t.map(gradFrom);
        gradTo = t.map(gradTo);
    }
    // Keep the line weight in proportion to the shape.
    width = float(width * std::sqrt(std::abs(t.determinant())));
}
