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

// The drawing `t` of the way (0..1) from `a` to `b`.  At 0 it is `a` itself and at 1 `b`.
Drawing between(Drawing const& a, Drawing const& b, double t);

}  // namespace Morphing
