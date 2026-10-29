// Eclipse - the Camera view: the Static Camera Transform tool and Permanent Selection.
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QPainter>

void Canvas::setPermanentSelection(bool on)
{
    if (on == m_permanentSelection)
        return;
    m_permanentSelection = on;
    emit permanentSelectionChanged(on);
}

void Canvas::staticCameraOffset(QPointF& by, double& depth) const
{
    by = QPointF();
    depth = 0.0;
    Scene const& scene = m_document->scene();
    int const camera = scene.cameraIndex();
    if (camera < 0 || m_drag != DragStaticCamera)
        return;
    if (m_staticCameraDepth) {
        // Up the view is into the scene (as a layer dragged in depth goes away), two units a pixel.
        depth = -(m_splineNow.y() - m_splineFrom.y()) / std::max(m_zoom, 1e-6) * 2.0;
        return;
    }
    // The framing follows the pen: from the view to the camera's frame, to the scene, to what the camera hangs from.
    QTransform const toFrame = cameraViewTransform().inverted();
    QTransform const fromCamera = scene.cameraTransform(m_frame).inverted();
    int const parent = scene.parentIndex(camera);
    QTransform const intoParent = parent >= 0 ? scene.worldTransform(parent, m_frame).inverted() : QTransform();
    auto place = [&](QPointF widgetPos) { return intoParent.map(fromCamera.map(toFrame.map(widgetPos))); };
    by = place(m_splineNow) - place(m_splineFrom);
}

void Canvas::paintStaticCameraAid(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    if (scene.cameraIndex() < 0)
        return;
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPolygonF frame = cameraViewTransform().map(QRectF(0, 0, scene.width, scene.height));
    if (m_drag == DragStaticCamera && !m_staticCameraDepth)
        frame.translate(m_splineNow - m_splineFrom);
    QPen pen(QColor(255, 170, 40), 2.0, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(frame);
    if (m_drag == DragStaticCamera) {
        painter.setPen(QPen(QColor(255, 170, 40), 1.4));
        painter.drawLine(m_splineFrom, m_splineNow);
    }
    painter.restore();
}
