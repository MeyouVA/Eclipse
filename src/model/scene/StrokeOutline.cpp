// Eclipse - the scene: the area a line covers, with its thickness, tips and joins.
#include "model/scene/Scene.h"

#include <QPainterPath>

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"

// The area a line covers.  The line is first thinned to the places that matter (where it
// bends or changes thickness).  Either side of it an edge is then stepped out by half the
// thickness - one place on each edge for each place on the line, so that neighbouring
// stretches share an edge exactly - and the line is cut into short ribbons, each gone round
// the same way; a disc rounds each tip and each sharp turn.  All of it is then made one
// shape, which also sorts out where the line crosses itself.
QVector<QPolygonF> Stroke::outlinePolygons() const
{
    QVector<QPolygonF> result;
    QVector<QPointF> all;
    QVector<double> allHalf;
    for (StrokePoint const& p : path()) {
        if (!all.isEmpty() && QLineF(all.last(), p.pos).length() < 1e-6)
            continue;  // a doubled point has no direction
        all.push_back(p.pos);
        allHalf.push_back(std::max(0.0, double(width) * p.pressure / 2.0));
    }
    if (all.isEmpty())
        return result;

    // Thinned: a place is dropped when the line and its thickness pass within a twentieth of
    // a unit of it anyway.
    QVector<QPointF> pos;
    QVector<double> half;
    {
        int const count = int(all.size());
        QVector<bool> keep(count, false);
        keep[0] = keep[count - 1] = true;
        QVector<QPair<int, int>> stack{{0, count - 1}};
        while (!stack.isEmpty()) {
            auto const [first, last] = stack.takeLast();
            double most = 0.05;
            int index = -1;
            for (int i = first + 1; i < last; ++i) {
                double const along = double(i - first) / (last - first);
                double const off = std::max(distanceToSegment(all[i], all[first], all[last]),
                                            std::abs(allHalf[i] - (allHalf[first] + (allHalf[last] - allHalf[first]) * along)));
                if (off > most) {
                    most = off;
                    index = i;
                }
            }
            if (index >= 0) {
                keep[index] = true;
                stack.push_back({first, index});
                stack.push_back({index, last});
            }
        }
        for (int i = 0; i < count; ++i)
            if (keep[i]) {
                pos.push_back(all[i]);
                half.push_back(allHalf[i]);
            }
    }
    int const n = int(pos.size());

    QPainterPath outline;
    outline.setFillRule(Qt::WindingFill);
    // Every piece goes round the same way, so that where two overlap they add up and never cancel.
    auto add = [&](QPolygonF piece) {
        double twice = 0.0;
        for (int k = 0; k < piece.size(); ++k)
            twice += piece[k].x() * piece[(k + 1) % piece.size()].y() - piece[(k + 1) % piece.size()].x() * piece[k].y();
        if (std::abs(twice) < 1e-9)
            return;
        if (twice < 0.0)
            std::reverse(piece.begin(), piece.end());
        outline.addPolygon(piece);
        outline.closeSubpath();
    };
    auto disc = [&](QPointF centre, double radius) {
        if (radius <= 1e-6)
            return;
        int const sides = std::clamp(int(std::ceil(radius * 1.5)) + 12, 16, 96);
        QPolygonF round;
        for (int k = 0; k < sides; ++k) {
            double const angle = 2.0 * 3.14159265358979323846 * k / sides;
            round << centre + QPointF(std::cos(angle), std::sin(angle)) * radius;
        }
        add(round);
    };
    if (n == 1) {
        disc(pos[0], std::max(half[0], 0.15));
    } else {
        auto unit = [](QPointF v) {
            double const length = std::hypot(v.x(), v.y());
            return length > 0.0 ? v / length : QPointF(1, 0);
        };
        auto normalOf = [](QPointF d) { return QPointF(-d.y(), d.x()); };
        bool const loop = closed && n > 2 && QLineF(pos[0], pos[n - 1]).length() < 1e-6;
        // For each place: where the two edges are as the line arrives there, and as it leaves.
        // Along a curve they are the same place (where the edges of the two stretches meet);
        // at a sharp turn they differ, and a disc covers the gap.
        QVector<QPointF> leftIn(n), leftOut(n), rightIn(n), rightOut(n);
        QVector<bool> sharp(n, false);
        for (int i = 0; i < n; ++i) {
            bool const hasBefore = i > 0 || loop, hasAfter = i + 1 < n || loop;
            QPointF const in = hasBefore ? unit(pos[i] - pos[i > 0 ? i - 1 : n - 2]) : unit(pos[1] - pos[0]);
            QPointF const out = hasAfter ? unit(pos[i + 1 < n ? i + 1 : 1] - pos[i]) : in;
            QPointF const n0 = normalOf(in), n1 = normalOf(out);
            double const turn = in.x() * out.x() + in.y() * out.y();    // the cosine of the turn
            QPointF const mid = unit(n0 + n1);
            double const along = mid.x() * n0.x() + mid.y() * n0.y();
            if (!hasBefore || !hasAfter || turn >= 0.94) {              // up to about 20 degrees
                double const reach = half[i] / std::max(along, 0.3);
                leftIn[i] = leftOut[i] = pos[i] + mid * reach;
                rightIn[i] = rightOut[i] = pos[i] - mid * reach;
            } else {
                sharp[i] = true;
                leftIn[i] = pos[i] + n0 * half[i];
                rightIn[i] = pos[i] - n0 * half[i];
                leftOut[i] = pos[i] + n1 * half[i];
                rightOut[i] = pos[i] - n1 * half[i];
            }
        }
        // Ribbons: a run of stretches as one piece, ended at a sharp turn, after a dozen
        // stretches, or once it has turned a third of the way round (a longer one could
        // fold over itself).
        int from = 0;
        double turned = 0.0;
        for (int i = 1; i < n; ++i) {
            if (i + 1 < n && !sharp[i]) {
                QPointF const in = unit(pos[i] - pos[i - 1]), out = unit(pos[i + 1] - pos[i]);
                turned += std::acos(std::clamp(in.x() * out.x() + in.y() * out.y(), -1.0, 1.0));
            }
            if (i + 1 < n && !sharp[i] && i - from < 12 && turned < 2.0)
                continue;
            QPolygonF ribbon;
            ribbon << leftOut[from];
            for (int k = from + 1; k <= i; ++k)
                ribbon << leftIn[k];
            for (int k = i; k > from; --k)
                ribbon << rightIn[k];
            ribbon << rightOut[from];
            add(ribbon);
            from = i;
            turned = 0.0;
        }
        // The sharp turns, finished as the line asks: rounded by a disc; or, on a line of one thickness, carried on to a
        // point (mitre - cut flat across when that point would be too far out) or cut flat across (bevel).
        bool const shaped = join != RoundJoin && uniform();
        for (int i = 0; i < n; ++i) {
            if (!sharp[i])
                continue;
            if (!shaped || half[i] <= 1e-6) {
                disc(pos[i], half[i]);
                continue;
            }
            for (int side : {1, -1}) {
                QPointF const a = side > 0 ? leftIn[i] : rightIn[i], b = side > 0 ? leftOut[i] : rightOut[i];
                QPolygonF piece;
                piece << pos[i] << a;
                if (join == MitreJoin) {
                    QPointF const n0 = (a - pos[i]) / half[i], n1 = (b - pos[i]) / half[i];
                    double const d = 1.0 + n0.x() * n1.x() + n0.y() * n1.y();
                    if (d > 1e-6) {
                        QPointF const tip = pos[i] + (n0 + n1) * (half[i] / d);
                        if (QLineF(pos[i], tip).length() <= kMitreLimit * width)
                            piece << tip;
                    }
                }
                piece << b;
                add(piece);
            }
        }
        if (!closed) {
            if (capStart == RoundCap)
                disc(pos[0], half[0]);
            if (capEnd == RoundCap)
                disc(pos[n - 1], half[n - 1]);
        } else if (!loop) {
            disc(pos[0], half[0]);
        }
    }
    double const speck = std::max(0.05, double(width) * double(width) * 0.002);
    for (QPolygonF polygon : outline.simplified().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        if (polygon.size() < 3)
            continue;
        double twice = 0.0;
        for (int k = 0; k < polygon.size(); ++k)
            twice += polygon[k].x() * polygon[(k + 1) % polygon.size()].y() - polygon[(k + 1) % polygon.size()].x() * polygon[k].y();
        if (std::abs(twice) / 2.0 >= speck)     // a sliver left by the arithmetic is not a hole
            result.push_back(polygon);
    }
    return result;
}
