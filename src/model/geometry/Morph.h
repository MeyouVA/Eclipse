// Eclipse - morphing: the drawings in between two key drawings, made by the program.
//
// The shapes of the first drawing are matched with those of the second - line with line,
// painted area with painted area, sheet by sheet, in the order they were drawn (or, when
// the two drawings do not hold the same number, each with the nearest) - and every matched
// pair is drawn part of the way from the one to the other.  A shape with no partner shrinks
// away to nothing (or grows from nothing).
#pragma once

#include "model/scene/Scene.h"

namespace Morphing {

Drawing between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints = {});
// The same with morphing layers (Morph::groups): each one's shapes of `a` into its shapes of `b`, then the rest into the rest.
Drawing between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints, QVector<Morph::Group> const& groups);
// A morphing of a layer as a whole: its hints by their types, its quality, its morphing layers each on its own timing and
// along its deformation path, and its painted areas flattened.
Drawing between(Drawing const& a, Drawing const& b, double t, Morph const& morph);
// The hints the program suggests between two drawings: for each shape of the first with a partner in the second, its point
// furthest from its middle pinned to the point of the partner in the same direction (a Pencil hint on a line, else Contour).
QVector<QLineF> suggestedHints(Drawing const& a, Drawing const& b, QVector<int>* types = nullptr);
// The painted areas of each sheet of one colour (and no gradient) made one.
void flatten(Drawing& d);
// Where along a path (0 its start .. 1 its end, by length) a point is, and which way the path goes there (degrees).
QPointF alongPath(QPolygonF const& path, double t, double* angle = nullptr);
// How a morphing layer names a shape of a drawing: its sheet, whether it is a line (or a painted area), and its place there.
int shapeKey(int sheet, bool stroke, int index);

}  // namespace Morphing
