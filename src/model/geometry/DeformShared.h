// Eclipse - deformers: what their files share.
#pragma once

#include "model/geometry/Deform.h"

#include "model/geometry/Model3D.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>

namespace Deform {


struct Chain {
    QVector<QPointF> joints;
    QVector<QTransform> bones;
    double smooth = 0.0;    // how far round a joint the bend is shared out (scene units); 0: as good as a fold
    QVector<SkinDab> skin;  // weights painted for the bones
    ChainOptions const* options = nullptr;
    QVector<double> turns;  // each bone's turn (degrees), for the muscles and folds

    Chain(QVector<QPointF> const& rest, QVector<double> const& bend, double smoothness = 0.0, QVector<SkinDab> const& painted = {},
          ChainOptions const* opts = nullptr)
        : joints(rest), smooth(smoothness), skin(painted), options(opts), turns(bend)
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
        QPointF const bent = bend(scaled(p));
        double const amount = options ? std::clamp(options->amount, 0.0, 1.0) : 1.0;
        return amount >= 1.0 ? bent : p + (bent - p) * amount;
    }

    // Deformation Scale and Wave: the point moved across the skeleton (at rest) by the chain's scaling where it lies along it.
    QPointF scaled(QPointF p) const
    {
        bool const muscles = options && (options->muscleLeft != 0.0 || options->muscleRight != 0.0);
        if (!options || (options->scale.plain() && !muscles) || joints.size() < 2)
            return p;
        ChainScale const& k = options->scale;
        double total = 0.0;
        for (int i = 0; i + 1 < joints.size(); ++i)
            total += std::hypot(joints[i + 1].x() - joints[i].x(), joints[i + 1].y() - joints[i].y());
        if (total <= 1e-9)
            return p;
        int best = 0;
        double nearest = 1e300, bestT = 0.0, before = 0.0, bestBefore = 0.0;
        for (int i = 0; i + 1 < joints.size(); ++i) {
            QPointF const a = joints[i], d = joints[i + 1] - joints[i];
            double const len2 = d.x() * d.x() + d.y() * d.y();
            double const t = len2 > 0.0 ? std::clamp(((p - a).x() * d.x() + (p - a).y() * d.y()) / len2, 0.0, 1.0) : 0.0;
            QPointF const c = a + d * t;
            double const dist = std::hypot(p.x() - c.x(), p.y() - c.y());
            if (dist < nearest) {
                nearest = dist;
                best = i;
                bestT = t;
                bestBefore = before;
            }
            before += std::sqrt(len2);
        }
        QPointF const a = joints[best], d = joints[best + 1] - joints[best];
        double const len = std::hypot(d.x(), d.y());
        if (len <= 1e-9)
            return p;
        QPointF const dir = d / len, normal(-dir.y(), dir.x());
        QPointF const c = a + d * bestT;
        double const across = (p - c).x() * normal.x() + (p - c).y() * normal.y();
        bool const left = across < 0.0;
        double const along = bestBefore + bestT * len, s = along / total;
        bool const leftSide = left || k.uniform;
        double const start = leftSide ? k.leftStart : k.rightStart, end = leftSide ? k.leftEnd : k.rightEnd;
        double factor = start + (end - start) * s;
        if (k.curved) {
            // Along a Bezier from (0, start) to (1, end) through the two handles: the place s found on it, then its scale.
            double const p1 = leftSide ? k.leftStartHandlePos : k.rightStartHandlePos, v1 = leftSide ? k.leftStartHandleScale : k.rightStartHandleScale;
            double const p2 = leftSide ? k.leftEndHandlePos : k.rightEndHandlePos, v2 = leftSide ? k.leftEndHandleScale : k.rightEndHandleScale;
            auto bez = [](double a, double b, double c, double d, double t) {
                double const u = 1.0 - t;
                return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
            };
            double lo = 0.0, hi = 1.0;
            for (int i = 0; i < 40; ++i) {
                double const mid = (lo + hi) / 2.0;
                (bez(0.0, p1, p2, 1.0, mid) < s ? lo : hi) = mid;
            }
            factor = bez(start, v1, v2, end, (lo + hi) / 2.0);
        }
        bool const leftWave = left || k.waveSameAsLeft;
        double const amp = leftWave ? k.waveLeft : k.waveRight;
        double const period = std::max(1.0, leftWave ? k.periodLeft : k.periodRight);
        double const offset = leftWave ? k.offsetLeft : k.offsetRight;
        if (amp != 0.0) {
            // Over its span only, growing in at its start and dying out at its end.
            double const span = leftWave ? k.spanLeft : k.spanRight;
            double const into = along - offset, length = span > 0.0 ? span : total - offset;
            double strength = into < 0.0 || into > length ? 0.0 : 1.0;
            double const fadeIn = leftWave ? k.fadeInLeft : k.fadeInRight, fadeOut = leftWave ? k.fadeOutLeft : k.fadeOutRight;
            if (strength > 0.0 && fadeIn > 0.0)
                strength = std::min(strength, into / fadeIn);
            if (strength > 0.0 && fadeOut > 0.0)
                strength = std::min(strength, (length - into) / fadeOut);
            factor *= 1.0 + amp * std::clamp(strength, 0.0, 1.0) * std::sin(2.0 * 3.14159265358979323846 * into / period);
        }
        // Auto Muscle: near each joint that turns, the inner side of the bend swells, more the more it turns.
        if (muscles) {
            double jointAlong = 0.0;
            for (int j = 1; j + 1 < joints.size(); ++j) {
                jointAlong += std::hypot(joints[j].x() - joints[j - 1].x(), joints[j].y() - joints[j - 1].y());
                double const turn = j < turns.size() ? turns[j] : 0.0;
                if (turn == 0.0)
                    continue;
                bool const innerRight = turn > 0.0;      // (a turn clockwise on the screen bends towards the right side)
                if (innerRight == left)
                    continue;
                double const away = std::abs(along - jointAlong) / std::max(1.0, options->muscleSpan);
                if (away >= 1.0)
                    continue;
                double const amount = left ? options->muscleLeft : options->muscleRight;
                factor *= 1.0 + amount * std::min(1.0, std::abs(turn) / 90.0) * (1.0 - away * away);
            }
        }
        // Scaled from a line so far out on that side (Offset Deformer), with so much added (Offset Scaling).
        double const from = left ? -(leftSide ? k.offsetDeformerLeft : k.offsetDeformerRight) : (k.uniform ? k.offsetDeformerLeft : k.offsetDeformerRight);
        double const added = left || k.uniform ? k.offsetScalingLeft : k.offsetScalingRight;
        double const distance = across - from;
        double const moved = from + distance * factor + (distance < 0.0 ? -added : added);
        return p + normal * (moved - across);
    }

    QPointF bend(QPointF p) const
    {
        QVarLengthArray<double, 16> pull(bones.size());
        nearness(p, pull);
        // Fold: each point goes with the bone on its side of the line half way between two bones (no overlapping at the
        // joint); Auto Fold only once some joint turns far enough.
        if (options && options->fold != 0 && joints.size() >= 3) {
            bool doFold = options->fold == 1;
            for (int k = 1; k < turns.size() && !doFold; ++k)
                doFold = std::abs(turns[k]) >= options->foldAngle;
            if (doFold) {
                int bone = 0;
                for (int k = 1; k + 1 < joints.size(); ++k) {
                    QPointF const a = joints[k] - joints[k - 1], b = joints[k + 1] - joints[k];
                    double const la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
                    if (la < 1e-9 || lb < 1e-9)
                        break;
                    QPointF const n = a / la + b / lb;
                    if ((p - joints[k]).x() * n.x() + (p - joints[k]).y() * n.y() < 0.0)
                        break;
                    bone = k;
                }
                for (int i = 0; i < bones.size(); ++i)
                    pull[i] = i == bone ? 1.0 : 0.0;
            }
        }
        if (options && !options->influences.isEmpty())
            for (int i = 0; i < bones.size(); ++i)
                if (i < options->influences.size())
                    pull[i] *= zoneWeight(joints, i, options->influences[i], p);
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

template <class Add> void pieces(QPointF a, QPointF b, Add add, double piece = 6.0)
{
    int const steps = std::clamp(int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / std::max(0.5, piece))), 1, 2000);
    for (int k = 0; k < steps; ++k)
        add(double(k) / steps);
}



// How pencil lines are followed when they are bent (see ChainOptions): the length of the pieces, thickness, smoothing, fit.
struct LineFollow {
    double piece = 6.0;
    bool preserveThickness = true;
    int smooth = 0;
    double fitError = 0.0;
};

template <class Map> Drawing mapped(Drawing const& drawing, Map const& map, LineFollow const& follow = {})
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
                    pieces(a, b, [&](double u) { polygon.push_back(map(a + (b - a) * u)); }, follow.piece);
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
            bent.thickness.clear();     // (its places carry the thickness)
            QVector<StrokePoint> const line = stroke.path();
            for (int i = 0; i + 1 < line.size(); ++i) {
                StrokePoint const& a = line[i];
                StrokePoint const& b = line[i + 1];
                pieces(a.pos, b.pos, [&](double u) {
                    QPointF const at = a.pos + (b.pos - a.pos) * u;
                    float pressure = float(a.pressure + (b.pressure - a.pressure) * u);
                    QPointF const there = map(at);
                    if (!follow.preserveThickness) {
                        // The line as thick as the drawing is stretched across it there.
                        QPointF const way = b.pos - a.pos;
                        double const length = std::hypot(way.x(), way.y());
                        if (length > 1e-9) {
                            QPointF const across(-way.y() / length, way.x() / length);
                            QPointF const side = map(at + across) - there;
                            pressure *= float(std::clamp(std::hypot(side.x(), side.y()), 0.05, 20.0));
                        }
                    }
                    bent.points.push_back({there, pressure});
                }, follow.piece);
            }
            if (!line.isEmpty())
                bent.points.push_back({map(line.last().pos), line.last().pressure});
            // Smoothed (its ends kept), then simplified as far as the fit allows.
            for (int pass = 0; pass < follow.smooth && bent.points.size() > 2; ++pass) {
                QVector<StrokePoint> const was = bent.points;
                for (int i = 1; i + 1 < was.size(); ++i)
                    bent.points[i].pos = (was[i - 1].pos + was[i].pos * 2.0 + was[i + 1].pos) / 4.0;
            }
            double const tolerance = std::max(1e-6, follow.fitError);
            QVector<StrokePoint> kept;
            for (StrokePoint const& q : bent.points) {
                while (kept.size() >= 2 && std::abs(kept.last().pressure - q.pressure) <= (follow.fitError > 0.0 ? 0.02f : 0.0f)
                       && std::abs(kept[kept.size() - 2].pressure - q.pressure) <= (follow.fitError > 0.0 ? 0.02f : 0.0f)
                       && distanceToSegment(kept.last().pos, kept[kept.size() - 2].pos, q.pos) < tolerance)
                    kept.removeLast();
                kept.push_back(q);
            }
            bent.points = kept;
            out.strokesOf(sheet).push_back(bent);
        }
    }
    return out;
}


}  // namespace Deform
