// Eclipse - morphing, its parts.
#include "model/geometry/MorphParts.h"

#include "model/geometry/Morph.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace morph_parts {

QVector<Sample> spaced(QVector<Sample> path, int count, bool closed)
{
    QVector<Sample> out;
    if (path.isEmpty())
        return out;
    if (closed)
        path.push_back(path.first());
    QVector<double> along{0.0};
    for (int i = 1; i < path.size(); ++i)
        along.push_back(along.last() + QLineF(path[i - 1].pos, path[i].pos).length());
    double const length = along.last();
    int const steps = closed ? count : count - 1;
    int at = 0;
    for (int i = 0; i < count; ++i) {
        double const want = steps > 0 ? length * i / steps : 0.0;
        while (at + 2 < path.size() && along[at + 1] < want)
            ++at;
        if (path.size() == 1 || length <= 0.0) {
            out.push_back(path.first());
            continue;
        }
        double const span = along[at + 1] - along[at];
        double const u = span > 0.0 ? std::clamp((want - along[at]) / span, 0.0, 1.0) : 0.0;
        out.push_back({path[at].pos + (path[at + 1].pos - path[at].pos) * u, path[at].pressure + (path[at + 1].pressure - path[at].pressure) * u});
    }
    return out;
}

QPointF middleOf(QVector<Sample> const& samples)
{
    QPointF sum;
    for (Sample const& s : samples)
        sum += s.pos;
    return samples.isEmpty() ? sum : sum / double(samples.size());
}

double apart(QVector<Sample> const& a, QVector<Sample> const& b, int shift, bool reversed)
{
    QPointF const ma = middleOf(a), mb = middleOf(b);
    int const n = int(a.size());
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        int const k = reversed ? (shift - i % n + 2 * n) % n : (i + shift) % n;
        QPointF const d = (a[i].pos - ma) - (b[k].pos - mb);
        sum += d.x() * d.x() + d.y() * d.y();
    }
    return sum;
}

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

void pairUp(QVector<QPointF> const& first, QVector<QPointF> const& second, QVector<int>& partner, QVector<int>& left)
{
    partner = QVector<int>(first.size(), -1);
    left.clear();
    QVector<bool> taken(second.size(), false);
    if (first.size() == second.size()) {
        // As many of each: in the order they were drawn.
        for (int i = 0; i < first.size(); ++i) {
            partner[i] = i;
            taken[i] = true;
        }
    } else {
        // Each with the nearest still free, the nearest pairs first.
        struct Pair {
            double distance;
            int a, b;
        };
        QVector<Pair> pairs;
        for (int i = 0; i < first.size(); ++i)
            for (int k = 0; k < second.size(); ++k)
                pairs.push_back({QLineF(first[i], second[k]).length(), i, k});
        std::sort(pairs.begin(), pairs.end(), [](Pair const& x, Pair const& y) { return x.distance < y.distance; });
        for (Pair const& pair : std::as_const(pairs)) {
            if (partner[pair.a] >= 0 || taken[pair.b])
                continue;
            partner[pair.a] = pair.b;
            taken[pair.b] = true;
        }
    }
    for (int k = 0; k < second.size(); ++k)
        if (!taken[k])
            left.push_back(k);
}

void colourBetween(int swatchA, QColor const& a, int swatchB, QColor const& b, double t, int& swatch, QColor& colour)
{
    if (swatchA == swatchB && (swatchA != Scene::kNoSwatch || a == b)) {
        swatch = swatchA;
        colour = a;
    } else if (swatchA == Scene::kNoSwatch && swatchB == Scene::kNoSwatch) {
        swatch = Scene::kNoSwatch;
        colour = QColor(int(mix(a.red(), b.red(), t) + 0.5), int(mix(a.green(), b.green(), t) + 0.5), int(mix(a.blue(), b.blue(), t) + 0.5),
                        int(mix(a.alpha(), b.alpha(), t) + 0.5));
    } else {
        swatch = t < 0.5 ? swatchA : swatchB;
        colour = t < 0.5 ? a : b;
    }
}

QVector<Sample> samplesOf(Stroke const& stroke)
{
    QVector<Sample> path;
    for (StrokePoint const& p : stroke.path())
        path.push_back({p.pos, double(p.pressure)});
    if (stroke.closed && path.size() > 1 && path.first().pos == path.last().pos)
        path.removeLast();
    return path;
}

QVector<Sample> samplesOf(QPolygonF const& outline)
{
    QVector<Sample> path;
    for (QPointF const& p : outline)
        path.push_back({p, 1.0});
    if (path.size() > 1 && path.first().pos == path.last().pos)
        path.removeLast();
    return path;
}

int nearestSample(QVector<Sample> const& run, QPointF p)
{
    int best = 0;
    double nearest = -1.0;
    for (int i = 0; i < run.size(); ++i) {
        double const away = QLineF(run[i].pos, p).length();
        if (nearest < 0.0 || away < nearest) {
            nearest = away;
            best = i;
        }
    }
    return best;
}

QVector<QPair<int, int>> pinsFor(QVector<Sample> const& from, QVector<Sample> const& to, QVector<QLineF> const& hints)
{
    QVector<QPair<int, int>> pins;
    if (from.isEmpty() || to.isEmpty())
        return pins;
    for (QLineF const& hint : hints)
        pins.push_back({nearestSample(from, hint.p1()), nearestSample(to, hint.p2())});
    return pins;
}

double awayFromRun(QVector<QPointF> const& run, bool closed, QPointF p)
{
    if (run.isEmpty())
        return 1e18;
    double nearest = QLineF(run.first(), p).length();
    int const n = int(run.size());
    for (int i = 0; i + 1 < n || (closed && i < n); ++i) {
        QPointF const a = run[i], b = run[(i + 1) % n];
        QPointF const ab = b - a;
        double const length2 = ab.x() * ab.x() + ab.y() * ab.y();
        double const u = length2 > 0.0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length2, 0.0, 1.0) : 0.0;
        nearest = std::min(nearest, QLineF(a + ab * u, p).length());
    }
    return nearest;
}

double awayFrom(Stroke const& stroke, QPointF p)
{
    QVector<QPointF> run;
    for (StrokePoint const& point : stroke.path())
        run.push_back(point.pos);
    return awayFromRun(run, stroke.closed, p);
}

double awayFrom(Fill const& fill, QPointF p)
{
    if (fill.contains(p))
        return 0.0;
    double nearest = 1e18;
    for (QPolygonF const& outline : fill.outlines)
        nearest = std::min(nearest, awayFromRun(outline, true, p));
    return nearest;
}


bool splitTo(Stroke& s, int count)
{
    int const segments = [&] { return int(s.points.size()) - (s.closed ? 0 : 1); }();
    if (!s.curve || segments < 1 || s.points.size() > count)
        return false;
    for (StrokePoint const& p : std::as_const(s.points))
        if (!p.handles)
            return false;
    while (s.points.size() < count) {
        int const n = int(s.points.size()), pieces = n - (s.closed ? 0 : 1);
        int longest = 0;
        double most = -1.0;
        for (int i = 0; i < pieces; ++i) {
            StrokePoint const& p = s.points[i];
            StrokePoint const& q = s.points[(i + 1) % n];
            double const length = QLineF(p.pos, p.pos + p.out).length() + QLineF(p.pos + p.out, q.pos + q.in).length() + QLineF(q.pos + q.in, q.pos).length();
            if (length > most) {
                most = length;
                longest = i;
            }
        }
        StrokePoint& p = s.points[longest];
        StrokePoint& q = s.points[(longest + 1) % n];
        QPointF const p0 = p.pos, p1 = p.pos + p.out, p2 = q.pos + q.in, p3 = q.pos;
        QPointF const a = (p0 + p1) / 2, b = (p1 + p2) / 2, c = (p2 + p3) / 2, d = (a + b) / 2, e = (b + c) / 2, middle = (d + e) / 2;
        p.out = a - p0;
        q.in = c - p3;
        StrokePoint made;
        made.pos = middle;
        made.pressure = (p.pressure + q.pressure) / 2.0f;
        made.handles = true;
        made.in = d - middle;
        made.out = e - middle;
        s.points.insert(longest + 1, made);
    }
    return true;
}

bool curveBetween(Stroke const& fromCurve, Stroke const& toCurve, double t, Stroke& out)
{
    // Not as many points: the one with fewer is given more, its shape as it was.
    Stroke a = fromCurve, b = toCurve;
    if (a.points.size() < b.points.size())
        splitTo(a, int(b.points.size()));
    else if (b.points.size() < a.points.size())
        splitTo(b, int(a.points.size()));
    int const n = int(a.points.size());
    if (n < 2 || b.points.size() != n || a.closed != b.closed || !a.curve || !b.curve)
        return false;
    auto partnerOf = [&](int i, int shift, bool reversed) {
        int const j = reversed ? (a.closed ? shift - i : n - 1 - i) : (a.closed ? i + shift : i);
        return ((j % n) + n) % n;
    };
    int bestShift = 0;
    bool bestReversed = false;
    double least = 1e300;
    for (bool reversed : {false, true})
        for (int shift = 0; shift < (a.closed ? n : 1); ++shift) {
            double cost = 0.0;
            for (int i = 0; i < n && cost < least; ++i) {
                QPointF const d = a.points[i].pos - b.points[partnerOf(i, shift, reversed)].pos;
                cost += d.x() * d.x() + d.y() * d.y();
            }
            if (cost < least) {
                least = cost;
                bestShift = shift;
                bestReversed = reversed;
            }
        }
    out = t < 0.5 ? a : b;
    out.points.clear();
    out.thickness.clear();
    out.curve = true;
    out.closed = a.closed;
    out.group = 0;
    for (int i = 0; i < n; ++i) {
        StrokePoint const& p = a.points[i];
        StrokePoint q = b.points[partnerOf(i, bestShift, bestReversed)];
        if (bestReversed)
            std::swap(q.in, q.out);     // (run backwards, its handles face the other way)
        StrokePoint r;
        r.pos = p.pos + (q.pos - p.pos) * t;
        r.pressure = float(mix(p.pressure, q.pressure, t));
        r.corner = t < 0.5 ? p.corner : q.corner;
        r.handles = p.handles && q.handles;
        if (r.handles) {
            r.in = p.in + (q.in - p.in) * t;
            r.out = p.out + (q.out - p.out) * t;
        }
        out.points.push_back(r);
    }
    if (!a.thickness.isEmpty() || !b.thickness.isEmpty()) {
        // The thickness along the lines (apart from their points): mixed place by place along them.
        QVector<double> shares;
        for (Stroke const* line : {&a, &b})
            for (auto const& entry : line->thicknessByLength())
                shares.push_back(bestReversed && line == &b ? 1.0 - entry.first : entry.first);
        std::sort(shares.begin(), shares.end());
        shares.erase(std::unique(shares.begin(), shares.end(), [](double x, double y) { return y - x < 1e-6; }), shares.end());
        QVector<QPair<double, ThicknessPoint>> mixed;
        for (double f : std::as_const(shares)) {
            ThicknessPoint const p = a.sidesAtLength(f);
            ThicknessPoint q = b.sidesAtLength(bestReversed ? 1.0 - f : f);
            if (bestReversed)
                std::swap(q.left, q.right);
            mixed.push_back({f, {0.0, float(mix(p.left, q.left, t)), float(mix(p.right, q.right, t))}});
        }
        out.setThicknessByLength(mixed);
        out.simplifyThickness(0.005);
    }
    return true;
}

Stroke strokeShrunk(Stroke const& stroke, double size)
{
    Stroke out = stroke;
    out.group = 0;
    QPointF const middle = stroke.bounds().center();
    out.transform(QTransform::fromTranslate(-middle.x(), -middle.y()) * QTransform::fromScale(size, size) * QTransform::fromTranslate(middle.x(), middle.y()));
    out.width = float(stroke.width * size);
    return out;
}

QPolygonF outlineShrunk(QPolygonF const& outline, double size)
{
    QPointF const middle = outline.boundingRect().center();
    QPolygonF out;
    for (QPointF const& p : outline)
        out << middle + (p - middle) * size;
    return out;
}

Fill fillShrunk(Fill const& fill, double size)
{
    Fill out;
    out.swatch = fill.swatch;
    out.color = fill.color;
    QPointF const middle = fill.bounds().center();
    for (QPolygonF const& outline : fill.outlines) {
        QPolygonF shrunk;
        for (QPointF const& p : outline)
            shrunk << middle + (p - middle) * size;
        out.outlines.push_back(shrunk);
    }
    return out;
}

Stroke strokeBetween(Stroke const& a, Stroke const& b, double t, QVector<QLineF> const& hints)
{
    if (Stroke curved; hints.isEmpty() && curveBetween(a, b, t, curved)) {
        // Curves of as many points: each point goes to its partner, and the in-between is a curve of those points.
        curved.width = float(mix(a.width, b.width, t));
        colourBetween(a.swatch, a.color, b.swatch, b.color, t, curved.swatch, curved.color);
        return curved;
    }
    bool const closed = a.closed && b.closed;
    QVector<Sample> const from = spaced(samplesOf(a), kLinePoints, closed);
    QVector<Sample> const other = spaced(samplesOf(b), kLinePoints, closed);
    QVector<Sample> const to = aligned(from, other, closed, pinsFor(from, other, hints));
    Stroke out = t < 0.5 ? a : b;
    out.points.clear();
    out.thickness.clear();      // (its places carry the thickness, the two lines' mixed)
    out.curve = false;
    out.closed = closed;
    out.group = 0;
    out.width = float(mix(a.width, b.width, t));
    colourBetween(a.swatch, a.color, b.swatch, b.color, t, out.swatch, out.color);
    for (int i = 0; i < from.size() && i < to.size(); ++i)
        out.points.push_back({from[i].pos + (to[i].pos - from[i].pos) * t, float(mix(from[i].pressure, to[i].pressure, t))});
    return out;
}

QPolygonF outlineBetween(QPolygonF const& a, QPolygonF const& b, double t, QVector<QLineF> const& hints)
{
    QVector<Sample> const from = spaced(samplesOf(a), kEdgePoints, true);
    QVector<Sample> const other = spaced(samplesOf(b), kEdgePoints, true);
    QVector<Sample> const to = aligned(from, other, true, pinsFor(from, other, hints));
    QPolygonF out;
    for (int i = 0; i < from.size() && i < to.size(); ++i)
        out << from[i].pos + (to[i].pos - from[i].pos) * t;
    return out;
}

Fill fillBetween(Fill const& a, Fill const& b, double t, QVector<QLineF> const& hints)
{
    Fill out;
    colourBetween(a.swatch, a.color, b.swatch, b.color, t, out.swatch, out.color);
    if (a.gradPlaced || b.gradPlaced) {
        QRectF const boxA = a.bounds(), boxB = b.bounds();
        QPointF const fromA = a.gradPlaced ? a.gradFrom : QPointF(boxA.left(), boxA.center().y());
        QPointF const toA = a.gradPlaced ? a.gradTo : QPointF(boxA.right(), boxA.center().y());
        QPointF const fromB = b.gradPlaced ? b.gradFrom : QPointF(boxB.left(), boxB.center().y());
        QPointF const toB = b.gradPlaced ? b.gradTo : QPointF(boxB.right(), boxB.center().y());
        out.gradPlaced = true;
        out.gradFrom = fromA + (fromB - fromA) * t;
        out.gradTo = toA + (toB - toA) * t;
    }
    QVector<QPointF> first, second;
    for (QPolygonF const& outline : a.outlines)
        first.push_back(outline.boundingRect().center());
    for (QPolygonF const& outline : b.outlines)
        second.push_back(outline.boundingRect().center());
    QVector<int> partner, left;
    pairUp(first, second, partner, left);
    if (hints.isEmpty() && left.isEmpty() && !a.contours.isEmpty() && a.contours.size() == a.outlines.size() && b.contours.size() == b.outlines.size()) {
        QVector<Stroke> contours;
        bool all = true;
        for (int i = 0; i < a.contours.size() && all; ++i) {
            Stroke made;
            all = partner[i] >= 0 && curveBetween(a.contours[i], b.contours[partner[i]], t, made);
            contours.push_back(made);
        }
        if (all) {
            out.contours = contours;
            out.rebuild();
            return out;
        }
    }
    for (int i = 0; i < a.outlines.size(); ++i) {
        // A hint is for the outline of the area it lies nearest.
        QVector<QLineF> mine;
        for (QLineF const& hint : hints) {
            int nearest = -1;
            double least = 1e18;
            for (int o = 0; o < a.outlines.size(); ++o) {
                double const away = awayFromRun(a.outlines[o], true, hint.p1());
                if (away < least) {
                    least = away;
                    nearest = o;
                }
            }
            if (nearest == i)
                mine.push_back(hint);
        }
        QPolygonF const outline = partner[i] >= 0 ? outlineBetween(a.outlines[i], b.outlines[partner[i]], t, mine) : outlineShrunk(a.outlines[i], 1.0 - t);
        if (outline.size() >= 3)
            out.outlines.push_back(outline);
    }
    for (int k : std::as_const(left)) {
        QPolygonF const outline = outlineShrunk(b.outlines[k], t);
        if (outline.size() >= 3)
            out.outlines.push_back(outline);
    }
    return out;
}

}  // namespace morph_parts
