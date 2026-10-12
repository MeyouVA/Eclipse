// Eclipse - the scene: a painted area - cutting and joining areas, contours; and what a drawing is.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include <QPainterPath>

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---- Fill ---------------------------------------------------------------------------------

Stroke Fill::contourOf(QPolygonF const& polygon, double tolerance)
{
    Stroke contour;
    contour.closed = true;
    QPolygonF ring = polygon;
    if (ring.size() > 1 && ring.first() == ring.last())
        ring.removeLast();
    for (QPointF const& p : std::as_const(ring))
        contour.points.push_back({p, 1.0f});
    if (ring.size() < 4 || tolerance < 0)
        return contour;     // a triangle: its three corners; or the outline wanted just as it is
    // Begun at its sharpest turn: when the outline has a corner, the place where the curve
    // closes is then that corner, and no kink is made on a smooth stretch.
    int start = 0;
    double sharpest = 2.0;
    int const count = int(ring.size());
    for (int i = 0; i < count; ++i) {
        QPointF const in = ring[i] - ring[(i + count - 1) % count], out = ring[(i + 1) % count] - ring[i];
        double const lengths = std::hypot(in.x(), in.y()) * std::hypot(out.x(), out.y());
        double const cosine = lengths > 0 ? (in.x() * out.x() + in.y() * out.y()) / lengths : 1.0;
        if (cosine < sharpest) {
            sharpest = cosine;
            start = i;
        }
    }
    Stroke open;
    for (int i = 0; i <= count; ++i)
        open.points.push_back({ring[(start + i) % count], 1.0f});
    open.fit(tolerance, 55.0);
    if (!open.curve || open.points.size() < 3)
        return contour;
    // Round to where it began: the last point is the first again, and gives it its handle.
    StrokePoint last = open.points.takeLast();
    StrokePoint& first = open.points.first();
    first.handles = true;
    first.in = last.in;
    double const inLength = std::hypot(first.in.x(), first.in.y()), outLength = std::hypot(first.out.x(), first.out.y());
    bool const turnsThere = sharpest < std::cos(55.0 * 3.14159265358979323846 / 180.0);
    if (turnsThere) {
        first.corner = true;
    } else if (inLength > 1e-9 && outLength > 1e-9) {
        // A smooth stretch: the two handles are put in line, so that no kink is left where the curve closes.
        QPointF direction = first.out / outLength - first.in / inLength;
        double const length = std::hypot(direction.x(), direction.y());
        if (length > 1e-9) {
            direction /= length;
            first.out = direction * outLength;
            first.in = -direction * inLength;
        }
    }
    contour.points = open.points;
    contour.curve = true;
    contour.closed = true;
    return contour;
}

Fill Fill::fromStroke(Stroke const& stroke, double tolerance)
{
    Fill shape;
    shape.swatch = stroke.swatch;
    shape.color = stroke.color;
    for (QPolygonF const& polygon : stroke.outlinePolygons())
        shape.contours.push_back(contourOf(polygon, tolerance));
    shape.rebuild();
    return shape;
}

// ---- cutting and joining areas -----------------------------------------------------------

namespace {

QPainterPath pathOfOutlines(QVector<QPolygonF> const& outlines)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (QPolygonF const& outline : outlines) {
        path.addPolygon(outline);
        path.closeSubpath();
    }
    return path;
}

double areaOfPolygon(QPolygonF const& polygon)
{
    double twice = 0.0;
    for (int i = 0; i < polygon.size(); ++i)
        twice += polygon[i].x() * polygon[(i + 1) % polygon.size()].y() - polygon[(i + 1) % polygon.size()].x() * polygon[i].y();
    return std::abs(twice) / 2.0;
}

// The outlines of `result` as areas: each outer outline with the holes in it is one area,
// in the colour of `like`.  An outline that is one of `before` (the outlines there were
// before the cut or the join) keeps the contour it had; any other gets one fitted to it.
QVector<Fill> areasOf(QPainterPath const& result, Fill const& like, QVector<Fill const*> const& before, double tolerance)
{
    QVector<QPolygonF> polygons;
    for (QPolygonF polygon : result.simplified().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        if (polygon.size() >= 3 && areaOfPolygon(polygon) > 0.01)
            polygons.push_back(polygon);
    }
    // Which outline is inside which: one inside an odd number of others is a hole, and
    // belongs to the smallest outline round it.
    int const count = int(polygons.size());
    QVector<int> owner(count, -1);
    QVector<bool> hole(count, false);
    for (int i = 0; i < count; ++i) {
        int inside = 0, smallest = -1;
        for (int k = 0; k < count; ++k) {
            if (k == i || !polygons[k].containsPoint(polygons[i].first(), Qt::OddEvenFill))
                continue;
            ++inside;
            if (smallest < 0 || areaOfPolygon(polygons[k]) < areaOfPolygon(polygons[smallest]))
                smallest = k;
        }
        hole[i] = inside % 2 == 1;
        owner[i] = hole[i] ? smallest : -1;
    }
    // The outline as it was before, when it was there before: found by its size and place.
    struct Old {
        QPolygonF outline;
        bool hasContour = false;
        Stroke contour;
    };
    auto old = [&](QPolygonF const& polygon, Old& found) {
        double const area = areaOfPolygon(polygon);
        QRectF const box = polygon.boundingRect();
        for (Fill const* fill : before) {
            for (int o = 0; o < fill->outlines.size(); ++o) {
                QRectF const other = fill->outlines[o].boundingRect();
                if (std::abs(areaOfPolygon(fill->outlines[o]) - area) > 1e-6 * area + 1e-6 || std::abs(other.left() - box.left()) > 1e-6
                    || std::abs(other.top() - box.top()) > 1e-6 || std::abs(other.right() - box.right()) > 1e-6
                    || std::abs(other.bottom() - box.bottom()) > 1e-6)
                    continue;
                found.outline = fill->outlines[o];
                found.hasContour = o < fill->contours.size();
                if (found.hasContour)
                    found.contour = fill->contours[o];
                return true;
            }
        }
        return false;
    };
    QVector<Fill> areas;
    for (int i = 0; i < count; ++i) {
        if (hole[i])
            continue;
        QVector<int> members{i};
        for (int k = 0; k < count; ++k)
            if (hole[k] && owner[k] == i)
                members.push_back(k);
        QVector<Old> olds(members.size());
        QVector<bool> known(members.size(), false);
        bool plain = true;      // every outline as it was, and none of them with a contour
        for (int m = 0; m < members.size(); ++m) {
            known[m] = old(polygons[members[m]], olds[m]);
            plain = plain && known[m] && !olds[m].hasContour;
        }
        Fill piece;
        piece.swatch = like.swatch;
        piece.color = like.color;
        for (int m = 0; m < members.size(); ++m) {
            if (plain) {
                piece.outlines.push_back(olds[m].outline);
            } else if (known[m] && olds[m].hasContour) {
                piece.contours.push_back(olds[m].contour);
            } else {
                piece.contours.push_back(Fill::contourOf(known[m] ? olds[m].outline : polygons[members[m]], tolerance));
            }
        }
        piece.rebuild();
        if (!piece.outlines.isEmpty())
            areas.push_back(piece);
    }
    return areas;
}

}  // namespace

QVector<Fill> Fill::pieces(QVector<int> const& counts) const
{
    Fill blank = *this;
    blank.outlines.clear();
    blank.contours.clear();
    blank.text.clear();
    blank.textFont.clear();
    blank.textAt = QPointF();
    bool const curves = contours.size() == outlines.size();
    QVector<Fill> made;
    auto put = [&](Fill& piece, int outline) {
        piece.outlines.push_back(outlines[outline]);
        if (curves)
            piece.contours.push_back(contours[outline]);
    };
    int total = 0;
    bool sound = !counts.isEmpty();
    for (int count : counts) {
        sound = sound && count > 0;
        total += count;
    }
    if (sound && total == outlines.size()) {
        int next = 0;
        for (int count : counts) {
            Fill piece = blank;
            for (int k = 0; k < count; ++k)
                put(piece, next++);
            made.push_back(piece);
        }
        return made;
    }
    // An outline inside an odd number of the others is a hole: it goes with the smallest
    // outline round it that is a shape.
    QVector<int> depth(outlines.size(), 0);
    for (int i = 0; i < outlines.size(); ++i)
        for (int j = 0; j < outlines.size(); ++j)
            if (i != j && !outlines[i].isEmpty() && outlines[j].containsPoint(outlines[i].first(), Qt::OddEvenFill))
                ++depth[i];
    QVector<int> pieceOf(outlines.size(), -1);
    for (int i = 0; i < outlines.size(); ++i) {
        if (depth[i] % 2 != 0)
            continue;
        pieceOf[i] = int(made.size());
        made.push_back(blank);
        put(made.last(), i);
    }
    for (int i = 0; i < outlines.size(); ++i) {
        if (depth[i] % 2 == 0)
            continue;
        int round = -1;
        double least = 0.0;
        for (int j = 0; j < outlines.size(); ++j) {
            if (pieceOf[j] < 0 || !outlines[j].containsPoint(outlines[i].first(), Qt::OddEvenFill))
                continue;
            QRectF const box = outlines[j].boundingRect();
            double const size = box.width() * box.height();
            if (round < 0 || size < least) {
                round = j;
                least = size;
            }
        }
        if (round >= 0)
            put(made[pieceOf[round]], i);
    }
    return made;
}

bool Fill::overlaps(Fill const& other) const
{
    if (!bounds().intersects(other.bounds()))
        return false;
    QPainterPath const common = pathOfOutlines(outlines).intersected(pathOfOutlines(other.outlines));
    double area = 0.0;
    for (QPolygonF const& polygon : common.toSubpathPolygons())
        area += areaOfPolygon(polygon);
    return area > 0.01;
}

QVector<Fill> Fill::minus(QVector<QPolygonF> const& area, double tolerance, bool* changed) const
{
    if (changed)
        *changed = false;
    QPainterPath const cut = pathOfOutlines(area);
    if (outlines.isEmpty() || !bounds().intersects(cut.boundingRect()))
        return {*this};
    QPainterPath const mine = pathOfOutlines(outlines);
    QPainterPath const taken = mine.intersected(cut);
    double lost = 0.0;
    for (QPolygonF const& polygon : taken.toSubpathPolygons())
        lost += areaOfPolygon(polygon);
    if (lost < 0.01)
        return {*this};     // the cut passes it by
    if (changed)
        *changed = true;
    return areasOf(mine.subtracted(cut), *this, {this}, tolerance);
}

bool Fill::mergeWith(Fill const& other, double tolerance)
{
    if (!overlaps(other))
        return false;
    QVector<Fill> const joined = areasOf(pathOfOutlines(outlines).united(pathOfOutlines(other.outlines)), *this, {this, &other}, tolerance);
    if (joined.isEmpty())
        return false;
    // Two areas that overlap make one area (with whatever holes are left between them).
    // Should more than one come back, they are kept together as one all the same.
    Fill all = joined.first();
    for (int k = 1; k < joined.size(); ++k) {
        if (all.contours.isEmpty())
            for (QPolygonF const& outline : std::as_const(all.outlines))
                all.contours.push_back(contourOf(outline, tolerance));
        if (joined[k].contours.isEmpty())
            for (QPolygonF const& outline : joined[k].outlines)
                all.contours.push_back(contourOf(outline, tolerance));
        else
            all.contours += joined[k].contours;
        all.rebuild();
    }
    *this = all;
    return true;
}

QVector<Stroke> Stroke::minus(QVector<QPolygonF> const& area, double tolerance, bool* changed) const
{
    if (changed)
        *changed = false;
    QPainterPath const cut = pathOfOutlines(area);
    if (points.size() < 2 || !bounds().intersects(cut.boundingRect()))
        return {*this};
    // Along the line, place by place, no more than a unit apart: inside the cut or not.
    // (`own` marks the places that are points of a line of straight pieces: the only ones
    // such a line needs, besides the ends the cut gives it.)
    struct Place {
        StrokePoint at;
        bool own = false;
        bool inside = false;
    };
    QVector<Place> line;
    {
        QVector<StrokePoint> const drawn = path();
        for (int i = 0; i < drawn.size(); ++i) {
            StrokePoint here = drawn[i];
            here.handles = here.corner = false;
            if (i > 0) {
                StrokePoint const& before = line.last().at;
                double const length = QLineF(before.pos, here.pos).length();
                int const steps = std::clamp(int(std::ceil(length)), 1, 4000);
                for (int k = 1; k < steps; ++k) {
                    double const t = double(k) / steps;
                    StrokePoint between;
                    between.pos = before.pos + (here.pos - before.pos) * t;
                    between.pressure = float(before.pressure + (here.pressure - before.pressure) * t);
                    line.push_back({between, false, false});
                }
            }
            line.push_back({here, !curve, false});
        }
    }
    bool any = false;
    for (Place& place : line) {
        place.inside = cut.contains(place.at.pos);
        any = any || place.inside;
    }
    auto between = [&](int i) {     // the middle of the stretch from place i to the next
        StrokePoint middle;
        middle.pos = (line[i].at.pos + line[i + 1].at.pos) / 2.0;
        middle.pressure = (line[i].at.pressure + line[i + 1].at.pressure) / 2.0f;
        return middle;
    };
    if (!any) {
        // No place of it is inside; a cut thinner than the gap between places may still pass through.
        bool crossed = false;
        for (int i = 0; i + 1 < line.size() && !crossed; ++i)
            crossed = cut.contains(between(i).pos);
        if (!crossed)
            return {*this};
    }
    if (changed)
        *changed = true;
    // Where exactly the line passes the edge of the cut, between a place outside and one inside.
    auto edge = [&](StrokePoint const& out, StrokePoint const& in) {
        double low = 0.0, high = 1.0;
        for (int round = 0; round < 14; ++round) {
            double const middle = (low + high) / 2.0;
            if (cut.contains(out.pos + (in.pos - out.pos) * middle))
                high = middle;
            else
                low = middle;
        }
        StrokePoint at;
        at.pos = out.pos + (in.pos - out.pos) * low;
        at.pressure = float(out.pressure + (in.pressure - out.pressure) * low);
        return at;
    };
    // The runs of line left; for each, whether it begins and ends at a cut (or at an end the line had).
    struct Run {
        QVector<Place> places;
        bool cutAtStart = false, cutAtEnd = false;
    };
    QVector<Run> runs;
    Run run;
    for (int i = 0; i < line.size(); ++i) {
        if (!line[i].inside) {
            if (run.places.isEmpty() && i > 0 && line[i - 1].inside) {
                run.places.push_back({edge(line[i].at, line[i - 1].at), true, false});
                run.cutAtStart = true;
            }
            run.places.push_back(line[i]);
            if (i + 1 < line.size() && !line[i + 1].inside && cut.contains(between(i).pos)) {
                // A thin cut between this place and the next: the line is parted there.
                StrokePoint const middle = between(i);
                run.places.push_back({edge(line[i].at, middle), true, false});
                run.cutAtEnd = true;
                runs.push_back(run);
                run = Run();
                run.places.push_back({edge(line[i + 1].at, middle), true, false});
                run.cutAtStart = true;
            }
        } else if (!run.places.isEmpty()) {
            run.places.push_back({edge(line[i - 1].at, line[i].at), true, false});
            run.cutAtEnd = true;
            runs.push_back(run);
            run = Run();
        }
    }
    if (!run.places.isEmpty())
        runs.push_back(run);
    // A closed line that was not cut where it closes: its last piece runs on into its first.
    if (closed && runs.size() >= 2 && !line.first().inside && !line.last().inside && !runs.last().cutAtEnd && !runs.first().cutAtStart) {
        Run joined = runs.takeLast();
        joined.places += runs.first().places.mid(1);
        joined.cutAtEnd = runs.first().cutAtEnd;
        runs.first() = joined;
    }
    QVector<Stroke> pieces;
    for (Run const& left : std::as_const(runs)) {
        double length = 0.0;
        for (int i = 1; i < left.places.size(); ++i)
            length += QLineF(left.places[i - 1].at.pos, left.places[i].at.pos).length();
        if (left.places.size() < 2 || length < 0.05)
            continue;
        Stroke piece = *this;
        piece.closed = false;
        piece.curve = false;
        piece.points.clear();
        for (int i = 0; i < left.places.size(); ++i)
            if (curve || left.places[i].own || i == 0 || i + 1 == left.places.size())
                piece.points.push_back(left.places[i].at);
        if (curve)
            piece.fit(tolerance, 50.0);     // a curve again, with a few points
        // An end made by the cut is cut off square, as the eraser left it; the ends the line had stay as they were.
        if (left.cutAtStart || closed)
            piece.capStart = FlatCap;
        if (left.cutAtEnd || closed)
            piece.capEnd = FlatCap;
        pieces.push_back(piece);
    }
    return pieces;
}

void Fill::rebuild()
{
    if (contours.isEmpty())
        return;
    outlines.clear();
    for (Stroke const& contour : std::as_const(contours)) {
        QPolygonF polygon;
        for (StrokePoint const& p : contour.path())
            polygon << p.pos;
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();   // a closed path comes back to its start
        if (polygon.size() >= 3)
            outlines.push_back(polygon);
    }
}

bool Fill::hit(QPointF p, double radius) const
{
    if (!bounds().adjusted(-radius, -radius, radius, radius).contains(p))
        return false;
    if (contains(p))
        return true;
    for (QPolygonF const& o : outlines)
        for (int i = 0; i < o.size(); ++i)
            if (distanceToSegment(p, o[i], o[(i + 1) % o.size()]) <= radius)
                return true;
    return false;
}

QRectF Fill::bounds() const
{
    QRectF r;
    for (QPolygonF const& o : outlines)
        r = r.isNull() ? o.boundingRect() : r.united(o.boundingRect());
    return r;
}

bool Fill::contains(QPointF p) const
{
    bool inside = false;
    for (QPolygonF const& o : outlines)
        if (o.containsPoint(p, Qt::OddEvenFill))
            inside = !inside;
    return inside;
}

void Fill::transform(QTransform const& t)
{
    textAt = t.map(textAt);
    gradFrom = t.map(gradFrom);
    gradTo = t.map(gradTo);
    if (!contours.isEmpty()) {
        // A contour shape: its points and handles are moved, and the outlines drawn again.
        for (Stroke& contour : contours)
            contour.transform(t);
        rebuild();
        return;
    }
    for (QPolygonF& o : outlines)
        o = t.map(o);
}

QRectF Drawing::bounds() const
{
    QRectF r;
    for (Picture const& p : pictures)
        r = r.isNull() ? p.bounds() : r.united(p.bounds());
    for (int sheet = 0; sheet < SheetCount; ++sheet) {
        for (Stroke const& s : strokesOf(sheet))
            r = r.isNull() ? s.bounds() : r.united(s.bounds());
        for (Fill const& f : fillsOf(sheet))
            r = r.isNull() ? f.bounds() : r.united(f.bounds());
    }
    return r;
}
