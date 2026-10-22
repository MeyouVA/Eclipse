// Eclipse - the Camera view: paint that follows its lines.  An area painted with the paint bucket (or filled under a line
// drawn round it) is the area its lines close round: when those lines are moved, bent or reshaped, it is found again from
// them - so the paint goes wherever the lines go, as it does in Harmony.
#include "ui/canvas/Canvas.h"

#include <algorithm>

#include <QSet>
#include <QHash>
#include <cstring>

#include "model/geometry/FillRegion.h"

namespace {

bool sameShape(Stroke const& a, Stroke const& b)
{
    if (a.points.size() != b.points.size() || a.closed != b.closed || a.curve != b.curve || a.width != b.width)
        return false;
    for (int i = 0; i < a.points.size(); ++i) {
        StrokePoint const& p = a.points[i];
        StrokePoint const& q = b.points[i];
        if (p.pos != q.pos || p.handles != q.handles || p.corner != q.corner || (p.handles && (p.in != q.in || p.out != q.out)))
            return false;
    }
    return true;
}

// What bounds the paint of a sheet: its lines, the line art's lines (for the other sheets), and the brush shapes there.
QVector<Stroke> boundsOf(Drawing const& d, int sheet)
{
    QVector<Stroke> lines = d.strokesOf(sheet);
    QVector<int> sheets{sheet};
    if (sheet != Drawing::LineSheet) {
        lines += d.strokesOf(Drawing::LineSheet);
        sheets.push_back(Drawing::LineSheet);
    }
    for (int s : std::as_const(sheets))
        for (Fill const& shape : d.fillsOf(s))
            for (Stroke const& contour : shape.contours)
                lines.push_back(contour);
    return lines;
}

// Where the lines changed: what is in one and not (just the same) in the other.
QRectF changedBetween(QVector<Stroke> const& now, QVector<Stroke> const& before)
{
    QRectF changed;
    QVector<bool> matched(before.size(), false);
    for (Stroke const& line : now) {
        bool found = false;
        for (int k = 0; k < before.size() && !found; ++k)
            if (!matched[k] && sameShape(line, before[k]))
                matched[k] = found = true;
        if (!found)
            changed = changed.united(line.bounds().adjusted(-line.width, -line.width, line.width, line.width));
    }
    for (int k = 0; k < before.size(); ++k)
        if (!matched[k])
            changed = changed.united(before[k].bounds().adjusted(-before[k].width, -before[k].width, before[k].width, before[k].width));
    return changed;
}

} // namespace

QPointF Canvas::insideOf(QVector<QPolygonF> const& outlines)
{
    Fill probe;
    probe.outlines = outlines;
    QRectF box;
    for (QPolygonF const& outline : outlines)
        box = box.united(outline.boundingRect());
    if (box.isEmpty())
        return QPointF();
    if (probe.contains(box.center()))
        return box.center();
    // A point well inside: the one of a grid over it furthest from the edge (as far as the grid can tell).
    QPointF best;
    double deepest = -1.0;
    for (int i = 1; i < 16; ++i)
        for (int k = 1; k < 16; ++k) {
            QPointF const p(box.left() + box.width() * i / 16.0, box.top() + box.height() * k / 16.0);
            if (!probe.contains(p))
                continue;
            double edge = 1e300;
            for (QPolygonF const& outline : outlines)
                for (int e = 0; e < outline.size(); ++e)
                    edge = std::min(edge, QLineF(p, outline[e]).length());
            if (edge > deepest) {
                deepest = edge;
                best = p;
            }
        }
    return best;
}

namespace {

// The lines (by their place in `lines`) an area's edge runs along: each line some of the edge lies within `near` of.
QSet<int> boundingLines(QVector<QPolygonF> const& outlines, QVector<Stroke> const& lines, double near)
{
    QSet<int> found;
    QVector<QVector<QPointF>> runs;
    for (Stroke const& line : lines) {
        QVector<QPointF> run;
        for (StrokePoint const& p : line.path())
            run.push_back(p.pos);
        if (line.closed && run.size() > 2)
            run.push_back(run.first());
        runs.push_back(run);
    }
    for (QPolygonF const& outline : outlines) {
        // Places along the edge no more than a few units apart.
        QVector<QPointF> places;
        for (int i = 0; i < outline.size(); ++i) {
            QPointF const a = outline[i], b = outline[(i + 1) % outline.size()];
            int const steps = std::max(1, int(std::ceil(QLineF(a, b).length() / 3.0)));
            for (int k = 0; k < steps; ++k)
                places.push_back(a + (b - a) * (double(k) / steps));
        }
        for (int l = 0; l < runs.size(); ++l) {
            if (found.contains(l))
                continue;
            QVector<QPointF> const& run = runs[l];
            bool hit = false;
            for (QPointF const& p : std::as_const(places)) {
                for (int i = 0; i + 1 < run.size() && !hit; ++i) {
                    QPointF const ab = run[i + 1] - run[i];
                    double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
                    double const t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - run[i], ab) / len2, 0.0, 1.0) : 0.0;
                    hit = QLineF(p, run[i] + ab * t).length() <= near;
                }
                if (hit)
                    break;
            }
            if (hit)
                found.insert(l);
        }
    }
    return found;
}

// How big an area is (outlines odd-even: the outer ones less the holes in them, as near as the largest says).
double areaOf(QVector<QPolygonF> const& outlines)
{
    double total = 0.0, largest = 0.0;
    for (QPolygonF const& outline : outlines) {
        double twice = 0.0;
        for (int i = 0; i < outline.size(); ++i) {
            QPointF const a = outline[i], b = outline[(i + 1) % outline.size()];
            twice += a.x() * b.y() - b.x() * a.y();
        }
        double const area = std::abs(twice) / 2.0;
        total += area;
        largest = std::max(largest, area);
    }
    return std::max(largest - (total - largest), 0.0);
}

} // namespace

Drawing Canvas::followLines(Drawing whole, Drawing const* before, bool quick) const
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Fill>& fills = whole.fillsOf(sheet);
        bool any = false;
        for (Fill const& fill : std::as_const(fills))
            any = any || fill.follows;
        if (!any)
            continue;
        QVector<Stroke> const lines = boundsOf(whole, sheet);
        QVector<Stroke> const linesBefore = before ? boundsOf(*before, sheet) : QVector<Stroke>();
        QRectF const changed = before ? changedBetween(lines, linesBefore) : QRectF();
        if (before && changed.isNull())
            continue;       // the lines round the paint are as they were
        Drawing bounded;
        bounded.strokes = lines;
        QRectF const area = FillRegion::paintArea(bounded);
        double const room = m_gapSize + 4.0;
        double const near = m_gapSize + 3.0;
        // Which line each line was: one just as it was is that one; with as many lines as before, the one in its place.
        QVector<int> now(linesBefore.size(), -1);
        {
            QVector<bool> taken(lines.size(), false);
            for (int i = 0; i < linesBefore.size(); ++i)
                for (int k = 0; k < lines.size(); ++k)
                    if (!taken[k] && sameShape(linesBefore[i], lines[k])) {
                        now[i] = k;
                        taken[k] = true;
                        break;
                    }
            if (lines.size() == linesBefore.size())
                for (int i = 0; i < linesBefore.size(); ++i)
                    if (now[i] < 0 && !taken[i]) {
                        now[i] = i;
                        taken[i] = true;
                    }
        }
        QVector<int> gone;      // paint whose lines went with it
        for (int f = 0; f < fills.size(); ++f) {
            Fill& fill = fills[f];
            if (!fill.follows || !fill.contours.isEmpty())
                continue;
            // Paint inside a line of its own (Auto Fill): that line alone says where it is - while it is still there.
            if (fill.link != 0) {
                auto linked = [&](Drawing const& d) -> Stroke const* {
                    for (Stroke const& line : d.strokesOf(sheet))
                        if (line.link == fill.link)
                            return &line;
                    return nullptr;
                };
                auto inside = [](Stroke const& line) {
                    QPolygonF outline;
                    for (StrokePoint const& p : line.path())
                        outline << p.pos;
                    return outline;
                };
                Stroke const* const then = before ? linked(*before) : nullptr;
                Stroke const* const line = linked(whole);
                if (!before || (then && line && sameShape(*then, *line)))
                    continue;       // its line as it was
                // Still where its line went round (not moved away from it on its own)?
                bool together = false;
                if (then) {
                    QRectF const a = fill.bounds(), b = inside(*then).boundingRect();
                    together = std::abs(a.left() - b.left()) < 3.0 && std::abs(a.top() - b.top()) < 3.0 && std::abs(a.right() - b.right()) < 3.0
                               && std::abs(a.bottom() - b.bottom()) < 3.0;
                }
                if (!together) {
                    if (then || !line) {
                        fill.link = 0;      // a shape of its own now
                        fill.follows = false;
                    }
                    continue;
                }
                if (!line) {
                    gone.push_back(f);      // its line deleted: it goes with it
                    continue;
                }
                fill.outlines = {inside(*line)};
                fill.seed = insideOf(fill.outlines);
                continue;
            }
            QRectF const was = fill.bounds();
            if (before && !was.adjusted(-room, -room, room, room).intersects(changed))
                continue;       // nothing near it changed
            // The lines it was painted inside, as they are now.
            QSet<int> own;
            if (before)
                for (int i : boundingLines(fill.outlines, linesBefore, near))
                    if (now[i] >= 0)
                        own.insert(now[i]);
            // Places to look from: where it was painted (while that is still in it), well inside it - and, when it knows
            // its lines, either side of each of them all along (the area they close round may have gone over to the other
            // side of one of them).
            QVector<QPointF> tries;
            if (fill.contains(fill.seed))
                tries.push_back(fill.seed);
            if (QPointF const inside = insideOf(fill.outlines); !inside.isNull())
                tries.push_back(inside);
            QRectF within = area;
            if (!own.isEmpty() && !quick) {
                QRectF reach;
                for (int l : own) {
                    Stroke const& line = lines[l];
                    reach = reach.united(line.bounds());
                    QVector<double> places;
                    QVector<StrokePoint> const run = line.pathWithPlaces(places);
                    double walked = 1e9;
                    for (int i = 0; i + 1 < run.size(); ++i) {
                        walked += QLineF(run[i].pos, run[i + 1].pos).length();
                        if (walked < 12.0)
                            continue;
                        walked = 0.0;
                        QPointF const way = run[i + 1].pos - run[i].pos;
                        double const length = std::hypot(way.x(), way.y());
                        if (length < 1e-9)
                            continue;
                        QPointF const across(-way.y() / length, way.x() / length);
                        double const off = line.width * run[i].pressure / 2.0 + 3.0;
                        tries.push_back(run[i].pos + across * off);
                        tries.push_back(run[i].pos - across * off);
                    }
                }
                // An area closed round by these lines lies within them: no further need be looked.
                within = reach.adjusted(-2.0 * room, -2.0 * room, 2.0 * room, 2.0 * room);
            }
            double const wasArea = areaOf(fill.outlines);
            // Each area found once; the one whose lines are the lines it had (most alike), then the one most of it lies in.
            QVector<QVector<QPolygonF>> found;
            QVector<QPolygonF> best;
            double bestScore = -1.0;
            QVector<QPointF> probes;
            {
                QRectF const box = fill.bounds();
                for (int i = 1; i < 6; ++i)
                    for (int k = 1; k < 6; ++k) {
                        QPointF const p(box.left() + box.width() * i / 6.0, box.top() + box.height() * k / 6.0);
                        if (fill.contains(p))
                            probes.push_back(p);
                    }
            }
            for (int t = 0; t < tries.size(); ++t) {
                QPointF const from = tries[t];
                if (!within.contains(from))
                    continue;
                bool known = false;
                for (QVector<QPolygonF> const& outlines : std::as_const(found)) {
                    Fill probe;
                    probe.outlines = outlines;
                    known = known || probe.contains(from);
                }
                if (known)
                    continue;
                QVector<QPolygonF> outlines;
                if (FillRegion::compute(bounded, within, from, m_gapSize, outlines) != FillRegion::Ok || outlines.isEmpty())
                    continue;
                found.push_back(outlines);
                if (own.isEmpty()) {
                    best = outlines;        // (not knowing its lines: the first found, from where it was)
                    break;
                }
                QSet<int> const theirs = boundingLines(outlines, lines, near);
                double const alike = double((own & theirs).size()) / std::max<qsizetype>(1, (own | theirs).size());
                Fill probe;
                probe.outlines = outlines;
                int inside = 0;
                for (QPointF const& p : std::as_const(probes))
                    inside += probe.contains(p) ? 1 : 0;
                // (Then as much of it as lay in the paint before; then as big as the paint was.)
                double const sizes = std::min(areaOf(outlines), wasArea) / std::max({areaOf(outlines), wasArea, 1e-9});
                double const score = alike * 1000.0 + (probes.isEmpty() ? 0.0 : double(inside) / probes.size()) * 10.0 + sizes + (t == 0 ? 0.001 : 0.0);
                if (score > bestScore) {
                    bestScore = score;
                    best = outlines;
                }
                if (alike >= 1.0 && t <= 1 && inside > 0)
                    break;      // its own lines round it still, where it was: that is it
            }
            if (best.isEmpty())
                continue;       // (its lines taken away: the paint stays as it was, as in Harmony)
            fill.outlines = best;
            if (!fill.contains(fill.seed))
                fill.seed = insideOf(best);
        }
        for (int k = int(gone.size()) - 1; k >= 0; --k)
            fills.removeAt(gone[k]);
    }
    return whole;
}

bool Canvas::keepJoined(Stroke const& line)
{
    if ((m_tool != Line && m_tool != Polyline) || (m_tool == Polyline && m_polyBehind) || bitmapNow() || line.closed || line.points.size() < 2)
        return false;
    Drawing const* const d = currentDrawing();
    if (!d)
        return false;
    Drawing art = *d;
    Stroke mine = line;
    int next = std::max(art.nextJoint(), realDrawing() ? realDrawing()->nextJoint() : 1);
    bool joined = false;
    for (int end : {0, int(mine.points.size()) - 1}) {
        StrokePoint& here = mine.points[end];
        for (Stroke& other : art.strokes) {
            if (other.closed || other.points.isEmpty())
                continue;
            for (int k : {0, int(other.points.size()) - 1}) {
                StrokePoint& there = other.points[k];
                if (there.pos != here.pos)
                    continue;
                if (there.joint == 0 && here.joint == 0)
                    there.joint = here.joint = next++;
                else if (here.joint == 0)
                    here.joint = there.joint;
                else if (there.joint == 0)
                    there.joint = here.joint;
                joined = true;
            }
        }
    }
    if (!joined)
        return false;
    mine.order = Drawing::topOrder(art.fills, art.strokes);
    art.strokes.push_back(mine);
    commitDrawing(art, tr("Draw Stroke"));
    return true;
}

// ---- the fine edge of paint: on the lines themselves ---------------------------------------------------------------------

QVector<Stroke> Canvas::wallsOf(Drawing const& drawing, int sheet)
{
    return boundsOf(drawing, sheet);
}

quint64 Canvas::wallsKey(QVector<Stroke> const& walls)
{
    // Enough to tell these lines from others: where all their points are, and how they are drawn through them.
    quint64 key = 1469598103934665603ULL;
    auto mix = [&key](double v) {
        quint64 bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        key = (key ^ bits) * 1099511628211ULL;
    };
    for (Stroke const& line : walls) {
        mix(line.width);
        mix(line.closed ? 1.0 : 0.0);
        mix(line.curve ? 1.0 : 0.0);
        for (StrokePoint const& p : line.points) {
            mix(p.pos.x());
            mix(p.pos.y());
            if (p.handles) {
                mix(p.in.x() + 3.0 * p.in.y());
                mix(p.out.x() + 7.0 * p.out.y());
            }
        }
        for (ThicknessPoint const& k : line.thickness)
            mix(k.at + k.left * 13.0 + k.right * 17.0);
    }
    return key;
}

QVector<QPolygonF> Canvas::fineOutlinesOf(Fill const& fill, QVector<Stroke> const& walls, quint64 key)
{
    static QHash<quint64, QVector<QPolygonF>> kept;
    quint64 mine = key;
    auto mix = [&mine](double v) {
        quint64 bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        mine = (mine ^ bits) * 1099511628211ULL;
    };
    for (QPolygonF const& outline : fill.outlines) {
        mix(double(outline.size()));
        for (QPointF const& p : outline) {
            mix(p.x());
            mix(p.y());
        }
    }
    auto const found = kept.constFind(mine);
    if (found != kept.constEnd())
        return *found;
    if (kept.size() > 6000)
        kept.clear();
    QVector<QPolygonF> const fine = FillRegion::refine(fill.outlines, walls);
    kept.insert(mine, fine);
    return fine;
}
