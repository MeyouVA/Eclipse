#include "ui/views/PerspectiveView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"
#include "actions/theme/InterfaceColours.h"

namespace {
constexpr double kDefaultYaw = -32.0;
constexpr double kDefaultPitch = 18.0;
}

PerspectiveView::PerspectiveView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setMinimumSize(200, 150);
    setFocusPolicy(Qt::StrongFocus);
    connect(m_document, &Document::changed, this, qOverload<>(&QWidget::update));
}

void PerspectiveView::setCurrentCell(int layer, int frame)
{
    m_layer = layer;
    m_frame = frame;
    update();
}

void PerspectiveView::setOrbit(double yaw, double pitch)
{
    m_yaw = std::remainder(yaw, 360.0);
    m_pitch = std::clamp(pitch, -89.0, 89.0);
    update();
}

void PerspectiveView::resetView()
{
    m_yaw = kDefaultYaw;
    m_pitch = kDefaultPitch;
    m_zoom = 1.0;
    m_pan = QPointF();
    m_viewX = m_viewY = m_viewZ = 0.0;
    update();
}

QPointF PerspectiveView::layerPoint(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return QPointF();
    double x, y, z;
    scene.toSpace(layer, m_frame, scene.layers[layer].pivot, x, y, z);
    if (scene.layers[layer].kind == Layer::Camera) {
        // A camera: where it stands, behind the frame it sees.
        QPointF const middle = scene.flatTransform(layer, m_frame).map(QPointF(scene.width / 2.0, scene.height / 2.0));
        return project(middle.x(), middle.y(), scene.depthOf(layer, m_frame) + scene.focalDistance(m_frame));
    }
    return project(x, y, z);
}

int PerspectiveView::layerAt(QPointF widgetPos) const
{
    // The nearest to the eye first.
    Scene const& scene = m_document->scene();
    int found = -1;
    double nearest = 1e300;
    for (int l = 0; l < scene.layers.size(); ++l) {
        QPolygonF const quad = layerPolygon(l);
        if (quad.size() < 3 || !quad.containsPoint(widgetPos, Qt::OddEvenFill))
            continue;
        double x, y, z;
        scene.toSpace(l, m_frame, scene.layers[l].pivot, x, y, z);
        double const d = distanceOf(x, y, z);
        if (d < nearest) {
            nearest = d;
            found = l;
        }
    }
    return found;
}

void PerspectiveView::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter painter(this);
    painter.fillRect(rect(), QColor(58, 58, 58));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The camera: the frame it sees, on the plane it sees as it is, and the cone back to where it stands.
    int const camera = scene.cameraIndex();
    double const cameraDepth = camera >= 0 ? scene.depthOf(camera, m_frame) : 0.0, distance = scene.focalDistance(m_frame);
    QTransform const fromPicture = scene.cameraTransform(m_frame).inverted();
    QVector<Point> corners;
    for (QPointF const& corner : {QPointF(0, 0), QPointF(scene.width, 0), QPointF(scene.width, scene.height), QPointF(0, scene.height)}) {
        QPointF const at = fromPicture.map(corner);
        corners.push_back({at.x(), at.y(), cameraDepth});
    }
    QPolygonF const frame = projectPolygon(corners);
    QPointF const middle = fromPicture.map(QPointF(scene.width / 2.0, scene.height / 2.0));
    bool eyeSeen = true;
    QPointF const eye = project(middle.x(), middle.y(), cameraDepth + distance, &eyeSeen);
    QColor const blue = InterfaceColours::get("planningCamera");

    // What is drawn, the furthest from the eye first: the layers, and the frame among them.
    struct Item {
        int layer;      // -1: the frame of the camera
        double distance;
        QPolygonF polygon;
        QTransform projection;
        Drawing drawing;
    };
    QVector<Item> items;
    if (!frame.isEmpty())
        items.push_back({-1, distanceOf(middle.x(), middle.y(), cameraDepth), frame, {}, {}});
    for (int l = 0; l < scene.layers.size(); ++l) {
        QTransform projection;
        Drawing drawing;
        QPolygonF polygon = layerPolygon(l, &projection, &drawing);
        if (polygon.size() < 3)
            continue;
        double x, y, z;
        scene.toSpace(l, m_frame, scene.layers[l].pivot, x, y, z);
        items.push_back({l, distanceOf(x, y, z), std::move(polygon), projection, std::move(drawing)});
    }
    std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) { return a.distance > b.distance; });

    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    for (Item const& item : std::as_const(items)) {
        painter.resetTransform();
        if (item.layer < 0) {
            painter.setPen(QPen(blue, 1.6));
            painter.setBrush(QColor(blue.red(), blue.green(), blue.blue(), 22));
            painter.drawPolygon(frame);
            continue;
        }
        Layer const& layer = scene.layers[item.layer];
        QTransform const& onto = item.projection;
        QPolygonF const& quad = item.polygon;
        if (quad.size() < 3 || !onto.isInvertible())
            continue;
        Pose const pose = scene.poseOf(item.layer, m_frame);
        painter.save();
        QPainterPath clip;
        clip.addPolygon(quad);
        painter.setClipPath(clip);
        painter.setTransform(onto);
        Canvas::paintDrawing(painter, scene, item.drawing, layer.opacity * pose.opacity);
        painter.restore();
        bool const current = item.layer == m_layer;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(current ? QColor(255, 190, 70) : QColor(150, 150, 150, 150), current ? 1.6 : 1.0, current ? Qt::SolidLine : Qt::DotLine));
        painter.drawPolygon(quad);
        painter.setFont(small);
        painter.setPen(current ? QColor(255, 220, 150) : QColor(190, 190, 190));
        painter.drawText(quad[0] + QPointF(2, -4), layer.name);
    }

    // The cone, over everything, so that the camera is always found.
    painter.resetTransform();
    painter.setPen(QPen(QColor(blue.red(), blue.green(), blue.blue(), 170), 1.0));
    if (m_cameraCone)
        for (Point const& corner : std::as_const(corners)) {
            QLineF line;
            if (projectSegment({middle.x(), middle.y(), cameraDepth + distance}, corner, line))
                painter.drawLine(line);
        }
    painter.setPen(QPen(QColor(20, 30, 44), 1));
    painter.setBrush(camera >= 0 && camera == m_layer ? QColor(255, 255, 255) : blue);
    if (eyeSeen)
        painter.drawEllipse(eye, 5.5, 5.5);

    painter.setPen(QColor(140, 140, 140));
    painter.setFont(small);
    painter.drawText(QRectF(4, 2, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("Perspective"));
    if (camera < 0)
        painter.drawText(QRectF(4, height() - 20, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("No camera: the scene is seen as it is."));
}

void PerspectiveView::mousePressEvent(QMouseEvent* event)
{
    m_drag = event->button() == Qt::LeftButton ? Orbit : (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) ? Pan : None;
    m_last = m_pressed = event->position();
    m_moved = false;
}

void PerspectiveView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == None)
        return;
    QPointF const d = event->position() - m_last;
    m_last = event->position();
    if (QLineF(m_pressed, m_last).length() > 3.0)
        m_moved = true;
    if (!m_moved)
        return;
    if (m_drag == Orbit)
        setOrbit(m_yaw + d.x() * 0.4, m_pitch - d.y() * 0.4);
    else {
        m_pan += d;
        update();
    }
}

void PerspectiveView::mouseReleaseEvent(QMouseEvent* event)
{
    // A click that did not turn the view picks the layer under it.
    if (m_drag == Orbit && !m_moved && event->button() == Qt::LeftButton) {
        int const picked = layerAt(event->position());
        if (picked >= 0 && picked != m_layer)
            emit layerPicked(picked);
    }
    m_drag = None;
}

void PerspectiveView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && layerAt(event->position()) < 0)
        resetView();
}

void PerspectiveView::wheelEvent(QWheelEvent* event)
{
    double const factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    double const zoom = std::clamp(m_zoom * factor, 0.1, 20.0);
    // What is under the pointer stays under it.
    QPointF const centre(width() / 2.0, height() / 2.0);
    QPointF const from = event->position() - centre - m_pan;
    m_pan = event->position() - centre - from * (zoom / m_zoom);
    m_zoom = zoom;
    update();
    event->accept();
}
