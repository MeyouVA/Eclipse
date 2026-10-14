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

// The drawing bent along the chain.  Lines come back as plain runs of short straight pieces.
// `smooth` (scene units): how far from a joint the bend is shared out between the two bones, so the drawing bends round
// it instead of folding there (Layer::jointSmoothness); 0 or less: within about a unit.
// `skin`: weights painted for the bones (Layer::skin), added to (or taken from) what nearness says.
Drawing deform(Drawing const& drawing, QVector<QPointF> const& joints, QVector<double> const& bend, double smooth = 0.0,
               QVector<SkinDab> const& skin = {});

// What bone `bone` does to what belongs to it: from where it lies at rest to where the pose
// puts it.  Identity when there is no such bone.
QTransform boneTransform(QVector<QPointF> const& joints, QVector<double> const& bend, int bone);

// The index of the bone a point at rest lies nearest to, or -1 when there are no bones.
int nearestBone(QVector<QPointF> const& joints, QPointF p);

// Inverse kinematics: the angles that bring joint `endJoint` as near to `target` as the
// chain can reach, turning only the bones before it and starting from `bend`.
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
// The curve drawn out as short straight pieces, `perSegment` to each stretch between two
// points (a closed one does not repeat its first point at the end).
QPolygonF warpLine(QVector<QPointF> const& points, bool closed, int perSegment = 12);
// Gives point `index` the handles of a curve that passes smoothly through it.
void smoothWarpHandles(QVector<QPointF>& points, bool closed, int index);
// A new point on the curve where it passes nearest to `near` (no further off than
// `within`), with handles such that the curve keeps its shape.  Returns the place of the
// new point among the points, or -1; `along` is told how far between its two neighbours it lies (0..1).
int insertWarpPoint(QVector<QPointF>& points, bool closed, QPointF near, double within, double* along = nullptr);
// An envelope round a drawing: so many points on a loop a little outside all of it.
// Empty when the drawing has nothing to go round.
// (`followOutline`: round the outline of the artwork itself, into its hollows, instead of the hull round it.)
QVector<QPointF> envelopeAround(Drawing const& drawing, int points = 8, double margin = 12.0, bool followOutline = false);
// The outer edges of what the artwork of a drawing covers - of one sheet, or (-1) of all of
// them: each a closed outline; what an edge goes round, gaps in the middle too, is inside it.
// (`grow`: the edges are that far out from the artwork all round.)
QVector<QPolygonF> outerEdges(Drawing const& drawing, int sheet = -1, double grow = 0.0);
// The drawing reshaped by the deformer.
Drawing warp(Drawing const& drawing, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved);
QPointF warpPoint(QPointF p, QVector<QPointF> const& rest, bool closed, QVector<QPointF> const& moved);

// A drawing of a layer as a pose shows it: reshaped by the deformer of the layer, then
// bent along its bones.
Drawing shown(Drawing const& drawing, Layer const& layer, Pose const& pose);

// A drawing of a layer attached to layers with a deformer that is reshaped (its parent, its parent's parent ...): reshaped
// by each of those deformers too, as if it were drawn on that layer - it rides on the curve.  As it was when there is none.
Drawing ridden(Scene const& scene, int layer, int frame, Drawing const& drawing);
// A drawing of a layer that bends on the Game Bones it hangs from (Layer::bendOnBones): bent along the chain, given back
// in the layer's own coordinates.  As it was when the layer does not bend, or does not hang from bones.
Drawing bentOnBones(Scene const& scene, int layer, int frame, Drawing const& drawing);
// The joints of the chain of Game Bones a layer hangs from, as they stand on frame 0, in the layer's own coordinates on
// `frame` (where weights for them are painted); none when it hangs from no bones.
QVector<QPointF> gameBoneJoints(Scene const& scene, int layer, int frame);

}
