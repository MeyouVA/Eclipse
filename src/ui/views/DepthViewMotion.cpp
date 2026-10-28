#include "ui/views/DepthView.h"
#include "ui/views/PlanningMotionPath.h"
#include "actions/theme/InterfaceColours.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

void DepthView::paintMotionPath(QPainter& painter, double back, double front) const
{
    if (!m_canvas || !m_canvas->showsPlanningControls())
        return;
    Scene const& scene = m_document->scene();
    PlanningMotionPath path;
    if (m_dragControlKey >= 0 && !m_dragControlDelta.isNull() && m_layer >= 0 && m_layer < scene.layers.size()
        && m_dragControlKey + 1 < scene.layers[m_layer].keys.size()
        && m_dragControlIndex >= 0 && m_dragControlIndex < scene.layers[m_layer].keys[m_dragControlKey].controls.size()) {
        QPointF position;
        double depth = 0.0;
        motionControlDragTarget(position, depth);
        if (std::isfinite(position.x()) && std::isfinite(position.y()) && std::isfinite(depth)) {
            Scene preview = scene;
            Keyframe& key = preview.layers[m_layer].keys[m_dragControlKey];
            Keyframe const& next = preview.layers[m_layer].keys[m_dragControlKey + 1];
            QVector<double> before;
            key.pathLine(QPointF(key.pose.x, key.pose.y), QPointF(next.pose.x, next.pose.y), &before);
            key.controls[m_dragControlIndex] = position;
            QVector<double> after;
            key.pathLine(QPointF(key.pose.x, key.pose.y), QPointF(next.pose.x, next.pose.y), &after);
            double const natural = key.pose.z + (next.pose.z - key.pose.z) * after.value(m_dragControlIndex, 0.0);
            if (m_dragControlIndex < key.controlsZ.size() || std::abs(natural - depth) > 1e-9) {
                while (key.controlsZ.size() <= m_dragControlIndex)
                    key.controlsZ.push_back(key.pose.z + (next.pose.z - key.pose.z) * before.value(key.controlsZ.size(), 0.0));
                key.controlsZ[m_dragControlIndex] = depth;
            }
            path = planningMotionPath(preview, m_layer);
        }
    }
    if (path.samples.isEmpty())
        path = planningMotionPath(scene, m_layer);
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


void DepthView::motionControlDragTarget(QPointF& position, double& depth) const
{
    position = m_dragControlPosition;
    depth = m_dragControlDepth;
    auto addAxis = [&](int axis, double amount) {
        if (axis == 0) position.rx() += amount;
        else if (axis == 1) position.ry() += amount;
        else if (axis == 2) depth += amount;
    };
    if (m_dragControlCoordinate >= 0) addAxis(m_dragControlCoordinate, m_dragControlDelta.x());
    addAxis(m_dragControlSecond, m_dragControlDelta.y());
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
    m_heldBack = back; m_heldFront = front; m_held = true;
    m_dragControlKey = picked->keyIndex; m_dragControlIndex = picked->controlIndex;
    m_dragControlDepth = picked->localDepth; m_dragControlSlope = picked->depthZ;
    m_dragControlPosition = scene.layers[m_layer].keys[picked->keyIndex].controls[picked->controlIndex];
    m_dragControlStart = at; m_dragControlDelta = QPointF();
    auto screenOffset = [&](double x, double y, double z) {
        QPointF const base = camera.map(QPointF(picked->x, picked->y));
        QPointF const offset = camera.map(QPointF(picked->x + x, picked->y + y));
        return place(m_mode == Top ? offset.x() : offset.y(), picked->z + z, back, front)
               - place(m_mode == Top ? base.x() : base.y(), picked->z, back, front);
    };
    QPointF const xAxis = screenOffset(picked->xX, picked->xY, picked->xZ);
    QPointF const yAxis = screenOffset(picked->yX, picked->yY, picked->yZ);
    QPointF const zAxis = screenOffset(picked->depthX, picked->depthY, picked->depthZ);
    auto determinant = [](QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); };
    QPointF const axes[] = {xAxis, yAxis, zAxis};
    m_dragControlNoZ = m_canvas->noZDragging();
    if (m_dragControlNoZ) {
        double const ax = m_mode == Top ? xAxis.x() : xAxis.y();
        double const ay = m_mode == Top ? yAxis.x() : yAxis.y();
        double const zx = m_mode == Top ? xAxis.y() : xAxis.x();
        double const zy = m_mode == Top ? yAxis.y() : yAxis.x();
        double const depthLength = std::hypot(zx, zy);
        QPointF direction = depthLength > 1e-9 ? QPointF(zy / depthLength, -zx / depthLength) : QPointF(ax, ay);
        double const across = ax * direction.x() + ay * direction.y();
        if (std::abs(across) < 1e-9) {
            m_dragControlKey = m_dragControlIndex = -1; m_held = false;
            return true;
        }
        m_dragNoZCoefficients = direction / across;
        m_dragControlCoordinate = 0; m_dragControlSecond = 1;
        m_dragStart = depthAt(at); m_dragBy = 0.0;
        update();
        return true;
    }
    int const first = m_mode == Top ? 0 : 1, other = 1 - first;
    int const pairs[][2] = {{first, 2}, {other, 2}, {first, other}};
    double best = 0.0;
    m_dragControlCoordinate = -1; m_dragControlSecond = 2;
    for (auto const& pair : pairs) {
        double const area = std::abs(determinant(axes[pair[0]], axes[pair[1]]));
        if (area > best + 1e-9) { best = area; m_dragControlCoordinate = pair[0]; m_dragControlSecond = pair[1]; }
    }
    if (m_dragControlCoordinate < 0 && std::abs(m_dragControlSlope) < 1e-9) {
        m_dragControlKey = m_dragControlIndex = -1; m_held = false;
        return true;
    }
    m_dragControlAxis = m_dragControlCoordinate >= 0 ? axes[m_dragControlCoordinate] : QPointF();
    m_dragControlDepthAxis = axes[m_dragControlSecond];
    m_dragStart = depthAt(at); m_dragBy = 0.0;
    update();
    return true;
}
