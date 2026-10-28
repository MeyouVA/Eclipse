#include "ui/views/DepthView.h"
#include "ui/views/PlanningMotionPath.h"
#include "actions/theme/InterfaceColours.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QPainter>
#include <cmath>

void DepthView::paintMotionPath(QPainter& painter, double back, double front) const
{
    if (!m_canvas || !m_canvas->showsPlanningControls())
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
    for (PlanningPathPoint point : path.controls) {
        if (point.keyIndex == m_dragControlKey && point.controlIndex == m_dragControlIndex && std::abs(m_dragControlSlope) > 1e-9) {
            double const by = m_dragBy / m_dragControlSlope;
            point.x += point.depthX * by; point.y += point.depthY * by; point.z += point.depthZ * by;
        }
        painter.drawEllipse(projected(point), 4.5, 4.5);
    }
    painter.restore();
}


bool DepthView::beginMotionControlDrag(QPointF at)
{
    if (!m_canvas || !m_canvas->showsPlanningControls())
        return false;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    PlanningMotionPath const path = planningMotionPath(scene, m_layer);
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    QTransform const camera = scene.cameraTransform(m_frame);
    PlanningPathPoint const* picked = nullptr;
    double nearest = 9.0;
    for (PlanningPathPoint const& point : path.controls) {
        QPointF const xy = camera.map(QPointF(point.x, point.y));
        QPointF const screen = place(m_mode == Top ? xy.x() : xy.y(), point.z, back, front);
        double const distance = QLineF(at, screen).length();
        if (distance < nearest) { nearest = distance; picked = &point; }
    }
    if (!picked)
        return false;
    if (scene.layers[m_layer].locked)
        return true;
    m_canvas->pickControlPoint(picked->keyIndex, picked->controlIndex);
    if (m_canvas->noZDragging() || std::abs(picked->depthZ) < 1e-9)
        return true;
    m_heldBack = back; m_heldFront = front; m_held = true;
    m_dragControlKey = picked->keyIndex; m_dragControlIndex = picked->controlIndex;
    m_dragControlDepth = picked->localDepth; m_dragControlSlope = picked->depthZ;
    m_dragStart = depthAt(at); m_dragBy = 0.0;
    update();
    return true;
}
