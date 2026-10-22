// Eclipse - the scene: a line - its curve through its points, its handles, how near a point is to it.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"

static void controls(Stroke const& stroke, int s, QPointF& c1, QPointF& c2)
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

// ---- the thickness of a pencil line, apart from its points ------------------------------------------------------------

int Stroke::pieces() const
{
    int const n = int(points.size());
    return n < 2 ? 0 : (closed && n >= 2 ? n : n - 1);
}

namespace {

double smoothBetween(QVector<ThicknessPoint> const& keys, int j, double at, float ThicknessPoint::*side)
{
    auto slope = [&](int k) {
        if (k <= 0 || k >= keys.size() - 1)
            return 0.0;
        double const h0 = keys[k].at - keys[k - 1].at, h1 = keys[k + 1].at - keys[k].at;
        if (h0 <= 1e-12 || h1 <= 1e-12)
            return 0.0;
        double const d0 = (keys[k].*side - keys[k - 1].*side) / h0, d1 = (keys[k + 1].*side - keys[k].*side) / h1;
        if (d0 * d1 <= 0.0)
            return 0.0;
        return 2.0 / (1.0 / d0 + 1.0 / d1);
    };
    double const x0 = keys[j].at, x1 = keys[j + 1].at, h = x1 - x0;
    double const y0 = keys[j].*side, y1 = keys[j + 1].*side;
    if (h <= 1e-12)
        return y1;
    double const t = std::clamp((at - x0) / h, 0.0, 1.0), t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * slope(j) + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * h * slope(j + 1);
}

} // namespace

ThicknessPoint Stroke::sidesAt(double at) const
{
    int const n = int(points.size());
    ThicknessPoint result;
    result.at = at;
    if (thickness.isEmpty()) {
        // As the points say, from one to the next.
        if (n == 0)
            return result;
        int const span = std::max(1, pieces());
        double const place = closed && n >= 2 ? std::fmod(std::fmod(at, span) + span, span) : std::clamp(at, 0.0, double(n - 1));
        int const i = std::clamp(int(std::floor(place)), 0, n - 1);
        StrokePoint const& a = points[i];
        StrokePoint const& b = points[(i + 1) % n];
        double const t = n > 1 ? place - i : 0.0;
        double const pressure = a.pressure + (b.pressure - a.pressure) * t;
        double const lean = a.lean + (b.lean - a.lean) * t;
        result.left = float(pressure * (1.0 + lean));
        result.right = float(pressure * (1.0 - lean));
        return result;
    }
    // By how far along the line: the shape, and the places of the thickness on it.
    bool const loop = closed && n >= 2;
    QVector<double> places;
    QVector<StrokePoint> const shape = shapeWithPlaces(places);
    QVector<double> along(shape.size(), 0.0);
    for (int i = 1; i < shape.size(); ++i)
        along[i] = along[i - 1] + QLineF(shape[i - 1].pos, shape[i].pos).length();
    double const span = pieces();
    double place = loop && span > 0.0 ? std::fmod(std::fmod(at, span) + span, span) : std::clamp(at, 0.0, span);
    double length = 0.0;
    for (int i = 1; i < places.size(); ++i)
        if (place <= places[i] || i + 1 == places.size()) {
            double const step = places[i] - places[i - 1];
            double const t = step > 1e-12 ? std::clamp((place - places[i - 1]) / step, 0.0, 1.0) : 0.0;
            length = along[i - 1] + (along[i] - along[i - 1]) * t;
            break;
        }
    ThicknessPoint found = sidesAlong(keysByLength(places, along), length, loop, along.isEmpty() ? 0.0 : along.last());
    found.at = at;
    return found;
}

QVector<ThicknessPoint> Stroke::keysByLength(QVector<double> const& places, QVector<double> const& along) const
{
    QVector<ThicknessPoint> keys = thickness;
    for (ThicknessPoint& key : keys) {
        double length = along.isEmpty() ? 0.0 : along.last();
        for (int i = 1; i < places.size(); ++i)
            if (key.at <= places[i]) {
                double const step = places[i] - places[i - 1];
                double const t = step > 1e-12 ? std::clamp((key.at - places[i - 1]) / step, 0.0, 1.0) : 0.0;
                length = along[i - 1] + (along[i] - along[i - 1]) * t;
                break;
            }
        if (key.at <= 0.0)
            length = 0.0;
        key.at = length;
    }
    return keys;
}

ThicknessPoint Stroke::sidesAlong(QVector<ThicknessPoint> keys, double length, bool loop, double total)
{
    ThicknessPoint result;
    result.at = length;
    if (keys.isEmpty())
        return result;
    if (loop && total > 0.0) {
        // Round the loop: the last place leads on to the first.
        length = std::fmod(std::fmod(length, total) + total, total);
        ThicknessPoint before = keys.last(), after = keys.first();
        before.at -= total;
        after.at += total;
        keys.prepend(before);
        keys.append(after);
    }
    if (keys.size() == 1 || length <= keys.first().at) {
        result.left = keys.first().left;
        result.right = keys.first().right;
        return result;
    }
    if (length >= keys.last().at) {
        result.left = keys.last().left;
        result.right = keys.last().right;
        return result;
    }
    int j = 0;
    while (j + 2 < keys.size() && keys[j + 1].at <= length)
        ++j;
    result.left = float(std::max(0.0, smoothBetween(keys, j, length, &ThicknessPoint::left)));
    result.right = float(std::max(0.0, smoothBetween(keys, j, length, &ThicknessPoint::right)));
    return result;
}

void Stroke::thicknessAt(double at, float& pressure, float& lean) const
{
    ThicknessPoint const sides = sidesAt(at);
    float const sum = sides.left + sides.right;
    pressure = sum / 2.0f;
    lean = sum > 1e-9f ? (sides.left - sides.right) / sum : 0.0f;
}

float Stroke::pressureAt(double at) const
{
    float pressure = 1.0f, lean = 0.0f;
    thicknessAt(at, pressure, lean);
    return pressure;
}

void Stroke::ownThickness()
{
    if (!thickness.isEmpty() || points.isEmpty())
        return;
    for (int i = 0; i < points.size(); ++i) {
        StrokePoint& p = points[i];
        thickness.push_back({double(i), float(p.pressure * (1.0 + p.lean)), float(p.pressure * (1.0 - p.lean))});
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
}

namespace {

// The line as drawn, with how far along it each place is.
struct Lengths {
    QVector<double> places, along;
    double total = 0.0;
};

Lengths lengthsOf(Stroke const& line)
{
    Lengths l;
    QVector<StrokePoint> const run = line.pathWithPlaces(l.places);
    l.along.resize(run.size());
    for (int i = 0; i < run.size(); ++i)
        l.along[i] = i == 0 ? 0.0 : l.along[i - 1] + QLineF(run[i - 1].pos, run[i].pos).length();
    l.total = l.along.isEmpty() ? 0.0 : l.along.last();
    return l;
}

double lengthAtPlace(Lengths const& l, double at)
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

double placeAtLength(Lengths const& l, double length)
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

} // namespace

QVector<QPair<double, ThicknessPoint>> Stroke::thicknessByLength() const
{
    Stroke owned = *this;
    owned.ownThickness();
    Lengths const l = lengthsOf(owned);
    QVector<QPair<double, ThicknessPoint>> out;
    for (ThicknessPoint const& key : std::as_const(owned.thickness))
        out.push_back({l.total > 0.0 ? lengthAtPlace(l, key.at) / l.total : 0.0, key});
    return out;
}

void Stroke::setThicknessByLength(QVector<QPair<double, ThicknessPoint>> const& byLength)
{
    for (StrokePoint& p : points) {
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
    thickness.clear();
    Lengths const l = lengthsOf(*this);
    for (auto const& [fraction, key] : byLength) {
        ThicknessPoint placed = key;
        placed.at = placeAtLength(l, std::clamp(fraction, 0.0, 1.0) * l.total);
        thickness.push_back(placed);
    }
    std::stable_sort(thickness.begin(), thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
    // Two places at one: the later one goes.
    for (int i = int(thickness.size()) - 1; i > 0; --i)
        if (thickness[i].at - thickness[i - 1].at < 1e-9)
            thickness.removeAt(i);
}

void Stroke::takeThicknessFrom(QVector<StrokePoint> const& drawn)
{
    if (drawn.isEmpty())
        return;
    bool even = true;
    for (StrokePoint const& p : drawn)
        even = even && std::abs(p.pressure - drawn.first().pressure) < 1e-3f && std::abs(p.lean - drawn.first().lean) < 1e-3f;
    if (even) {
        // As thick all along: the points say it.
        thickness.clear();
        for (StrokePoint& p : points) {
            p.pressure = drawn.first().pressure;
            p.lean = drawn.first().lean;
        }
        return;
    }
    QVector<double> along(drawn.size(), 0.0);
    for (int i = 1; i < drawn.size(); ++i)
        along[i] = along[i - 1] + QLineF(drawn[i - 1].pos, drawn[i].pos).length();
    double const total = along.last();
    QVector<QPair<double, ThicknessPoint>> byLength;
    for (int i = 0; i < drawn.size(); ++i) {
        StrokePoint const& p = drawn[i];
        byLength.push_back({total > 0.0 ? along[i] / total : 0.0, {0.0, float(p.pressure * (1.0 + p.lean)), float(p.pressure * (1.0 - p.lean))}});
    }
    setThicknessByLength(byLength);
    simplifyThickness(0.01);
}

void Stroke::simplifyThickness(double tolerance)
{
    if (thickness.size() < 2)
        return;
    // Many places: first those a straight run between their neighbours says (Douglas and Peucker's way).
    if (thickness.size() > 48) {
        QVector<bool> keep(thickness.size(), false);
        keep.first() = keep.last() = true;       // (and the ends always)
        QVector<QPair<int, int>> todo{{0, int(thickness.size()) - 1}};
        while (!todo.isEmpty()) {
            auto const [a, b] = todo.takeLast();
            double worst = -1.0;
            int at = -1;
            for (int k = a + 1; k < b; ++k) {
                double const span = thickness[b].at - thickness[a].at;
                double const t = span > 1e-12 ? (thickness[k].at - thickness[a].at) / span : 0.0;
                double const off = std::max(std::abs(thickness[a].left + (thickness[b].left - thickness[a].left) * t - thickness[k].left),
                                            std::abs(thickness[a].right + (thickness[b].right - thickness[a].right) * t - thickness[k].right));
                if (off > worst) {
                    worst = off;
                    at = k;
                }
            }
            if (at >= 0 && worst > tolerance * 0.5) {
                keep[at] = true;
                todo.push_back({a, at});
                todo.push_back({at, b});
            }
        }
        QVector<ThicknessPoint> kept;
        for (int k = 0; k < thickness.size(); ++k)
            if (keep[k])
                kept.push_back(thickness[k]);
        thickness = kept;
    }
    QVector<double> places;
    QVector<StrokePoint> const shape = shapeWithPlaces(places);
    QVector<double> along(shape.size(), 0.0);
    for (int i = 1; i < shape.size(); ++i)
        along[i] = along[i - 1] + QLineF(shape[i - 1].pos, shape[i].pos).length();
    double const total = along.isEmpty() ? 0.0 : along.last();
    QVector<ThicknessPoint> byLength = keysByLength(places, along);
    for (;;) {
        if (thickness.size() < 2)
            break;
        int best = -1;
        double bestOff = tolerance;
        bool const loop = closed && points.size() >= 2;
        for (int k = 0; k < thickness.size(); ++k) {
            if (!loop && (k == 0 || k + 1 == thickness.size()))
                continue;       // the first and the last stay: what is said of one place is not then said of the whole line
            QVector<ThicknessPoint> without = byLength;
            ThicknessPoint const gone = without.takeAt(k);
            ThicknessPoint const there = sidesAlong(without, gone.at, loop, total);
            double const off = std::max(std::abs(there.left - gone.left), std::abs(there.right - gone.right));
            if (off <= bestOff) {
                bestOff = off;
                best = k;
            }
        }
        if (best < 0)
            break;
        thickness.removeAt(best);
        byLength.removeAt(best);
    }
}

int Stroke::thicknessPointAt(double at, double near)
{
    ownThickness();
    for (int k = 0; k < thickness.size(); ++k)
        if (std::abs(thickness[k].at - at) <= near)
            return k;
    ThicknessPoint key = sidesAt(at);
    key.at = at;
    int k = 0;
    while (k < thickness.size() && thickness[k].at < at)
        ++k;
    thickness.insert(k, key);
    return k;
}

namespace {

// The piece of the curve from point `s` and its two control points.
void pieceOf(Stroke const& line, int s, QPointF& a, QPointF& c1, QPointF& c2, QPointF& b)
{
    int const n = int(line.points.size());
    a = line.points[s].pos;
    b = line.points[(s + 1) % n].pos;
    if (line.curve) {
        controls(line, s, c1, c2);
    } else {
        c1 = a + (b - a) / 3.0;
        c2 = a + (b - a) * (2.0 / 3.0);
    }
}

} // namespace

QPointF Stroke::pointAt(double at) const
{
    int const n = int(points.size());
    if (n == 0)
        return QPointF();
    int const span = pieces();
    if (span == 0)
        return points.first().pos;
    double const place = std::clamp(at, 0.0, double(span));
    int const s = std::min(int(std::floor(place)), span - 1);
    QPointF a, c1, c2, b;
    pieceOf(*this, s, a, c1, c2, b);
    return cubicAt(a, c1, c2, b, place - s);
}

QPointF Stroke::directionAt(double at) const
{
    double const e = 1e-3;
    double const span = pieces();
    QPointF const way = pointAt(std::min(span, at + e)) - pointAt(std::max(0.0, at - e));
    double const length = std::hypot(way.x(), way.y());
    return length > 1e-12 ? way / length : QPointF();
}

double Stroke::placeAt(QPointF p) const
{
    int const span = pieces();
    double best = 0.0, bestDistance = 1e300;
    for (int s = 0; s < span; ++s) {
        QPointF a, c1, c2, b;
        pieceOf(*this, s, a, c1, c2, b);
        for (int k = 0; k <= 64; ++k) {
            double const t = k / 64.0;
            double const d = QLineF(cubicAt(a, c1, c2, b, t), p).length();
            if (d < bestDistance) {
                bestDistance = d;
                best = s + t;
            }
        }
    }
    // Closer, about the best of those.
    int const s = std::clamp(int(std::floor(best)), 0, std::max(0, span - 1));
    if (span > 0) {
        QPointF a, c1, c2, b;
        pieceOf(*this, s, a, c1, c2, b);
        double const from = std::max(0.0, best - s - 1.0 / 64.0), to = std::min(1.0, best - s + 1.0 / 64.0);
        for (int k = 0; k <= 32; ++k) {
            double const t = from + (to - from) * k / 32.0;
            double const d = QLineF(cubicAt(a, c1, c2, b, t), p).length();
            if (d < bestDistance) {
                bestDistance = d;
                best = s + t;
            }
        }
    }
    return best;
}

Stroke Stroke::section(int from, int to) const
{
    int const n = int(points.size());
    Stroke out = *this;
    out.closed = false;
    out.points.clear();
    out.thickness.clear();
    if (n == 0 || to <= from)
        return out;
    for (int i = from; i <= to; ++i)
        out.points.push_back(points[((i % n) + n) % n]);
    if (thickness.isEmpty())
        return out;
    for (StrokePoint& p : out.points) {
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
    double const span = pieces();
    bool const loop = closed && n >= 2;
    auto put = [&](double at, ThicknessPoint sides) {
        sides.at = at;
        out.thickness.push_back(sides);
    };
    put(0.0, sidesAt(from));
    for (ThicknessPoint const& key : thickness)
        for (double u : {key.at, key.at + span}) {
            if (!loop && u != key.at)
                continue;
            if (u > from + 1e-9 && u < to - 1e-9)
                put(u - from, key);
        }
    put(double(to - from), sidesAt(to));
    std::stable_sort(out.thickness.begin(), out.thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
    out.simplifyThickness(1e-4);
    return out;
}

void Stroke::reverse()
{
    int const n = int(points.size());
    double const span = pieces();
    bool const loop = closed && n >= 2;
    std::reverse(points.begin(), points.end());
    for (StrokePoint& p : points) {
        std::swap(p.in, p.out);
        p.lean = -p.lean;       // its left is now its right
    }
    std::swap(capStart, capEnd);
    if (thickness.isEmpty())
        return;
    for (ThicknessPoint& key : thickness) {
        double at = (loop ? n - 1.0 : span) - key.at;
        if (loop && at < 0.0)
            at += span;
        key.at = at;
        std::swap(key.left, key.right);
    }
    std::stable_sort(thickness.begin(), thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
}

void Stroke::append(Stroke const& other, bool meets)
{
    if (other.points.isEmpty())
        return;
    if (points.isEmpty()) {
        *this = other;
        return;
    }
    // The other line's thickness, against this one's width.
    double const scale = width > 0.0f && other.width > 0.0f ? double(other.width) / width : 1.0;
    bool const profiled = !thickness.isEmpty() || !other.thickness.isEmpty() || std::abs(scale - 1.0) > 1e-6;
    Stroke after = other;
    if (profiled) {
        ownThickness();
        after.ownThickness();
        for (ThicknessPoint& key : after.thickness) {
            key.left = float(key.left * scale);
            key.right = float(key.right * scale);
        }
        // Places at the two ends, so that the thickness runs from the one to the other.
        thicknessPointAt(pieces());
        after.thicknessPointAt(0.0);
    }
    double const offset = pieces() + (meets ? 0.0 : 1.0);
    if (curve || after.curve) {
        freezeHandles();
        after.freezeHandles();
    }
    if (meets) {
        // The point where they meet: this one's way in, the other's way out (a corner, as the two lines meet there).
        StrokePoint& meet = points.last();
        meet.out = after.points.first().out;
        meet.handles = meet.handles || after.points.first().handles;
        meet.corner = true;
    }
    for (int i = meets ? 1 : 0; i < after.points.size(); ++i)
        points.push_back(after.points[i]);
    capEnd = after.capEnd;
    if (!profiled)
        return;
    for (ThicknessPoint key : std::as_const(after.thickness)) {
        key.at += offset;
        if (!thickness.isEmpty() && key.at <= thickness.last().at + 1e-9) {
            // Where they meet: this line's thickness up to there, the other's straight after.
            if (std::abs(key.left - thickness.last().left) < 1e-5f && std::abs(key.right - thickness.last().right) < 1e-5f)
                continue;
            key.at = thickness.last().at + 1e-3;
        }
        thickness.push_back(key);
    }
}

ThicknessPoint Stroke::sidesAtLength(double fraction) const
{
    Lengths const l = lengthsOf(*this);
    ThicknessPoint sides = sidesAt(placeAtLength(l, std::clamp(fraction, 0.0, 1.0) * l.total));
    sides.at = fraction;
    return sides;
}

void Stroke::removePoint(int index)
{
    if (index < 0 || index >= points.size())
        return;
    bool const profiled = !thickness.isEmpty();
    QVector<QPair<double, ThicknessPoint>> const byLength = profiled ? thicknessByLength() : QVector<QPair<double, ThicknessPoint>>();
    points.removeAt(index);
    if (profiled)
        setThicknessByLength(byLength);
}

void Stroke::freezeHandles()
{
    if (!curve) {
        // Straight pieces: a curve whose handles have no length.
        for (StrokePoint& p : points) {
            p.handles = true;
            p.corner = true;
            p.in = p.out = QPointF();
        }
        curve = true;
        return;
    }
    QVector<QPair<QPointF, QPointF>> kept;
    for (int i = 0; i < points.size(); ++i) {
        QPointF in, out;
        handlesAt(i, in, out);
        kept.push_back({in, out});
    }
    for (int i = 0; i < points.size(); ++i) {
        points[i].handles = true;
        points[i].in = kept[i].first;
        points[i].out = kept[i].second;
    }
}
