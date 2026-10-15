// Eclipse - the scene: a line - its curve through its points, its handles, how near a point is to it.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"

// The two control points of the piece of a curve from point `s` to the next: from the
// handles the points were given, or - for a point without - from the smooth curve through
// the points on either side (a centripetal Catmull-Rom spline, which is a cubic too).
static void controls(Stroke const& stroke, int s, QPointF& c1, QPointF& c2)
{
    QVector<StrokePoint> const& points = stroke.points;
    int const n = int(points.size());
    bool const loop = stroke.closed && n > 2;
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
    bool const loop = closed && n > 2;
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
    int const n = int(points.size());
    QVector<StrokePoint> out;
    if (segmentOf)
        segmentOf->clear();
    if (n < 2 || (!curve && !closed)) {
        out = points;
        if (segmentOf)
            for (int i = 0; i < n; ++i)
                segmentOf->push_back(std::min(i, std::max(0, n - 2)));
        return out;
    }
    int const spans = closed && n > 2 ? n : n - 1;
    for (int s = 0; s < spans; ++s) {
        StrokePoint const& a = points[s];
        StrokePoint const& b = points[(s + 1) % n];
        int steps = 1;
        QPointF c1, c2;
        if (curve) {
            controls(*this, s, c1, c2);
            // Enough pieces that none is long and none turns far: a short span round a
            // tight bend needs as many as a long gentle one, or the bend shows as corners
            // when looked at closely.
            QLineF const first(a.pos, c1), middle(c1, c2), last(c2, b.pos);
            double const length = first.length() + middle.length() + last.length();
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
        for (int k = 0; k < steps; ++k) {
            double const u = double(k) / steps;
            StrokePoint p;
            p.pos = steps == 1 ? a.pos : cubicAt(a.pos, c1, c2, b.pos, u);
            p.pressure = float(a.pressure + (b.pressure - a.pressure) * u);
            out.push_back(p);
            if (segmentOf)
                segmentOf->push_back(s);
        }
    }
    StrokePoint end = points[spans % n];  // the far end: the last point, or the first again
    end.handles = false;
    out.push_back(end);
    if (segmentOf)
        segmentOf->push_back(spans - 1);
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
        points.insert(s + 1, {a.pos + ab * t, float(a.pressure + (b.pressure - a.pressure) * t)});
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
    // The piece cut in two there (de Casteljau): each half is a Bezier curve of its own,
    // and together they are the piece as it was.
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
    fresh.handles = true;
    fresh.in = abc - on;
    fresh.out = bcd - on;
    points.insert(s + 1, fresh);
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

bool Stroke::uniform() const
{
    for (StrokePoint const& p : points)
        if (std::abs(p.pressure - points[0].pressure) > 0.001f)
            return false;
    return true;
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
    double const half = width / 2.0;
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
        // On the area the line really covers (its tips, its corners, its thickness point by point), or within
        // `radius` of its edge.
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
