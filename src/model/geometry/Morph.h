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
// How a morphing layer names a shape of a drawing: its sheet, whether it is a line (or a painted area), and its place there.
int shapeKey(int sheet, bool stroke, int index);

}  // namespace Morphing
