#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include <QPainter>

void Canvas::setPlaneProjection(QTransform const& projection)
{
    m_planeView = true;
    m_drawingView = true;
    m_fitted = true;
    m_planeProjection = projection;
    QPointF const origin = projection.map(QPointF());
    double const x = QLineF(origin, projection.map(QPointF(1, 0))).length();
    double const y = QLineF(origin, projection.map(QPointF(0, 1))).length();
    m_zoom = std::clamp(std::sqrt(x * y), 0.001, 1000.0);
}

void Canvas::cancelPlaneInput()
{
    if (!m_planeView)
        return;
    cancelInput();
}

void Canvas::paintPlane(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    if (!m_planeView || m_layer < 0 || m_layer >= scene.layers.size())
        return;
    bool const transforming = m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate || m_drag == DragPoint
        || m_drag == DragHandle || m_drag == DragSegment || m_drag == DragCeBox;
    bool const erasing = (m_drag == DragErase && (m_eraserCuts || bitmapNow()) && m_eraseAny)
        || m_drag == DragGradient || m_drag == DragThickness
        || ((m_drag == DragInk || m_drag == DragSmooth || m_drag == DragRepaint || m_drag == DragRetouch) && m_inked)
        || m_drag == DragEnvelope;
    bool const previous = m_previewAllArt;
    m_previewAllArt = true;
    painter.save();
    paintViewLayer(painter, m_layer, QRectF(0, 0, scene.width, scene.height), m_planeProjection, transforming, erasing, [](int) {});
    paintSelectionAids(painter);
    painter.restore();
    m_previewAllArt = previous;
}
