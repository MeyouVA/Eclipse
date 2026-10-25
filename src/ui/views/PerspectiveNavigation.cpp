// Eclipse - navigation and framing in the Perspective view.
#include "ui/views/PerspectiveView.h"
#include "model/document/Commands.h"

#include <algorithm>
#include <cmath>

void PerspectiveView::onActionResetPan()
{
    m_pan = QPointF();
    m_viewX = m_viewY = m_viewZ = 0.0;
    update();
}

void PerspectiveView::moveView(double by)
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    double const yaw = m_yaw * radians, pitch = m_pitch * radians;
    m_viewX += by * std::cos(pitch) * std::sin(yaw);
    m_viewY -= by * std::sin(pitch);
    m_viewZ -= by * std::cos(pitch) * std::cos(yaw);
    update();
}

void PerspectiveView::onActionMoveForward()
{
    Scene const& scene = m_document->scene();
    moveView(std::max(scene.width, scene.height) * 0.1);
}

void PerspectiveView::onActionMoveBackward()
{
    Scene const& scene = m_document->scene();
    moveView(-std::max(scene.width, scene.height) * 0.1);
}

void PerspectiveView::onActionLookAtSelected()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[m_layer];
    QPointF centre = layer.pivot;
    int const drawing = layer.drawingAt(m_frame);
    if (drawing >= 0 && drawing < layer.drawings.size() && !layer.drawings[drawing].isEmpty())
        centre = layer.drawings[drawing].bounds().center();
    double x, y, z;
    scene.toSpace(m_layer, m_frame, centre, x, y, z);
    if (layer.kind == Layer::Camera)
        z += scene.focalDistance(m_frame);
    constexpr double radians = 3.14159265358979323846 / 180.0;
    double const yaw = m_yaw * radians, pitch = m_pitch * radians;
    double const nx = -std::cos(pitch) * std::sin(yaw), ny = std::sin(pitch), nz = std::cos(pitch) * std::cos(yaw);
    double depth = nx * (x - scene.width / 2.0 - m_viewX) + ny * (y - scene.height / 2.0 - m_viewY) + nz * (z - m_viewZ);
    if (depth >= 3.5 * std::max(scene.width, scene.height) * 0.95)
        depth = 0.0;
    m_viewX = x - scene.width / 2.0 - nx * depth;
    m_viewY = y - scene.height / 2.0 - ny * depth;
    m_viewZ = z - nz * depth;
    m_pan = QPointF();
    update();
}

void PerspectiveView::onActionFrameSelected()
{
    QPolygonF const quad = layerPolygon(m_layer);
    if (quad.isEmpty())
        return;
    QRectF const box = quad.boundingRect();
    double const factor = std::min(width() * 0.7 / std::max(box.width(), 1.0), height() * 0.7 / std::max(box.height(), 1.0));
    double const zoom = std::clamp(m_zoom * factor, 0.1, 20.0);
    m_pan = (m_pan - box.center() + QPointF(width() / 2.0, height() / 2.0)) * (zoom / m_zoom);
    m_zoom = zoom;
    update();
}
