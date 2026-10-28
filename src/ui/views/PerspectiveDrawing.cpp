#include "ui/views/PerspectiveView.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include <QApplication>
#include <QKeyEvent>
#include <QTabletEvent>
#include <cmath>

void PerspectiveView::setDrawingCanvas(Canvas* canvas)
{
    if (m_drawingCanvas)
        disconnect(m_drawingCanvas, &Canvas::showControlChanged, this, nullptr);
    m_drawingCanvas = canvas;
    if (m_drawingCanvas)
        connect(m_drawingCanvas, &Canvas::showControlChanged, this, qOverload<>(&QWidget::update));
    update();
}

void PerspectiveView::cancelPlaneDrawing()
{
    if (m_planeEditor)
        m_planeEditor->cancelPlaneInput();
    m_drawingGesture = false;
}

bool PerspectiveView::drawingTool() const
{
    if (!m_drawingCanvas)
        return false;
    switch (m_drawingCanvas->tool()) {
    case Canvas::Brush: case Canvas::Pencil: case Canvas::Eraser: case Canvas::Line:
    case Canvas::Rectangle: case Canvas::Ellipse: case Canvas::Paint: case Canvas::PaintUnpainted:
    case Canvas::Repaint: case Canvas::Unpaint: case Canvas::RepaintBrush:
        return true;
    default: return false;
    }
}

bool PerspectiveView::canEditPlane() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || scene.layers[m_layer].id != m_facingLayer
        || !scene.layers[m_layer].holdsDrawings() || !scene.enabled(m_layer))
        return false;
    auto point = [&](QPointF local) {
        Point p; scene.toSpace(m_layer, m_frame, local, p.x, p.y, p.z);
        return viewPoint(p);
    };
    Point const a = point(QPointF()), b = point(QPointF(1, 0)), c = point(QPointF(0, 1));
    double const x = std::hypot(b.x - a.x, b.y - a.y), y = std::hypot(c.x - a.x, c.y - a.y);
    double const distance = 3.5 * std::max(scene.width, scene.height) - a.z;
    return x > 1e-9 && y > 1e-9 && std::abs(b.z - a.z) < x * 1e-6 && std::abs(c.z - a.z) < y * 1e-6
        && distance >= m_nearDistance && distance <= m_farDistance;
}

bool PerspectiveView::preparePlaneEditor()
{
    if (!m_drawingCanvas || !canEditPlane())
        return false;
    QTransform const projection = layerProjection(m_layer);
    if (!projection.isInvertible())
        return false;
    if (!m_planeEditor) {
        m_planeEditor = new Canvas(m_document, this);
        m_planeEditor->setObjectName(QStringLiteral("PerspectiveDrawingTools"));
        m_planeEditor->hide();
        m_planeEditor->setLeader(m_drawingCanvas);
        connect(m_planeEditor, &Canvas::message, this, &PerspectiveView::message);
    }
    m_planeEditor->followSettings(*m_drawingCanvas);
    m_planeEditor->resize(size());
    m_planeEditor->setCurrentCell(m_layer, m_frame);
    m_planeEditor->setPlaneProjection(projection);
    return true;
}

QPolygonF PerspectiveView::editingPolygon() const
{
    QTransform const projection = layerProjection(m_layer);
    if (!projection.isInvertible())
        return {};
    QTransform const back = projection.inverted();
    QVector<Point> polygon;
    QRectF const area = QRectF(rect()).adjusted(-2, -2, 2, 2);
    for (QPointF const& at : {area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()}) {
        QPointF const local = back.map(at);
        Point point; m_document->scene().toSpace(m_layer, m_frame, local, point.x, point.y, point.z);
        polygon.push_back(point);
    }
    return projectPolygon(polygon);
}

void PerspectiveView::onActionFaceSelectedToDraw()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings()) {
        emit message(tr("Select a drawing layer to face."));
        return;
    }
    auto world = [&](QPointF local) {
        Point p; scene.toSpace(m_layer, m_frame, local, p.x, p.y, p.z);
        return p;
    };
    Point const a = world(QPointF()), b = world(QPointF(1, 0)), c = world(QPointF(0, 1));
    Point const u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
    Point n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    double const length = std::hypot(n.x, n.y, n.z);
    if (length < 1e-12) {
        emit message(tr("This drawing plane is collapsed and cannot be faced."));
        return;
    }
    n.x /= length; n.y /= length; n.z /= length;
    constexpr double radians = 3.14159265358979323846 / 180.0;
    double const facing = -n.x * std::cos(m_pitch * radians) * std::sin(m_yaw * radians)
        + n.y * std::sin(m_pitch * radians) + n.z * std::cos(m_pitch * radians) * std::cos(m_yaw * radians);
    if (facing < 0) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }
    cancelPlaneDrawing();
    m_drag = None;
    setOrbit(std::atan2(-n.x, n.z) / radians, std::asin(std::clamp(n.y, -1.0, 1.0)) / radians);
    Layer const& layer = scene.layers[m_layer];
    Drawing const shown = Deform::ridden(scene, m_layer, m_frame,
        Deform::shown(layer.shownAt(m_frame), layer, scene.poseOf(m_layer, m_frame), layer.drawingAt(m_frame)));
    QPointF const centre = shown.isEmpty() ? layer.pivot : shown.bounds().center();
    Point const target = world(centre);
    double const eye = 3.5 * std::max(scene.width, scene.height);
    double const distance = eye > m_nearDistance && eye < m_farDistance ? eye : (m_nearDistance + m_farDistance) / 2.0;
    double const depth = eye - distance;
    m_viewX = target.x - scene.width / 2.0 - n.x * depth;
    m_viewY = target.y - scene.height / 2.0 - n.y * depth;
    m_viewZ = target.z - n.z * depth;
    m_pan = QPointF();
    m_facingLayer = layer.id;
    preparePlaneEditor();
    emit message(tr("Facing selected drawing. Use a drawing tool; Alt-drag to orbit."));
    update();
}

void PerspectiveView::tabletEvent(QTabletEvent* event)
{
    if (event->type() == QEvent::TabletPress && drawingTool() && preparePlaneEditor())
        m_drawingGesture = true;
    if (!m_drawingGesture) { event->ignore(); return; }
    if (!preparePlaneEditor()) { cancelPlaneDrawing(); event->accept(); update(); return; }
    QApplication::sendEvent(m_planeEditor, event);
    if (event->type() == QEvent::TabletRelease)
        m_drawingGesture = false;
    event->accept(); update();
}

bool PerspectiveView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && m_facingLayer >= 0) {
        int const key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Space) { event->accept(); return true; }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && m_planeEditor && m_planeEditor->bendingLine()) {
            event->accept(); return true;
        }
    }
    return QWidget::event(event);
}

void PerspectiveView::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_planeEditor && m_planeEditor->bendingLine()) {
        QApplication::sendEvent(m_planeEditor, event);
        update(); return;
    }
    if (event->key() == Qt::Key_Space) { m_spaceDown = true; event->accept(); return; }
    if (event->key() == Qt::Key_Escape && m_facingLayer >= 0) {
        bool const active = m_planeEditor && m_planeEditor->planeInputActive();
        cancelPlaneDrawing();
        if (!active) m_facingLayer = -1;
        update(); event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

void PerspectiveView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) { m_spaceDown = false; event->accept(); return; }
    QWidget::keyReleaseEvent(event);
}
