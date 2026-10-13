#include "model/geometry/Morph.h"

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

Stroke strokeBetween(Stroke const& a, Stroke const& b, double t, QVector<QLineF> const& hints = {})
{
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
    QVector<QPointF> first, second;
    for (QPolygonF const& outline : a.outlines)
        first.push_back(outline.boundingRect().center());
    for (QPolygonF const& outline : b.outlines)
        second.push_back(outline.boundingRect().center());
    QVector<int> partner, left;
    pairUp(first, second, partner, left);
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
