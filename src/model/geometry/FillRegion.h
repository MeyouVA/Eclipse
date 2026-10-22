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

Result compute(Drawing const& drawing, int width, int height, QPointF point, double gap, QVector<QPolygonF>& outlines);

Result compute(Drawing const& drawing, QRectF const& area, QPointF point, double gap, QVector<QPolygonF>& outlines);
QVector<QPolygonF> refine(QVector<QPolygonF> const& outlines, QVector<Stroke> const& walls, double reach = 3.5);
// All that the lines and brush shapes of a drawing take up, with room round it: where its areas to paint are.
QRectF paintArea(Drawing const& drawing, double margin = 24.0);

// Removes points that lie within `tolerance` of the line through their neighbours.
QPolygonF simplify(QPolygonF const& polygon, double tolerance);

}  // namespace FillRegion
