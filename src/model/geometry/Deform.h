// Eclipse - bone deformation.
//
// A layer may have a chain of bones: joints, in layer coordinates, one bone between each
// two.  A pose turns each bone by some angle about the joint it starts from (carrying the
// rest of the chain with it), and the artwork bends along: every point follows the bones
// it lies near, more the nearer it is.
#pragma once

#include <QPointF>
#include <QTransform>
#include <QVector>

#include "model/scene/Scene.h"

namespace Deform {

// True when the chain has at least one bone and some bone is turned.
bool isBent(QVector<QPointF> const& joints, QVector<double> const& bend);

// Where the joints are once each bone is turned by its angle (degrees, clockwise on screen).
QVector<QPointF> posedJoints(QVector<QPointF> const& joints, QVector<double> const& bend);

Drawing deform(Drawing const& drawing, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth = 0.0,
               QVector<SkinDab> const& skin = {});

QTransform boneTransform(QVector<QPointF> const& joints, QVector<double> const& bend, int bone);

// The index of the bone a point at rest lies nearest to, or -1 when there are no bones.
int nearestBone(QVector<QPointF> const& joints, QPointF p);

QVector<double> solveIK(QVector<QPointF> const& joints, QVector<double> bend, int endJoint, QPointF target);

// One point, the same way.
QPointF deformPoint(QPointF p, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth = 0.0, QVector<SkinDab> const& skin = {});
// How much the painted weights add to the pull of bone `bone` at `p` (0 where none is painted; below 0 where it is taken away).
double paintedWeight(QVector<SkinDab> const& skin, int bone, QPointF p);

// ---- curve and envelope deformers ---------------------------------------------------------
// A deformer is a Bezier curve through a few points (see Layer::warp for how it is laid
// out).  Left open, the artwork bends along it as it is reshaped: every point of a drawing
// keeps its place along the curve and its distance to the side of it.  Closed, it is an
// envelope: the artwork inside is pulled about as the outline round it is.

// True when there is a deformer and a pose moves some point or handle of it.
bool isWarped(QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved);
// The points and handles of the deformer under a pose.
QVector<QPointF> posedWarp(QVector<QPointF> const& rest, QVector<QPointF> const& moved);
QPolygonF warpLine(QVector<QPointF> const& points, bool closed, int perSegment = 12);
// Gives point `index` the handles of a curve that passes smoothly through it.
void smoothWarpHandles(QVector<QPointF>& points, bool closed, int index);
int insertWarpPoint(QVector<QPointF>& points, bool closed, QPointF near, double within, double* along = nullptr);
QVector<QPointF> envelopeAround(Drawing const& drawing, int points = 8, double margin = 12.0, bool followOutline = false);
QVector<QPolygonF> outerEdges(Drawing const& drawing, int sheet = -1, double grow = 0.0);
// The drawing reshaped by the deformer.
Drawing warp(Drawing const& drawing, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved);
QPointF warpPoint(QPointF p, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved);

// `index`: which of the layer's drawings it is, for its moved points (-1: found from where `drawing` lies, if it is one of them).
Drawing shown(Drawing const& drawing, Layer const& layer, Pose const& pose, int index = -1);

// ---- point animation (see PointMove) ------------------------------------------------------
// The drawing with the points the moves give it moved; paint that has no contour of its own follows the lines round it.
Drawing movedPoints(Drawing const& drawing, int index, QVector<PointMove> const& moves);
bool movesPoints(QVector<PointMove> const& moves, int index);
// How each point of `moved` lies from where it is in `rest`; false when the two do not have the same lines and points.
bool pointMoves(Drawing const& rest, Drawing const& moved, int index, QVector<PointMove>& out);
// `all` with the moves of drawing `index` put in place of those it had.
QVector<PointMove> withMoves(QVector<PointMove> const& all, int index, QVector<PointMove> const& mine);

Drawing ridden(Scene const& scene, int layer, int frame, Drawing const& drawing);
Drawing bentOnBones(Scene const& scene, int layer, int frame, Drawing const& drawing);
QVector<QPointF> gameBoneJoints(Scene const& scene, int layer, int frame);

}
