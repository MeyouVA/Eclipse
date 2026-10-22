// Eclipse - painted areas: lines cut by areas, contours given and rebuilt, hit-testing, bounds, transforms.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include <QHash>
#include <QPainterPath>
#include <QSet>

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "model/scene/FillShared.h"

using namespace fill_shared;

QVector<Stroke> Stroke::minus(QVector<QPolygonF> const& area, double tolerance, bool* changed) const
{
    if (changed)
        *changed = false;
    QPainterPath const cut = pathOfOutlines(area);
    if (points.size() < 2 || !bounds().intersects(cut.boundingRect()))
        return {*this};
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
        piece.thickness.clear();        // (its places carry the thickness the line had there)
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

void Fill::giveContours(double tolerance)
{
    if (!contours.isEmpty() || outlines.isEmpty())
        return;
    for (QPolygonF const& outline : std::as_const(outlines))
        contours.push_back(contourOf(outline, tolerance));
    rebuild();
    // (An outline too small to make a curve of would leave the two out of step: then the outlines stay as they were.)
    if (contours.size() != outlines.size()) {
        contours.clear();
        for (QPolygonF const& outline : std::as_const(outlines))
            contours.push_back(contourOf(outline, -1.0));
        rebuild();
    }
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
    // (Auto Fill paint: all its line goes round, where the line crosses itself too.)
    if (link != 0) {
        for (QPolygonF const& o : outlines)
            if (o.containsPoint(p, Qt::WindingFill))
                return true;
        return false;
    }
    bool inside = false;
    for (QPolygonF const& o : outlines)
        if (o.containsPoint(p, Qt::OddEvenFill))
            inside = !inside;
    return inside;
}

void Fill::transform(QTransform const& t)
{
    textAt = t.map(textAt);
    if (!text.isEmpty() && t.isAffine())
        textShape = textShape * QTransform(t.m11(), t.m12(), t.m21(), t.m22(), 0.0, 0.0);
    gradFrom = t.map(gradFrom);
    gradTo = t.map(gradTo);
    seed = t.map(seed);
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
