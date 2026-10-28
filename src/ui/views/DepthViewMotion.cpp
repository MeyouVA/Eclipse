#include "ui/views/DepthView.h"
#include "ui/views/PlanningMotionPath.h"
#include "actions/theme/InterfaceColours.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QPainter>

void DepthView::paintMotionPath(QPainter& painter, double back, double front) const
{
    if (!m_canvas || !m_canvas->showsControl())
        return;
    Scene const& scene = m_document->scene();
    PlanningMotionPath const path = planningMotionPath(scene, m_layer);
    if (path.samples.isEmpty())
        return;
    QTransform const camera = scene.cameraTransform(m_frame);
    auto projected = [&](PlanningPathPoint const& point) {
        QPointF const xy = camera.map(QPointF(point.x, point.y));
        return place(m_mode == Top ? xy.x() : xy.y(), point.z, back, front);
    };
    QPolygonF line;
    for (PlanningPathPoint const& point : path.samples)
        line << projected(point);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 3.0));
    painter.drawPolyline(line);
    painter.setPen(QPen(InterfaceColours::get("cameraKeyframe"), 1.4));
    painter.drawPolyline(line);
    for (PlanningPathPoint const& point : path.samples) {
        if (!point.key)
            continue;
        QPointF const at = projected(point);
        painter.setPen(QPen(QColor(20, 20, 20), 1.0));
        painter.setBrush(InterfaceColours::get("cameraKeyframe"));
        painter.drawRect(QRectF(at - QPointF(3.5, 3.5), QSizeF(7, 7)));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(InterfaceColours::get("controlPoint"), 1.6));
    for (PlanningPathPoint const& point : path.controls)
        painter.drawEllipse(projected(point), 4.5, 4.5);
    painter.restore();
}
