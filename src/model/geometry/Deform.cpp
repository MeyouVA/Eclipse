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

QVector<double> solveIK(QVector<QPointF> const& joints, QVector<double> bend, int endJoint, QPointF target)
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

Drawing deform(Drawing const& drawing, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth, QVector<SkinDab> const& skin)
{
    if (!isBent(joints, bend))
        return drawing;
    Chain const chain(joints, bend, smooth, skin);
    return mapped(drawing, [&chain](QPointF p) { return chain.map(p); });
}

}  // namespace Deform
