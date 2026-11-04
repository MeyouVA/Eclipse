// Eclipse - bone deformers: chains posed, inverse kinematics, painted weights, drawings bent.
#include "model/geometry/Deform.h"

#include "model/geometry/Model3D.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include "model/geometry/DeformShared.h"

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

QVector<double> solveIK(QVector<QPointF> const& joints, QVector<double> bend, int endJoint, QPointF target, QVector<double> const& stiffness)
{
    if (endJoint < 1 || endJoint >= joints.size())
        return bend;
    if (bend.size() < endJoint)
        bend.resize(endJoint);
    double const degrees = 180.0 / 3.14159265358979323846;
    for (int pass = 0; pass < 40; ++pass) {
        for (int i = endJoint - 1; i >= 0; --i) {
            QVector<QPointF> const posed = posedJoints(joints, bend);
            QPointF const toEnd = posed[endJoint] - posed[i];
            QPointF const toTarget = target - posed[i];
            if (std::hypot(toEnd.x(), toEnd.y()) < 1e-9 || std::hypot(toTarget.x(), toTarget.y()) < 1e-9)
                continue;
            double const turn = (std::atan2(toTarget.y(), toTarget.x()) - std::atan2(toEnd.y(), toEnd.x())) * degrees;
            bend[i] += std::remainder(turn, 360.0) * (1.0 - std::clamp(stiffness.value(i, 0.0), 0.0, 1.0));
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

QPointF deformPoint(QPointF p, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth, QVector<SkinDab> const& skin,
                    ChainOptions const* options)
{
    return Chain(joints, bend, smooth, skin, options).map(p);
}

Drawing deform(Drawing const& drawing, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth, QVector<SkinDab> const& skin,
               ChainOptions const* options)
{
    bool const scaled = options && !options->scale.plain() && joints.size() >= 2;
    if ((!isBent(joints, bend) && !scaled) || (options && options->amount <= 0.0))
        return drawing;
    Chain const chain(joints, bend, smooth, skin, options);
    LineFollow follow;
    if (options) {
        static double const kPieces[4] = {10.0, 6.0, 3.0, 1.5};
        follow.piece = kPieces[std::clamp(options->lineQuality, 0, 3)];
        follow.preserveThickness = options->preserveThickness;
        follow.smooth = options->lineSmooth;
        follow.fitError = options->fitError;
    }
    Drawing out = mapped(drawing, [&chain](QPointF p) { return chain.map(p); }, follow);
    if (!options || (!options->shapeAware && !options->overlayRigidity))
        return out;
    // Shape Aware / the rigidity map: a line or area that is to keep its shape goes where its points went, as a whole - the
    // similarity (moved, turned, scaled) that best fits where its points were taken.
    QVector<QPolygonF> rigid;
    if (options->overlayRigidity)
        for (Fill const& f : drawing.fillsOf(Drawing::OverlaySheet))
            for (QPolygonF const& ring : f.outlines)
                rigid.push_back(ring);
    auto keepsShape = [&](QRectF const& box) {
        if (options->shapeAware && !options->overlayRigidity)
            return true;
        for (QPolygonF const& ring : rigid)
            if (ring.containsPoint(box.center(), Qt::OddEvenFill))
                return true;
        return false;
    };
    auto fit = [&](QVector<QPointF> const& from) {
        QVector<QPointF> to;
        for (QPointF const& p : from)
            to.push_back(chain.map(p));
        QPointF a, b;
        for (int i = 0; i < from.size(); ++i) {
            a += from[i];
            b += to[i];
        }
        a /= std::max<qsizetype>(1, from.size());
        b /= std::max<qsizetype>(1, from.size());
        double dot = 0.0, cross = 0.0, norm = 0.0;
        for (int i = 0; i < from.size(); ++i) {
            QPointF const p = from[i] - a, q = to[i] - b;
            dot += p.x() * q.x() + p.y() * q.y();
            cross += p.x() * q.y() - p.y() * q.x();
            norm += p.x() * p.x() + p.y() * p.y();
        }
        QTransform t;
        if (norm < 1e-12)
            return QTransform::fromTranslate(b.x() - a.x(), b.y() - a.y());
        double const sc = dot / norm, sn = cross / norm;
        // x' = sc x - sn y, y' = sn x + sc y about the middles.
        return QTransform::fromTranslate(-a.x(), -a.y()) * QTransform(sc, sn, -sn, sc, 0, 0) * QTransform::fromTranslate(b.x(), b.y());
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Stroke> const& was = drawing.strokesOf(sheet);
        QVector<Stroke>& now = out.strokesOf(sheet);
        for (int i = 0; i < was.size() && i < now.size(); ++i) {
            if (!keepsShape(was[i].bounds()) || was[i].points.isEmpty())
                continue;
            QVector<QPointF> places;
            for (StrokePoint const& sp : was[i].path())
                places.push_back(sp.pos);
            Stroke kept = was[i];
            kept.transform(fit(places));
            now[i] = kept;
        }
        QVector<Fill> const& wasFills = drawing.fillsOf(sheet);
        QVector<Fill>& nowFills = out.fillsOf(sheet);
        for (int i = 0; i < wasFills.size() && i < nowFills.size(); ++i) {
            if (!keepsShape(wasFills[i].bounds()) || wasFills[i].outlines.isEmpty())
                continue;
            QVector<QPointF> places;
            for (QPolygonF const& ring : wasFills[i].outlines)
                for (QPointF const& p : ring)
                    places.push_back(p);
            QTransform const t = fit(places);
            Fill kept = wasFills[i];
            kept.transform(t);
            nowFills[i] = kept;
        }
    }
    return out;
}

QPointF shownPoint(Layer const& layer, Pose const& pose, QPointF p)
{
    if (pose.deform <= 0.0)
        return p;
    QPointF at = p;
    if (isHandled(layer.pointDeformer, pose.handles)) {
        QVector<QPointF> handles = pose.handles;
        for (QPointF& h : handles)
            h *= std::min(1.0, pose.deform);
        at = handlePoint(layer.pointDeformer, handles, at);
    }
    if (isWarped(layer.warp, layer.warpClosed, pose.warp)) {
        QVector<QPointF> moved = pose.warp;
        for (QPointF& m : moved)
            m *= std::min(1.0, pose.deform);
        at = warpPoint(at, layer.warp, layer.warpClosed, moved, layer.warpCurve, layer.warpIntervals);
    }
    if (layer.bones.size() >= 2) {
        ChainOptions const options = optionsOf(layer, pose);
        at = deformPoint(at, layer.bones, pose.bend, layer.jointSmoothness, layer.skin, &options);
    }
    return at;
}

QTransform trackerTransform(Layer const& parent, Pose const& parentPose, QVector<QPointF> const& trackers, int sampling)
{
    if (trackers.isEmpty())
        return QTransform();
    QVector<QPointF> now;
    for (QPointF const& t : trackers)
        now.push_back(shownPoint(parent, parentPose, t));
    QPointF const a = trackers[0], a2 = now[0];
    if (sampling == Layer::OnePoint || trackers.size() < 2)
        return QTransform::fromTranslate(a2.x() - a.x(), a2.y() - a.y());
    QPointF const b = trackers[1], b2 = now[1];
    if (sampling == Layer::ThreePoint && trackers.size() >= 3) {
        // The affine map that takes the three trackers where they have gone.
        QPointF const c = trackers[2], c2 = now[2];
        double const det = (b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y());
        if (std::abs(det) > 1e-9) {
            // Solve for m11 m21 dx, and m12 m22 dy, from x' = m11 x + m21 y + dx and y' = m12 x + m22 y + dy.
            auto solve = [&](double pa, double pb, double pc, double& u, double& v, double& w) {
                u = ((pb - pa) * (c.y() - a.y()) - (pc - pa) * (b.y() - a.y())) / det;
                v = ((b.x() - a.x()) * (pc - pa) - (c.x() - a.x()) * (pb - pa)) / det;
                w = pa - u * a.x() - v * a.y();
            };
            double m11, m21, dx, m12, m22, dy;
            solve(a2.x(), b2.x(), c2.x(), m11, m21, dx);
            solve(a2.y(), b2.y(), c2.y(), m12, m22, dy);
            return QTransform(m11, m12, m21, m22, dx, dy);
        }
    }
    // Two points: turned as the line between them turns (and scaled as it grows, with scaling), about the first.
    QPointF const was = b - a, is = b2 - a2;
    double const turn = std::atan2(is.y(), is.x()) - std::atan2(was.y(), was.x());
    double const lengthWas = std::hypot(was.x(), was.y()), lengthIs = std::hypot(is.x(), is.y());
    double const grow = sampling == Layer::TwoPointScale && lengthWas > 1e-9 ? lengthIs / lengthWas : 1.0;
    QTransform t;
    t.translate(a2.x(), a2.y());
    t.rotateRadians(turn);
    t.scale(grow, grow);
    t.translate(-a.x(), -a.y());
    return t;
}

ChainOptions optionsOf(Layer const& layer, Pose const& pose)
{
    ChainOptions o;
    o.influences = layer.influences;
    o.scale = layer.chainScale;
    o.amount = pose.deform;
    o.muscleLeft = layer.muscleLeft;
    o.muscleRight = layer.muscleRight;
    o.muscleSpan = layer.muscleSpan;
    o.fold = layer.fold;
    o.shapeAware = layer.shapeAware;
    o.overlayRigidity = layer.overlayRigidity;
    o.foldAngle = layer.foldAngle;
    o.lineQuality = layer.lineDeformQuality;
    o.preserveThickness = layer.linePreserveThickness;
    o.lineSmooth = layer.lineDeformSmooth;
    o.fitError = layer.lineDeformFitError;
    return o;
}

namespace {

// How far `p` lies outside the polygon (0 inside).
double outsideOf(QPolygonF const& shape, QPointF p)
{
    if (shape.size() < 3 || shape.containsPoint(p, Qt::OddEvenFill))
        return 0.0;
    double nearest = 1e300;
    for (int i = 0; i < shape.size(); ++i)
        nearest = std::min(nearest, distanceToSegment(p, shape[i], shape[(i + 1) % shape.size()]));
    return nearest;
}

double faded(BoneInfluence const& zone, double outside)
{
    if (outside <= 0.0)
        return 1.0;
    if (zone.fade <= 0.0)
        return 0.0;
    double const t = 1.0 - outside / zone.fade;
    return t <= 0.0 ? 0.0 : std::pow(t, std::max(0.05, zone.exponent));
}

}  // namespace

double zoneWeight(QVector<QPointF> const& joints, int bone, BoneInfluence const& zone, QPointF p)
{
    switch (zone.type) {
    case BoneInfluence::Zero:
        return 0.0;
    case BoneInfluence::Shaped:
        return zone.shape.size() >= 3 ? faded(zone, outsideOf(zone.shape, p)) : 1.0;
    case BoneInfluence::Elliptic: {
        if (bone < 0 || bone + 1 >= joints.size())
            return 1.0;
        QPointF const a = joints[bone], d = joints[bone + 1] - joints[bone];
        double const len = std::hypot(d.x(), d.y());
        if (len <= 1e-9)
            return 1.0;
        QPointF const dir = d / len, normal(-dir.y(), dir.x());
        double const u = (p - a).x() * dir.x() + (p - a).y() * dir.y();
        double const v = (p - a).x() * normal.x() + (p - a).y() * normal.y();
        double const across = v < 0.0 ? zone.left : zone.rightRadius();
        double const beyond = u < 0.0 ? -u : u > len ? u - len : 0.0;
        double const cap = u < 0.0 ? zone.begin : zone.end;
        // Inside the ellipse round the bone (a stadium: the bone's length, then half-ellipses at its ends)?
        double const du = beyond > 0.0 ? (cap > 0.0 ? beyond / cap : 1e9) : 0.0;
        double const dv = across > 0.0 ? std::abs(v) / across : (v == 0.0 ? 0.0 : 1e9);
        double const e = std::hypot(du, dv);
        if (e <= 1.0)
            return zone.fadeInside ? std::pow(std::max(0.0, 1.0 - e), std::max(0.05, zone.exponent)) : 1.0;
        // How far past the edge, in scene units (along the ray from the bone).
        double const reach = std::hypot(beyond, v);
        return faded(zone, reach - reach / e);
    }
    default:
        return 1.0;
    }
}

QPolygonF zoneShape(QVector<QPointF> const& joints, int bone, BoneInfluence const& zone, int points)
{
    QPolygonF shape;
    if (bone < 0 || bone + 1 >= joints.size())
        return shape;
    QPointF const a = joints[bone], b = joints[bone + 1], d = b - a;
    double const len = std::hypot(d.x(), d.y());
    if (len <= 1e-9)
        return shape;
    QPointF const dir = d / len, normal(-dir.y(), dir.x());
    double const pi = 3.14159265358979323846;
    int const half = std::max(4, points / 2);
    // Round the end (right side to left side), then round the start.
    for (int i = 0; i <= half; ++i) {
        double const t = -pi / 2.0 + pi * i / half;
        double const side = std::sin(t) < 0.0 ? zone.left : zone.rightRadius();
        shape << b + dir * (std::cos(t) * zone.end) + normal * (std::sin(t) * side);
    }
    for (int i = 0; i <= half; ++i) {
        double const t = pi / 2.0 + pi * i / half;
        double const side = std::sin(t) < 0.0 ? zone.left : zone.rightRadius();
        shape << a + dir * (std::cos(t) * zone.begin) + normal * (std::sin(t) * side);
    }
    return shape;
}

}  // namespace Deform
