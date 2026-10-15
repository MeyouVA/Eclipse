#include "model/geometry/Deform.h"

#include "model/geometry/Model3D.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>

namespace Deform {

bool isBent(QVector<QPointF> const& joints, QVector<double> const& bend)
{
    if (joints.size() < 2)
        return false;
    for (int i = 0; i < bend.size() && i < joints.size() - 1; ++i)
        if (bend[i] != 0.0)
            return true;
    return false;
}

namespace {

// What each bone does to the points that belong to it: from where the bone lies at rest
// to where the pose puts it.
struct Chain {
    QVector<QPointF> joints;
    QVector<QTransform> bones;
    double smooth = 0.0;    // how far round a joint the bend is shared out (scene units); 0: as good as a fold
    QVector<SkinDab> skin;  // weights painted for the bones

    Chain(QVector<QPointF> const& rest, QVector<double> const& bend, double smoothness = 0.0, QVector<SkinDab> const& painted = {})
        : joints(rest), smooth(smoothness), skin(painted)
    {
        QPointF origin = rest.isEmpty() ? QPointF() : rest[0];  // where the bone starts, posed
        double angle = 0.0;                                      // how far it is turned in all
        for (int i = 0; i + 1 < rest.size(); ++i) {
            angle += i < bend.size() ? bend[i] : 0.0;
            QTransform t;
            t.translate(origin.x(), origin.y());
            t.rotate(angle);
            t.translate(-rest[i].x(), -rest[i].y());
            bones.push_back(t);
            origin = t.map(rest[i + 1]);
        }
    }

    QPointF map(QPointF p) const
    {
        if (bones.isEmpty())
            return p;
        // Each bone pulls with a strength that falls off fast with distance, so a point
        // beside one bone follows that bone, and one at a joint goes half way with each.
        // The pull of each bone by nearness; then what is painted added to it (never below nothing).  Where the painting
        // takes every pull away, nearness alone says.
        QVarLengthArray<double, 16> pull(bones.size());
        nearness(p, pull);
        if (!skin.isEmpty()) {
            QVarLengthArray<double, 16> painted(bones.size());
            double total = 0.0;
            for (int i = 0; i < bones.size(); ++i) {
                painted[i] = std::max(0.0, pull[i] + 3.0 * paintedWeight(skin, i, p));
                total += painted[i];
            }
            if (total > 1e-12)
                for (int i = 0; i < bones.size(); ++i)
                    pull[i] = painted[i];
        }
        double total = 0.0;
        QPointF sum;
        for (int i = 0; i < bones.size(); ++i) {
            total += pull[i];
            sum += bones[i].map(p) * pull[i];
        }
        return total > 0.0 ? sum / total : p;
    }

    // How strongly each bone pulls on `p` by how near it is.
    void nearness(QPointF p, QVarLengthArray<double, 16>& pull) const
    {
        if (smooth > 1.0) {
            // Smoothed joints: the nearest bone pulls fully, another as much less as it is further than that one, and
            // not at all once it is `smooth` further - so the bend is shared out round a joint, and nowhere else.
            QVarLengthArray<double, 16> away(bones.size());
            double nearest = 1e300;
            for (int i = 0; i < bones.size(); ++i) {
                away[i] = distanceToSegment(p, joints[i], joints[i + 1]);
                nearest = std::min(nearest, away[i]);
            }
            for (int i = 0; i < bones.size(); ++i) {
                double const share = std::max(0.0, 1.0 - (away[i] - nearest) / smooth);
                pull[i] = share * share;
            }
            return;
        }
        for (int i = 0; i < bones.size(); ++i) {
            double const d = distanceToSegment(p, joints[i], joints[i + 1]);
            pull[i] = 1.0 / ((d * d + 1.0) * (d * d + 1.0));
        }
    }
};

// Adds the points from `a` towards `b` (not `b` itself), close enough together that the
// bent line stays smooth.
template <class Add> void pieces(QPointF a, QPointF b, Add add)
{
    int const steps = std::clamp(int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / 6.0)), 1, 400);
    for (int k = 0; k < steps; ++k)
        add(double(k) / steps);
}

}

QVector<QPointF> posedJoints(QVector<QPointF> const& joints, QVector<double> const& bend)
{
    Chain const chain(joints, bend);
    QVector<QPointF> out = joints;
    for (int i = 0; i < chain.bones.size(); ++i)
        out[i + 1] = chain.bones[i].map(joints[i + 1]);
    return out;
}

QTransform boneTransform(QVector<QPointF> const& joints, QVector<double> const& bend, int bone)
{
    if (bone < 0 || bone + 1 >= joints.size() || !isBent(joints, bend))
        return QTransform();
    return Chain(joints, bend).bones[bone];
}

int nearestBone(QVector<QPointF> const& joints, QPointF p)
{
    int best = -1;
    double nearest = 1e300;
    for (int i = 0; i + 1 < joints.size(); ++i) {
        double const d = distanceToSegment(p, joints[i], joints[i + 1]);
        if (d < nearest) {
            nearest = d;
            best = i;
        }
    }
    return best;
}

QVector<double> solveIK(QVector<QPointF> const& joints, QVector<double> bend, int endJoint, QPointF target)
{
    if (endJoint < 1 || endJoint >= joints.size())
        return bend;
    if (bend.size() < endJoint)
        bend.resize(endJoint);
    double const degrees = 180.0 / 3.14159265358979323846;
    // Cyclic coordinate descent: from the last bone back to the first, turn each so that
    // the end of the chain swings onto the line from its joint to the target; and again.
    for (int pass = 0; pass < 40; ++pass) {
        for (int i = endJoint - 1; i >= 0; --i) {
            QVector<QPointF> const posed = posedJoints(joints, bend);
            QPointF const toEnd = posed[endJoint] - posed[i];
            QPointF const toTarget = target - posed[i];
            if (std::hypot(toEnd.x(), toEnd.y()) < 1e-9 || std::hypot(toTarget.x(), toTarget.y()) < 1e-9)
                continue;
            double const turn = (std::atan2(toTarget.y(), toTarget.x()) - std::atan2(toEnd.y(), toEnd.x())) * degrees;
            bend[i] += std::remainder(turn, 360.0);
        }
        QPointF const end = posedJoints(joints, bend)[endJoint];
        if (std::hypot(end.x() - target.x(), end.y() - target.y()) < 0.01)
            break;
    }
    return bend;
}

double paintedWeight(QVector<SkinDab> const& skin, int bone, QPointF p)
{
    double sum = 0.0;
    for (SkinDab const& d : skin) {
        if (d.bone != bone || d.radius <= 0.0)
            continue;
        QPointF const off = p - d.at;
        double const t = (off.x() * off.x() + off.y() * off.y()) / (d.radius * d.radius);
        if (t < 1.0)
            sum += d.strength * (1.0 - t) * (1.0 - t);
    }
    return sum;
}

QPointF deformPoint(QPointF p, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth, QVector<SkinDab> const& skin)
{
    return Chain(joints, bend, smooth, skin).map(p);
}

namespace {

// The drawing with every point of it sent through `map`: lines and outlines are first cut
// into short pieces, so that what was straight can come out bent.
template <class Map> Drawing mapped(Drawing const& drawing, Map const& map)
{
    Drawing out;
    out.pinned = drawing.pinned;
    out.pictures = drawing.pictures;    // a picture is not bent: it stays as it lies
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill const& fill : drawing.fillsOf(sheet)) {
            Fill bent = fill;
            bent.outlines.clear();
            for (QPolygonF const& outline : fill.outlines) {
                QPolygonF polygon;
                for (int i = 0; i < outline.size(); ++i) {
                    QPointF const a = outline[i], b = outline[(i + 1) % outline.size()];
                    pieces(a, b, [&](double u) { polygon.push_back(map(a + (b - a) * u)); });
                }
                bent.outlines.push_back(polygon);
            }
            // A gradient placed in the area goes where the area goes.
            if (bent.gradPlaced) {
                bent.gradFrom = map(fill.gradFrom);
                bent.gradTo = map(fill.gradTo);
            }
            out.fillsOf(sheet).push_back(bent);
        }
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            Stroke bent = stroke;
            bent.curve = false;
            bent.closed = false;  // the path already comes back to its start
            bent.points.clear();
            QVector<StrokePoint> const line = stroke.path();
            for (int i = 0; i + 1 < line.size(); ++i) {
                StrokePoint const& a = line[i];
                StrokePoint const& b = line[i + 1];
                pieces(a.pos, b.pos, [&](double u) {
                    bent.points.push_back({map(a.pos + (b.pos - a.pos) * u), float(a.pressure + (b.pressure - a.pressure) * u)});
                });
            }
            if (!line.isEmpty())
                bent.points.push_back({map(line.last().pos), line.last().pressure});
            // Where it came out straight after all, the pieces are one piece again: a run of
            // points in a dead straight line is drawn with a small bump at each of them.
            QVector<StrokePoint> kept;
            for (StrokePoint const& q : bent.points) {
                while (kept.size() >= 2 && kept.last().pressure == q.pressure && kept[kept.size() - 2].pressure == q.pressure
                       && distanceToSegment(kept.last().pos, kept[kept.size() - 2].pos, q.pos) < 1e-6)
                    kept.removeLast();
                kept.push_back(q);
            }
            bent.points = kept;
            out.strokesOf(sheet).push_back(bent);
        }
    }
    return out;
}

}

Drawing deform(Drawing const& drawing, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth, QVector<SkinDab> const& skin)
{
    if (!isBent(joints, bend))
        return drawing;
    Chain const chain(joints, bend, smooth, skin);
    return mapped(drawing, [&chain](QPointF p) { return chain.map(p); });
}

// ---- curve and envelope deformers ---------------------------------------------------------

namespace {

double cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
double length(QPointF a) { return std::hypot(a.x(), a.y()); }

// What a deformer does to a point, worked out once for a whole drawing.
struct Warper {
    bool closed = false;
    QPolygonF rest, posed;          // the curve at rest and under the pose, piece for piece
    QVector<QTransform> pieces;     // an open curve: what each piece does to what lies beside it

    Warper(QVector<QPointF> const& restPoints, bool isClosed, QVector<QPointF> const& moved) : closed(isClosed)
    {
        rest = warpLine(restPoints, closed);
        posed = warpLine(posedWarp(restPoints, moved), closed);
        if (closed)
            return;
        for (int i = 0; i + 1 < rest.size(); ++i) {
            // From the piece at rest - along it, and to the side of it - to the piece as posed:
            // as much longer or shorter along it as the piece has become, as far to the side.
            QPointF const a = rest[i + 1] - rest[i], b = posed[i + 1] - posed[i];
            double const la = length(a), lb = length(b);
            if (la < 1e-9 || lb < 1e-9) {
                pieces.push_back(QTransform::fromTranslate(posed[i].x() - rest[i].x(), posed[i].y() - rest[i].y()));
                continue;
            }
            QPointF const ta = a / la, tb = b / lb;
            double const stretch = lb / la;
            // Columns: where the unit vector along the rest piece goes, and the one to its side.
            QTransform const fromRest(ta.x(), -ta.y(), ta.y(), ta.x(), 0, 0);       // into (along, side)
            QTransform const toPosed(tb.x() * stretch, tb.y() * stretch, -tb.y(), tb.x(), 0, 0);
            pieces.push_back(QTransform::fromTranslate(-rest[i].x(), -rest[i].y()) * fromRest * toPosed
                             * QTransform::fromTranslate(posed[i].x(), posed[i].y()));
        }
    }

    QPointF map(QPointF p) const { return closed ? inside(p) : beside(p); }

    // Along an open curve: each piece pulls with a strength that falls off fast with distance,
    // as the bones of a chain do, so the artwork follows the stretch of curve it lies by.
    QPointF beside(QPointF p) const
    {
        if (pieces.isEmpty())
            return p;
        double total = 0.0;
        QPointF sum;
        for (int i = 0; i < pieces.size(); ++i) {
            double const d = distanceToSegment(p, rest[i], rest[i + 1]);
            double const w = 1.0 / ((d * d + 1.0) * (d * d + 1.0));
            total += w;
            sum += pieces[i].map(p) * w;
        }
        return sum / total;
    }

    // In an envelope: mean value coordinates - the point is a weighted mean of the points of
    // the outline at rest, and goes to the same mean of where the pose has put them.
    QPointF inside(QPointF p) const
    {
        int const n = int(rest.size());
        if (n < 3 || posed.size() != n)
            return p;
        QVarLengthArray<QPointF, 256> s(n);
        QVarLengthArray<double, 256> r(n);
        for (int i = 0; i < n; ++i) {
            s[i] = rest[i] - p;
            r[i] = length(s[i]);
            if (r[i] < 1e-9)
                return posed[i];
        }
        QVarLengthArray<double, 256> area(n), along(n);
        for (int i = 0; i < n; ++i) {
            int const j = (i + 1) % n;
            area[i] = cross(s[i], s[j]) * 0.5;
            along[i] = dot(s[i], s[j]);
            if (std::abs(area[i]) < 1e-9 && along[i] < 0.0) {
                // On the outline itself, between two of its points.
                double const u = r[i] / (r[i] + r[j]);
                return posed[i] + (posed[j] - posed[i]) * u;
            }
        }
        double total = 0.0;
        QPointF sum;
        for (int i = 0; i < n; ++i) {
            int const before = (i + n - 1) % n, after = (i + 1) % n;
            double w = 0.0;
            if (std::abs(area[before]) > 1e-9)
                w += (r[before] - along[before] / r[i]) / area[before];
            if (std::abs(area[i]) > 1e-9)
                w += (r[after] - along[i] / r[i]) / area[i];
            total += w;
            sum += posed[i] * w;
        }
        return std::abs(total) > 1e-12 ? sum / total : p;
    }
};

}

bool isWarped(QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved)
{
    if (rest.size() / 3 < (closed ? 3 : 2))
        return false;
    for (int i = 0; i < moved.size() && i < rest.size(); ++i)
        if (!moved[i].isNull())
            return true;
    return false;
}

QVector<QPointF> posedWarp(QVector<QPointF> const& rest, QVector<QPointF> const& moved)
{
    QVector<QPointF> out = rest;
    for (int i = 0; i < moved.size() && i < out.size(); ++i)
        out[i] += moved[i];
    return out;
}

QPolygonF warpLine(QVector<QPointF> const& points, bool closed, int perSegment)
{
    QPolygonF line;
    int const n = int(points.size()) / 3;
    if (n < 2)
        return line;
    perSegment = std::max(perSegment, 1);
    int const stretches = closed ? n : n - 1;
    for (int i = 0; i < stretches; ++i) {
        int const j = (i + 1) % n;
        QPointF const p0 = points[i * 3], p1 = p0 + points[i * 3 + 2];
        QPointF const p3 = points[j * 3], p2 = p3 + points[j * 3 + 1];
        for (int k = 0; k < perSegment; ++k) {
            double const t = double(k) / perSegment, u = 1.0 - t;
            line.push_back(p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t));
        }
    }
    if (!closed)
        line.push_back(points[(n - 1) * 3]);
    return line;
}

void smoothWarpHandles(QVector<QPointF>& points, bool closed, int index)
{
    int const n = int(points.size()) / 3;
    if (index < 0 || index >= n)
        return;
    QPointF const here = points[index * 3];
    bool const hasBefore = closed ? n >= 2 : index > 0, hasAfter = closed ? n >= 2 : index + 1 < n;
    QPointF const before = hasBefore ? points[((index + n - 1) % n) * 3] : here;
    QPointF const after = hasAfter ? points[((index + 1) % n) * 3] : here;
    QPointF in, out;
    if (hasBefore && hasAfter && n > 2) {
        // Through the point in the direction from its one neighbour to the other, each handle
        // a third of the way to the neighbour on its side.
        QPointF const through = after - before;
        double const l = length(through);
        if (l > 1e-9) {
            in = -through / l * (length(before - here) / 3.0);
            out = through / l * (length(after - here) / 3.0);
        }
    } else {
        if (hasBefore)
            in = (before - here) / 3.0;
        if (hasAfter)
            out = (after - here) / 3.0;
    }
    points[index * 3 + 1] = in;
    points[index * 3 + 2] = out;
}

int insertWarpPoint(QVector<QPointF>& points, bool closed, QPointF near, double within, double* along)
{
    int const n = int(points.size()) / 3;
    if (n < 2)
        return -1;
    int const stretches = closed && n >= 3 ? n : n - 1;
    int best = -1;
    double bestT = 0.0, nearest = within;
    auto at = [&](int i, double t) {
        int const j = (i + 1) % n;
        QPointF const p0 = points[i * 3], p1 = p0 + points[i * 3 + 2], p3 = points[j * 3], p2 = p3 + points[j * 3 + 1];
        double const u = 1.0 - t;
        return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
    };
    for (int i = 0; i < stretches; ++i) {
        for (int k = 1; k < 64; ++k) {
            double const t = k / 64.0, d = length(at(i, t) - near);
            if (d < nearest) {
                nearest = d;
                best = i;
                bestT = t;
            }
        }
    }
    if (best < 0)
        return -1;
    // The stretch cut in two where the point goes: each half is the same curve as before.
    int const i = best, j = (i + 1) % n;
    double const t = bestT;
    QPointF const p0 = points[i * 3], p1 = p0 + points[i * 3 + 2], p3 = points[j * 3], p2 = p3 + points[j * 3 + 1];
    QPointF const a = p0 + (p1 - p0) * t, b = p1 + (p2 - p1) * t, c = p2 + (p3 - p2) * t;
    QPointF const d = a + (b - a) * t, e = b + (c - b) * t, f = d + (e - d) * t;
    points[i * 3 + 2] = a - p0;
    points[j * 3 + 1] = c - p3;
    int const place = i + 1;
    points.insert(place * 3, 3, QPointF());
    points[place * 3] = f;
    points[place * 3 + 1] = d - f;
    points[place * 3 + 2] = e - f;
    if (along)
        *along = t;
    return place;
}

QVector<QPolygonF> outerEdges(Drawing const& drawing, int sheet, double grow)
{
    // All that the artwork covers, as one area.
    QPainterPath all;
    all.setFillRule(Qt::WindingFill);
    for (int s = 0; s < Drawing::SheetCount; ++s) {
        if (sheet >= 0 && s != sheet)
            continue;
        for (Stroke const& stroke : drawing.strokesOf(s)) {
            QVector<StrokePoint> const line = stroke.path();
            if (line.size() < 2)
                continue;
            QPainterPath centre;
            float widest = 0.0f;
            centre.moveTo(line.first().pos);
            for (StrokePoint const& point : line) {
                centre.lineTo(point.pos);
                widest = std::max(widest, point.pressure);
            }
            if (stroke.closed) {
                centre.closeSubpath();
                QPainterPath inside = centre;
                inside.setFillRule(Qt::WindingFill);
                all = all.united(inside);       // what a closed line goes round is inside the drawing
            }
            if (stroke.width > 0.0f) {
                QPainterPathStroker stroker;
                stroker.setWidth(std::max(0.1, double(stroke.width) * widest));
                stroker.setJoinStyle(Qt::RoundJoin);
                stroker.setCapStyle(Qt::RoundCap);
                all = all.united(stroker.createStroke(centre));
            }
        }
        for (Fill const& fill : drawing.fillsOf(s)) {
            QPainterPath area;
            for (QPolygonF const& outline : fill.outlines)
                area.addPolygon(outline);
            area.closeSubpath();
            all = all.united(area);
        }
    }
    if (grow > 0.0) {
        // A band round it, twice as wide as it is to grow: half of that is outside.
        QPainterPathStroker band;
        band.setWidth(grow * 2.0);
        band.setJoinStyle(Qt::RoundJoin);
        all = all.united(band.createStroke(all));
    }
    // Its outer edges only.
    QList<QPolygonF> const edges = all.simplified().toSubpathPolygons();
    QVector<QPolygonF> outer;
    for (int i = 0; i < edges.size(); ++i) {
        if (edges[i].size() < 3)
            continue;
        bool inner = false;
        for (int k = 0; k < edges.size() && !inner; ++k)
            inner = k != i && edges[k].size() >= 3 && edges[k].boundingRect().contains(edges[i].boundingRect())
                    && edges[k].containsPoint(edges[i].first(), Qt::OddEvenFill);
        if (!inner)
            outer.push_back(edges[i]);
    }
    return outer;
}

QVector<QPointF> envelopeAround(Drawing const& drawing, int count, double margin, bool followOutline)
{
    // Every point of the artwork; then the hull round them all.
    QVector<QPointF> all;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill const& fill : drawing.fillsOf(sheet))
            for (QPolygonF const& outline : fill.outlines)
                for (QPointF const& p : outline)
                    all.push_back(p);
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            double const half = stroke.width / 2.0;
            for (StrokePoint const& p : stroke.path())
                for (QPointF const& side : {QPointF(-half, -half), QPointF(half, -half), QPointF(half, half), QPointF(-half, half)})
                    all.push_back(p.pos + side);
        }
    }
    for (Picture const& picture : drawing.pictures)
        for (QPointF const& p : QPolygonF(picture.bounds()))
            all.push_back(p);
    if (all.size() < 3 || count < 3)
        return {};
    std::sort(all.begin(), all.end(), [](QPointF const& p, QPointF const& q) { return p.x() < q.x() || (p.x() == q.x() && p.y() < q.y()); });
    QVector<QPointF> hull;
    for (int pass = 0; pass < 2; ++pass) {
        int const start = int(hull.size());
        for (int k = 0; k < all.size(); ++k) {
            QPointF const& p = pass == 0 ? all[k] : all[all.size() - 1 - k];
            while (hull.size() >= start + 2 && cross(hull[hull.size() - 1] - hull[hull.size() - 2], p - hull[hull.size() - 1]) <= 0)
                hull.removeLast();
            hull.push_back(p);
        }
        hull.removeLast();
    }
    if (hull.size() < 3)
        return {};
    bool outline = false;
    if (followOutline) {
        // Round the biggest piece of the artwork itself, instead of the hull.
        QPolygonF biggest;
        for (QPolygonF const& edge : outerEdges(drawing)) {
            QRectF const box = edge.boundingRect(), had = biggest.boundingRect();
            if (box.width() * box.height() > had.width() * had.height())
                biggest = edge;
        }
        if (biggest.size() > 1 && biggest.first() == biggest.last())
            biggest.removeLast();
        if (biggest.size() >= 3) {
            hull = biggest;
            outline = true;
        }
    }
    // Which way round it goes says which side of an edge is outside.
    double turned = 0.0;
    for (int i = 0; i < hull.size(); ++i)
        turned += cross(hull[i], hull[(i + 1) % hull.size()]);
    double around = 0.0;
    QPointF middle;
    for (int i = 0; i < hull.size(); ++i) {
        around += length(hull[(i + 1) % hull.size()] - hull[i]);
        middle += hull[i];
    }
    middle /= double(hull.size());
    if (around < 1e-6)
        return {};
    // So many points, evenly spaced round the hull, each pushed out a little from the middle.
    QVector<QPointF> points;
    int edge = 0;
    double walked = 0.0;
    for (int k = 0; k < count; ++k) {
        double const wanted = around * k / count;
        while (edge < hull.size() - 1 && walked + length(hull[(edge + 1) % hull.size()] - hull[edge]) < wanted) {
            walked += length(hull[(edge + 1) % hull.size()] - hull[edge]);
            ++edge;
        }
        QPointF const from = hull[edge], to = hull[(edge + 1) % hull.size()];
        double const side = length(to - from);
        QPointF p = side > 1e-9 ? from + (to - from) * std::clamp((wanted - walked) / side, 0.0, 1.0) : from;
        // Pushed out a little: from the middle for a hull; for an outline, square off its edge.
        QPointF out = p - middle;
        if (outline && side > 1e-9) {
            QPointF const along = (to - from) / side;
            out = turned > 0.0 ? QPointF(along.y(), -along.x()) : QPointF(-along.y(), along.x());
        }
        if (length(out) > 1e-9)
            p += out / length(out) * margin;
        points << p << QPointF() << QPointF();
    }
    for (int k = 0; k < count; ++k)
        smoothWarpHandles(points, true, k);
    return points;
}

QPointF warpPoint(QPointF p, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved)
{
    if (!isWarped(rest, closed, moved))
        return p;
    return Warper(rest, closed, moved).map(p);
}

Drawing warp(Drawing const& drawing, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved)
{
    if (!isWarped(rest, closed, moved))
        return drawing;
    Warper const warper(rest, closed, moved);
    return mapped(drawing, [&warper](QPointF p) { return warper.map(p); });
}

// Everything of a drawing moved by `t`.
static void transformAll(Drawing& drawing, QTransform const& t)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill& fill : drawing.fillsOf(sheet))
            fill.transform(t);
        for (Stroke& stroke : drawing.strokesOf(sheet))
            stroke.transform(t);
    }
    for (Picture& picture : drawing.pictures)
        picture.transform(t);
}

namespace {

// The Game Bones a layer hangs from (from the first of the chain to the one it hangs on), the joints of the chain as they
// stand on frame 0 in the first bone's own coordinates, how far each bone is turned on `frame` from how it stood then, and
// from the layer's own coordinates (on `frame`) into the first bone's.  False when it hangs from none.
bool gameChain(Scene const& scene, int layer, int frame, QVector<int>& chain, QVector<QPointF>& joints, QVector<double>& bend, QTransform& into)
{
    chain.clear();
    joints.clear();
    bend.clear();
    for (int b = scene.parentIndex(layer), guard = 0; b >= 0 && guard < 64 && scene.layers[b].kind == Layer::Bone; b = scene.parentIndex(b), ++guard)
        chain.push_back(b);
    if (chain.isEmpty())
        return false;
    std::reverse(chain.begin(), chain.end());      // from the first bone to the last
    auto local = [&scene](int l, int f) { return scene.layers[l].matrixFor(scene.layers[l].poseAt(f)); };
    // From each bone (on frame 0) to the first bone's own coordinates; and the drawing there - its own place on this frame,
    // the bones as they stand on frame 0.
    QVector<QTransform> toRoot(chain.size());
    for (int i = 0; i < chain.size(); ++i)
        toRoot[i] = i == 0 ? QTransform() : local(chain[i], 0) * toRoot[i - 1];
    into = local(layer, frame) * toRoot.last();
    for (int i = 0; i < chain.size(); ++i) {
        Layer const& bone = scene.layers[chain[i]];
        joints.push_back(toRoot[i].map(bone.pivot));
        bend.push_back(bone.poseAt(frame).rotation - bone.poseAt(0).rotation);
    }
    Layer const& last = scene.layers[chain.last()];
    joints.push_back(toRoot.last().map(last.pivot + QPointF(last.boneLength, 0.0)));
    return true;
}

}  // namespace

QVector<QPointF> gameBoneJoints(Scene const& scene, int layer, int frame)
{
    QVector<int> chain;
    QVector<QPointF> joints;
    QVector<double> bend;
    QTransform into;
    if (layer < 0 || layer >= scene.layers.size() || !gameChain(scene, layer, frame, chain, joints, bend, into) || !into.isInvertible())
        return {};
    QTransform const back = into.inverted();
    for (QPointF& j : joints)
        j = back.map(j);
    return joints;
}

Drawing bentOnBones(Scene const& scene, int layer, int frame, Drawing const& drawing)
{
    if (layer < 0 || layer >= scene.layers.size() || !scene.layers[layer].bendOnBones || scene.turned(layer, frame))
        return drawing;
    QVector<int> chain;
    QVector<QPointF> joints;
    QVector<double> bend;
    QTransform into;
    if (!gameChain(scene, layer, frame, chain, joints, bend, into) || !isBent(joints, bend))
        return drawing;
    int const root = chain.first();
    // The weights painted on the drawing, where the drawing is taken to.
    QVector<SkinDab> skin = scene.layers[layer].skin;
    double const grow = std::sqrt(std::abs(into.determinant()));
    for (SkinDab& d : skin) {
        d.at = into.map(d.at);
        d.radius *= grow;
    }
    // The first bone where it is on this frame, but turned as on frame 0: its turning is in the bending.
    PoseOverride over;
    over.layer = root;
    over.frame = frame;
    over.pose = scene.layers[root].poseAt(frame);
    over.pose.rotation = scene.layers[root].poseAt(0).rotation;
    QTransform const rootNow = scene.worldTransform(root, frame, &over), here = scene.worldTransform(layer, frame);
    if (!here.isInvertible())
        return drawing;
    Drawing out = drawing;
    transformAll(out, into);
    out = deform(out, joints, bend, scene.layers[layer].jointSmoothness, skin);
    transformAll(out, rootNow * here.inverted());
    return out;
}

Drawing ridden(Scene const& scene, int layer, int frame, Drawing const& drawing)
{
    if (layer < 0 || layer >= scene.layers.size())
        return drawing;
    Drawing out = bentOnBones(scene, layer, frame, drawing);
    QTransform const own = scene.worldTransform(layer, frame);
    int guard = 0;
    for (int a = scene.parentIndex(layer); a >= 0 && guard < 64; a = scene.parentIndex(a), ++guard) {
        Layer const& up = scene.layers[a];
        if (!up.hasWarp())
            continue;
        Pose const pose = up.poseAt(frame);
        if (!isWarped(up.warp, up.warpClosed, pose.warp))
            continue;
        // Into the coordinates of that layer, where its deformer lies; reshaped there; and back.
        QTransform const there = scene.worldTransform(a, frame);
        if (!there.isInvertible())
            continue;
        QTransform const into = own * there.inverted();
        if (!into.isInvertible())
            continue;
        transformAll(out, into);
        out = warp(out, up.warp, up.warpClosed, pose.warp);
        transformAll(out, into.inverted());
    }
    return out;
}

Drawing shown(Drawing const& drawing, Layer const& layer, Pose const& pose)
{
    if (!drawing.model.isEmpty()) {
        // A 3D model: a picture of it, turned as the layer is, under whatever is drawn over it.
        Drawing out = drawing;
        out.model.clear();
        if (Picture picture; Model3D::picture(drawing.model, pose.rotX, pose.rotY, picture)) {
            picture.transform(QTransform::fromTranslate(drawing.modelCentre.x(), drawing.modelCentre.y()));
            out.pictures.prepend(picture);
        }
        return shown(out, layer, pose);
    }
    bool const warped = isWarped(layer.warp, layer.warpClosed, pose.warp), bent = isBent(layer.bones, pose.bend);
    if (!warped && !bent)
        return drawing;
    if (!bent)
        return warp(drawing, layer.warp, layer.warpClosed, pose.warp);
    if (!warped)
        return deform(drawing, layer.bones, pose.bend, layer.jointSmoothness, layer.skin);
    return deform(warp(drawing, layer.warp, layer.warpClosed, pose.warp), layer.bones, pose.bend, layer.jointSmoothness, layer.skin);
}

}
