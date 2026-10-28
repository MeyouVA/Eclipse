#include "ui/views/PerspectiveView.h"
#include "ui/views/PlanningMotionPath.h"
#include "actions/theme/InterfaceColours.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QPainter>

void PerspectiveView::paintMotionPath(QPainter& painter) const
{
    if (!m_drawingCanvas || !m_drawingCanvas->showsPlanningControls())
        return;
    PlanningMotionPath const path = planningMotionPath(m_document->scene(), m_layer);
    if (path.samples.isEmpty())
        return;
    QVector<QLineF> segments;
    for (int i = 1; i < path.samples.size(); ++i) {
        PlanningPathPoint const& a = path.samples[i - 1];
        PlanningPathPoint const& b = path.samples[i];
        QLineF line;
        if (projectSegment({a.x, a.y, a.z}, {b.x, b.y, b.z}, line))
            segments.push_back(line);
    }
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 3.0));
    painter.drawLines(segments);
    painter.setPen(QPen(InterfaceColours::get("cameraKeyframe"), 1.4));
    painter.drawLines(segments);
    for (PlanningPathPoint const& point : path.samples) {
        if (!point.key)
            continue;
        bool seen = false;
        QPointF const at = project(point.x, point.y, point.z, &seen);
        if (!seen)
            continue;
        painter.setPen(QPen(QColor(20, 20, 20), 1.0));
        painter.setBrush(InterfaceColours::get("cameraKeyframe"));
        painter.drawRect(QRectF(at - QPointF(3.5, 3.5), QSizeF(7, 7)));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(InterfaceColours::get("controlPoint"), 1.6));
    for (PlanningPathPoint const& point : path.controls) {
        bool seen = false;
        QPointF const at = project(point.x, point.y, point.z, &seen);
        if (seen)
            painter.drawEllipse(at, 4.5, 4.5);
    }
    painter.restore();
}
