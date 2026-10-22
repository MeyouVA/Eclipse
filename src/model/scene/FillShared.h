// Eclipse - painted areas: what their files share.
#pragma once

#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include <QHash>
#include <QPainterPath>
#include <QSet>

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fill_shared {


inline QPainterPath pathOfOutlines(QVector<QPolygonF> const& outlines)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (QPolygonF const& outline : outlines) {
        path.addPolygon(outline);
        path.closeSubpath();
    }
    return path;
}

inline double areaOfPolygon(QPolygonF const& polygon)
{
    double twice = 0.0;
    for (int i = 0; i < polygon.size(); ++i)
        twice += polygon[i].x() * polygon[(i + 1) % polygon.size()].y() - polygon[(i + 1) % polygon.size()].x() * polygon[i].y();
    return std::abs(twice) / 2.0;
}

inline Stroke keptContour(QPolygonF const& polygon, QVector<Fill const*> const& before, double tolerance)
{
    using Key = std::pair<qint64, qint64>;
    auto key = [](QPointF p) { return Key(qRound64(p.x() * 4096.0), qRound64(p.y() * 4096.0)); };
    struct Anchor {
        int contour = -1;
        int index = -1;
    };
    QSet<Key> wasEdge;                  // every corner of the outlines there were
    QVector<Stroke const*> contours;
    QHash<Key, Anchor> anchorAt;        // the points of their contours, by where they are
    for (Fill const* fill : before)
        for (int o = 0; o < fill->outlines.size(); ++o) {
            for (QPointF const& p : fill->outlines[o])
                wasEdge.insert(key(p));
            if (o >= fill->contours.size() || !fill->contours[o].curve || !fill->contours[o].closed)
                continue;
            int const c = int(contours.size());
            contours.push_back(&fill->contours[o]);
            for (int a = 0; a < fill->contours[o].points.size(); ++a)
                if (fill->contours[o].points[a].handles)
                    anchorAt.insert(key(fill->contours[o].points[a].pos), {c, a});
        }
    int const n = int(polygon.size());
    QVector<int> at;
    QVector<Anchor> hits;
    for (int i = 0; i < n; ++i)
        if (auto const found = anchorAt.constFind(key(polygon[i])); found != anchorAt.cend()) {
            at.push_back(i);
            hits.push_back(*found);
        }
    int const count = int(at.size());
    if (count < 2 || n < 4 || tolerance < 0)
        return Fill::contourOf(polygon, tolerance);
    Stroke out;
    out.curve = true;
    out.closed = true;
    QPointF pendingIn;
    bool pendingCorner = false;
    for (int h = 0; h < count; ++h) {
        Anchor const a = hits[h], b = hits[(h + 1) % count];
        int const from = at[h], to = h + 1 < count ? at[h + 1] : at[0] + n;
        Stroke const& old = *contours[a.contour];
        int const m = int(old.points.size());
        StrokePoint anchor = old.points[a.index];
        anchor.pos = polygon[from];
        anchor.handles = true;
        anchor.in = pendingIn;
        anchor.corner = anchor.corner || pendingCorner;
        // The same piece of an old contour, either way round, with nothing new in between.
        bool untouched = a.contour == b.contour && m > 2;
        for (int k = from + 1; untouched && k < to; ++k)
            untouched = wasEdge.contains(key(polygon[k % n]));
        bool const forward = untouched && (a.index + 1) % m == b.index, backward = untouched && (b.index + 1) % m == a.index;
        if (forward || backward) {
            anchor.out = forward ? old.points[a.index].out : old.points[a.index].in;
            pendingIn = forward ? old.points[b.index].in : old.points[b.index].out;
            pendingCorner = false;
            out.points.push_back(anchor);
            continue;
        }
        // A new stretch: fitted, and where it meets what was kept, a corner (as a cut makes one).
        Stroke stretch;
        for (int k = from; k <= to; ++k)
            stretch.points.push_back({polygon[k % n], 1.0f});
        stretch.fit(tolerance, 55.0);
        if (!stretch.curve || stretch.points.size() < 2) {
            stretch.points.clear();
            for (int k = from; k <= to; ++k)
                stretch.points.push_back({polygon[k % n], 1.0f, true, true, QPointF(), QPointF()});
        }
        anchor.out = stretch.points.first().out;
        anchor.corner = true;
        out.points.push_back(anchor);
        for (int k = 1; k + 1 < stretch.points.size(); ++k)
            out.points.push_back(stretch.points[k]);
        pendingIn = stretch.points.last().in;
        pendingCorner = true;
    }
    out.points.first().in = pendingIn;
    out.points.first().corner = out.points.first().corner || pendingCorner;
    return out;
}

inline QVector<Fill> areasOf(QPainterPath const& result, Fill const& like, QVector<Fill const*> const& before, double tolerance)
{
    QVector<QPolygonF> polygons;
    for (QPolygonF polygon : result.simplified().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        if (polygon.size() >= 3 && areaOfPolygon(polygon) > 0.01)
            polygons.push_back(polygon);
    }
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
        piece.texture = like.texture;
        piece.textureAmount = like.textureAmount;
        piece.textureCrisp = like.textureCrisp;
        piece.textureLow = like.textureLow;
        piece.textureHigh = like.textureHigh;
        for (int m = 0; m < members.size(); ++m) {
            if (plain) {
                piece.outlines.push_back(olds[m].outline);
            } else if (known[m] && olds[m].hasContour) {
                piece.contours.push_back(olds[m].contour);
            } else {
                piece.contours.push_back(known[m] ? Fill::contourOf(olds[m].outline, tolerance) : keptContour(polygons[members[m]], before, tolerance));
            }
        }
        piece.rebuild();
        if (!piece.outlines.isEmpty())
            areas.push_back(piece);
    }
    return areas;
}


}  // namespace fill_shared
