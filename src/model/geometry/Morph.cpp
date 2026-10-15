#include "model/geometry/Morph.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kLinePoints = 40;     // a line in between is this many points
constexpr int kEdgePoints = 64;     // an outline in between is this many

struct Sample {
    QPointF pos;
    double pressure = 1.0;
};

// `count` places evenly spaced along a path (round it and back to its start when closed).
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

// How far apart two runs of places are, each taken from its own middle: so that a shape
// which has only moved is matched point for point, not turned round.
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

// `b` put in the order that lies nearest `a`: begun at the best place (a closed run) and
// run the best way round.
// (`pins`: a point of `a`, by its number, and the point of `b` it is to be matched with - what hints ask for.
// They count before nearness does.)
QVector<Sample> aligned(QVector<Sample> const& a, QVector<Sample> const& b, bool closed, QVector<QPair<int, int>> const& pins = {})
{
    int const n = int(a.size());
    if (n == 0 || b.size() != n)
        return b;
    auto missed = [&](int shift, bool reversed) {
        double sum = 0.0;
        for (QPair<int, int> const& pin : pins) {
            int const k = reversed ? (shift - pin.first % n + 2 * n) % n : (pin.first + shift) % n;
            QPointF const d = b[k].pos - b[pin.second].pos;
            sum += d.x() * d.x() + d.y() * d.y();
        }
        return sum;
    };
    int bestShift = 0;
    bool bestReversed = false;
    double best = apart(a, b, 0, false);
    double bestMissed = missed(0, false);
    auto consider = [&](int shift, bool reversed) {
        double const d = apart(a, b, shift, reversed);
        double const m = missed(shift, reversed);
        if (m < bestMissed - 1e-9 || (m <= bestMissed + 1e-9 && d < best - 1e-9)) {
            best = d;
            bestMissed = m;
            bestShift = shift;
            bestReversed = reversed;
        }
    };
    if (closed) {
        for (int shift = 0; shift < n; ++shift) {
            consider(shift, false);
            consider(shift, true);
        }
    } else {
        consider(n - 1, true);      // the other way along
    }
    QVector<Sample> out;
    for (int i = 0; i < n; ++i)
        out.push_back(b[bestReversed ? (bestShift - i % n + 2 * n) % n : (i + bestShift) % n]);
    return out;
}

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

// Which of the second lot each of the first goes with (-1: none), and which of the second are left over.
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

// The colour of something in between: that of both when they share it, a mix when
// neither is linked to the palette, else that of the nearer drawing.
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

// The point of a run that lies nearest a place.
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

// How far a place is from a run of points joined up.
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

// A hint is for the shape it lies on, or within this of.
constexpr double kHintReach = 25.0;

// What the hints ask of the pairing: the shape of the first lot nearest the one end of a hint goes with the
// shape of the second lot nearest the other end; whatever had that partner takes the one given up.
template <typename Shape>
void pairByHints(QVector<Shape> const& from, QVector<Shape> const& to, QVector<QLineF> const& hints, QVector<int>& partner, QVector<int>& left,
                 QVector<QVector<QLineF>>& hintsOf)
{
    hintsOf = QVector<QVector<QLineF>>(from.size());
    for (QLineF const& hint : hints) {
        int mine = -1, theirs = -1;
        double nearMine = kHintReach, nearTheirs = kHintReach;
        for (int i = 0; i < from.size(); ++i) {
            double const away = awayFrom(from[i], hint.p1());
            if (away <= nearMine) {
                nearMine = away;
                mine = i;
            }
        }
        for (int k = 0; k < to.size(); ++k) {
            double const away = awayFrom(to[k], hint.p2());
            if (away <= nearTheirs) {
                nearTheirs = away;
                theirs = k;
            }
        }
        if (mine < 0 || theirs < 0)
            continue;
        if (partner[mine] != theirs) {
            int const had = partner[mine];
            int const holder = int(partner.indexOf(theirs));
            partner[mine] = theirs;
            if (holder >= 0)
                partner[holder] = had;
        }
        hintsOf[mine].push_back(hint);
    }
    left.clear();
    for (int k = 0; k < to.size(); ++k)
        if (!partner.contains(k))
            left.push_back(k);
}

// More points on a curve without changing its shape: its longest pieces are each split in two where they are half done
// (de Casteljau), until it has `count`.  False when it cannot be (a point without handles, too few points).
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

// Two curves of as many points (with their handles), point for point - the points of the one paired with those of the
// other where that moves them least (round a closed one, and either way along) - as far from the one to the other as `t`.
// The points, handles and corners are kept: what comes between is a curve of those points too.  Curves of different numbers
// of points: the one with fewer is split (in shape as it was) to as many.  False when they cannot be paired so (not curves,
// a point without handles where points are to be added, one closed and one not).
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
    return true;
}

Stroke strokeBetween(Stroke const& a, Stroke const& b, double t, QVector<QLineF> const& hints = {})
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
    out.curve = false;
    out.closed = closed;
    out.group = 0;
    out.width = float(mix(a.width, b.width, t));
    colourBetween(a.swatch, a.color, b.swatch, b.color, t, out.swatch, out.color);
    for (int i = 0; i < from.size() && i < to.size(); ++i)
        out.points.push_back({from[i].pos + (to[i].pos - from[i].pos) * t, float(mix(from[i].pressure, to[i].pressure, t))});
    return out;
}

// A line on its way to nothing (or from nothing): drawn towards its middle, and thinner.
Stroke strokeShrunk(Stroke const& stroke, double size)
{
    Stroke out = stroke;
    out.group = 0;
    QPointF const middle = stroke.bounds().center();
    out.transform(QTransform::fromTranslate(-middle.x(), -middle.y()) * QTransform::fromScale(size, size) * QTransform::fromTranslate(middle.x(), middle.y()));
    out.width = float(stroke.width * size);
    return out;
}

QPolygonF outlineBetween(QPolygonF const& a, QPolygonF const& b, double t, QVector<QLineF> const& hints = {})
{
    QVector<Sample> const from = spaced(samplesOf(a), kEdgePoints, true);
    QVector<Sample> const other = spaced(samplesOf(b), kEdgePoints, true);
    QVector<Sample> const to = aligned(from, other, true, pinsFor(from, other, hints));
    QPolygonF out;
    for (int i = 0; i < from.size() && i < to.size(); ++i)
        out << from[i].pos + (to[i].pos - from[i].pos) * t;
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

Fill fillBetween(Fill const& a, Fill const& b, double t, QVector<QLineF> const& hints = {})
{
    Fill out;
    colourBetween(a.swatch, a.color, b.swatch, b.color, t, out.swatch, out.color);
    // A gradient placed by hand goes from where it lay in the one to where it lies in the other
    // (one not placed lies across its area: from side to side of it).
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
        // Contour shapes whose contours pair up point for point: the contours themselves go from the one to the other,
        // and the area in between is a contour shape with those points too.
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

}  // namespace

int Morphing::shapeKey(int sheet, bool stroke, int index)
{
    return ((sheet * 2 + (stroke ? 1 : 0)) << 24) | (index & 0xffffff);
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints, QVector<Morph::Group> const& groups)
{
    if (groups.isEmpty())
        return between(a, b, t, hints);
    // The shapes of a drawing that a set of keys names (or, `rest`, that none of the morphing layers names).
    auto part = [](Drawing const& d, QSet<int> const& keys, bool rest) {
        Drawing out;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            QVector<Fill> const& fills = d.fillsOf(sheet);
            for (int i = 0; i < fills.size(); ++i)
                if (keys.contains(shapeKey(sheet, false, i)) != rest)
                    out.fillsOf(sheet).push_back(fills[i]);
            QVector<Stroke> const& strokes = d.strokesOf(sheet);
            for (int i = 0; i < strokes.size(); ++i)
                if (keys.contains(shapeKey(sheet, true, i)) != rest)
                    out.strokesOf(sheet).push_back(strokes[i]);
        }
        return out;
    };
    Drawing out;
    auto add = [&](Drawing const& d) {
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            out.fillsOf(sheet) += d.fillsOf(sheet);
            out.strokesOf(sheet) += d.strokesOf(sheet);
        }
    };
    QSet<int> allFirst, allSecond;
    for (Morph::Group const& g : groups) {
        QSet<int> const first(g.first.cbegin(), g.first.cend()), second(g.second.cbegin(), g.second.cend());
        allFirst += first;
        allSecond += second;
        add(between(part(a, first, false), part(b, second, false), t, hints));
    }
    Drawing const rest = between(part(a, allFirst, true), part(b, allSecond, true), t, hints);
    add(rest);
    out.pictures = t < 0.5 ? a.pictures : b.pictures;
    return out;
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0)
        return a;
    if (t >= 1.0)
        return b;
    Drawing out;
    out.pictures = t < 0.5 ? a.pictures : b.pictures;     // a picture is not morphed: one, then the other
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        // The painted areas.
        {
            QVector<Fill> const& from = a.fillsOf(sheet);
            QVector<Fill> const& to = b.fillsOf(sheet);
            QVector<QPointF> first, second;
            for (Fill const& fill : from)
                first.push_back(fill.bounds().center());
            for (Fill const& fill : to)
                second.push_back(fill.bounds().center());
            QVector<int> partner, left;
            pairUp(first, second, partner, left);
            QVector<QVector<QLineF>> hintsOf(from.size());
            if (!hints.isEmpty())
                pairByHints(from, to, hints, partner, left, hintsOf);
            QVector<Fill>& made = out.fillsOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                Fill const fill = partner[i] >= 0 ? fillBetween(from[i], to[partner[i]], t, hintsOf[i]) : fillShrunk(from[i], 1.0 - t);
                if (!fill.outlines.isEmpty())
                    made.push_back(fill);
            }
            for (int k : std::as_const(left))
                made.push_back(fillShrunk(to[k], t));
        }
        // The lines.
        {
            QVector<Stroke> const& from = a.strokesOf(sheet);
            QVector<Stroke> const& to = b.strokesOf(sheet);
            QVector<QPointF> first, second;
            for (Stroke const& stroke : from)
                first.push_back(stroke.bounds().center());
            for (Stroke const& stroke : to)
                second.push_back(stroke.bounds().center());
            QVector<int> partner, left;
            pairUp(first, second, partner, left);
            QVector<QVector<QLineF>> hintsOf(from.size());
            if (!hints.isEmpty())
                pairByHints(from, to, hints, partner, left, hintsOf);
            QVector<Stroke>& made = out.strokesOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                if (from[i].points.isEmpty())
                    continue;
                made.push_back(partner[i] >= 0 && !to[partner[i]].points.isEmpty() ? strokeBetween(from[i], to[partner[i]], t, hintsOf[i]) : strokeShrunk(from[i], 1.0 - t));
            }
            for (int k : std::as_const(left))
                if (!to[k].points.isEmpty())
                    made.push_back(strokeShrunk(to[k], t));
        }
    }
    return out;
}
