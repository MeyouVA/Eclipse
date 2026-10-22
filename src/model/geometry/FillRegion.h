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

// The same within any part of the drawing's plane, not only the frame: for artwork that lies
// beside the frame, or on a layer the camera sees moved.
Result compute(Drawing const& drawing, QRectF const& area, QPointF point, double gap, QVector<QPolygonF>& outlines);
// The edge of an area as compute() gives it (quick, as the pixels have it) put on the middles of the lines round it
// (`walls`), as a vector: each place of it within `reach` of a line goes onto it.
QVector<QPolygonF> refine(QVector<QPolygonF> const& outlines, QVector<Stroke> const& walls, double reach = 3.5);
// All that the lines and brush shapes of a drawing take up, with room round it: where its areas to paint are.
QRectF paintArea(Drawing const& drawing, double margin = 24.0);

// Removes points that lie within `tolerance` of the line through their neighbours.
QPolygonF simplify(QPolygonF const& polygon, double tolerance);

}  // namespace FillRegion
