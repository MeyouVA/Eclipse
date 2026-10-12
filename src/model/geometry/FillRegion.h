// Eclipse - the paint bucket: finds the area enclosed by a drawing's lines around a point.
#pragma once

#include <QPolygonF>
#include <QVector>

#include "model/scene/Scene.h"

namespace FillRegion {

enum Result {
    Ok,
    OnLine,     // the point is on a line: nothing to fill
    NotClosed,  // the area leaks out to the edge of the frame
    Outside,    // the point is outside the frame
};

// The outlines (odd-even) of the area of `drawing` that contains `point`, bounded by its
// strokes.  Works on a raster of the frame, so the result is accurate to about a pixel;
// the area is grown slightly so that it reaches under the middle of the lines around it.
// `gap` closes openings in the line work up to that many pixels wide.
Result compute(Drawing const& drawing, int width, int height, QPointF point, double gap, QVector<QPolygonF>& outlines);

// Removes points that lie within `tolerance` of the line through their neighbours.
QPolygonF simplify(QPolygonF const& polygon, double tolerance);

}  // namespace FillRegion
