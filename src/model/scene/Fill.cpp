// Eclipse - painted areas: contours, pieces, overlaps, cut and kept within areas, merged.
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

QVector<Fill> Fill::pieces(QVector<int> const& counts) const
{
    Fill blank = *this;
    blank.outlines.clear();
    blank.contours.clear();
    blank.text.clear();
    blank.textFont.clear();
    blank.textAt = QPointF();
    blank.textShape = QTransform();
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

QVector<Fill> Fill::within(QVector<QPolygonF> const& area, double tolerance) const
{
    QPainterPath const cut = pathOfOutlines(area);
    if (outlines.isEmpty() || !bounds().intersects(cut.boundingRect()))
        return {};
    QPainterPath const taken = pathOfOutlines(outlines).intersected(cut);
    double kept = 0.0;
    for (QPolygonF const& polygon : taken.toSubpathPolygons())
        kept += areaOfPolygon(polygon);
    if (kept < 0.01)
        return {};
    return areasOf(taken, *this, {this}, tolerance);
}

bool Fill::mergeWith(Fill const& other, double tolerance)
{
    if (!overlaps(other))
        return false;
    QVector<Fill> const joined = areasOf(pathOfOutlines(outlines).united(pathOfOutlines(other.outlines)), *this, {this, &other}, tolerance);
    if (joined.isEmpty())
        return false;
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
