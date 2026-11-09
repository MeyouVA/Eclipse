#include "board/ui/BoardCameraCanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <cmath>
#include <numbers>

Pose boardCameraLayerPose(BoardCameraPose const& pose)
{
    Pose result;
    result.x = pose.offset.x();
    result.y = pose.offset.y();
    result.scaleX = result.scaleY = 1.0 / pose.zoom;
    result.rotation = pose.rotation;
    return result;
}

BoardCameraCanvas::BoardCameraCanvas(Document* document, QWidget* parent) : Canvas(document, parent)
{
    setFramePresentation(38.0, QColor(164, 164, 164), false);
}

void BoardCameraCanvas::setCameraTool(bool on)
{
    cancelDrag();
    m_cameraTool = on;
    if (on)
        setCursor(Qt::OpenHandCursor);
    else
        unsetCursor();
    update();
}

void BoardCameraCanvas::setCameraGuide(bool on)
{
    m_cameraGuide = on;
    update();
}

void BoardCameraCanvas::setCameraFrame(QSize size, BoardCameraPose const& pose, bool hasKeys, int frame)
{
    cancelDrag();
    m_frameSize = size;
    m_pose = pose;
    m_hasKeys = hasKeys;
    m_frame = frame;
    update();
}

QPolygonF BoardCameraCanvas::framePolygon() const
{
    QRectF const area{QPointF(), QSizeF(m_frameSize)};
    QTransform const transform = Layer::matrix(boardCameraLayerPose(m_pose), area.center());
    return QList<QPointF>{mapFromScene(transform.map(area.topLeft())), mapFromScene(transform.map(area.topRight())),
                          mapFromScene(transform.map(area.bottomRight())), mapFromScene(transform.map(area.bottomLeft()))};
}

QPointF BoardCameraCanvas::rotationHandle(QPolygonF const& frame) const
{
    QPointF const top = (frame[0] + frame[1]) / 2.0;
    QPointF const out = top - (frame[0] + frame[2]) / 2.0;
    double const length = std::hypot(out.x(), out.y());
    return length > 0.0 ? top + out * (23.0 / length) : top;
}

void BoardCameraCanvas::paintEvent(QPaintEvent* event)
{
    Canvas::paintEvent(event);
    if (m_frameSize.isEmpty() || (!m_cameraTool && !(m_cameraGuide && m_hasKeys)))
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor const edge(27, 120, 168);
    painter.setPen(QPen(edge, 2, m_cameraTool ? Qt::SolidLine : Qt::DashLine));
    QPolygonF const frame = framePolygon();
    painter.drawPolygon(frame);
    if (!m_cameraTool)
        return;
    auto const rotate = rotationHandle(frame);
    painter.drawLine(QLineF((frame[0] + frame[1]) / 2.0, rotate));
    painter.setBrush(QColor(236, 248, 255));
    painter.drawEllipse(rotate, 5, 5);
    for (auto const& corner : frame)
        painter.drawRect(QRectF(corner - QPointF(4, 4), QSizeF(8, 8)));
    painter.drawText(10, 20, tr("Camera · Panel Frame %1").arg(m_frame + 1));
}

void BoardCameraCanvas::mousePressEvent(QMouseEvent* event)
{
    if (!m_cameraTool || event->button() != Qt::LeftButton) {
        Canvas::mousePressEvent(event);
        return;
    }
    if (m_frameSize.isEmpty())
        return;
    auto const frame = framePolygon();
    QPointF const point = event->position();
    if (QLineF(point, rotationHandle(frame)).length() <= 9)
        m_drag = Drag::Rotate;
    else if (std::any_of(frame.cbegin(), frame.cend(), [&](QPointF corner) { return QLineF(point, corner).length() <= 9; }))
        m_drag = Drag::Zoom;
    else if (frame.containsPoint(point, Qt::OddEvenFill))
        m_drag = Drag::Pan;
    m_startMouse = point;
    m_startPose = m_pose;
}

void BoardCameraCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_cameraTool || event->buttons().testFlag(Qt::MiddleButton)) {
        Canvas::mouseMoveEvent(event);
        return;
    }
    if (m_drag == Drag::None || !event->buttons().testFlag(Qt::LeftButton))
        return;
    QPointF const from = mapToScene(m_startMouse);
    QPointF const to = mapToScene(event->position());
    QPointF const centre(m_frameSize.width() / 2.0 + m_startPose.offset.x(),
                         m_frameSize.height() / 2.0 + m_startPose.offset.y());
    m_pose = m_startPose;
    if (m_drag == Drag::Pan) {
        QPointF const offset = m_startPose.offset + to - from;
        m_pose.offset = {std::clamp(offset.x(), -BoardCamera::offsetLimit, BoardCamera::offsetLimit),
                         std::clamp(offset.y(), -BoardCamera::offsetLimit, BoardCamera::offsetLimit)};
    } else if (m_drag == Drag::Zoom) {
        double const ratio = QLineF(centre, from).length() / std::max(0.001, QLineF(centre, to).length());
        m_pose.zoom = std::clamp(m_startPose.zoom * ratio, BoardCamera::minZoom, BoardCamera::maxZoom);
    } else {
        double const angle = std::atan2(to.y() - centre.y(), to.x() - centre.x())
                           - std::atan2(from.y() - centre.y(), from.x() - centre.x());
        double const degrees = std::remainder(angle * 180.0 / std::numbers::pi, 360.0);
        m_pose.rotation = std::clamp(m_startPose.rotation + degrees, -BoardCamera::rotationLimit, BoardCamera::rotationLimit);
    }
    update();
}

void BoardCameraCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_cameraTool || event->button() != Qt::LeftButton) {
        Canvas::mouseReleaseEvent(event);
        return;
    }
    if (m_drag == Drag::None)
        return;
    if (QLineF(m_startMouse, event->position()).length() < 2 || m_pose == m_startPose) {
        cancelDrag();
        return;
    }
    m_drag = Drag::None;
    emit cameraEdited(m_pose);
}

void BoardCameraCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_cameraTool)
        Canvas::mouseDoubleClickEvent(event);
}

void BoardCameraCanvas::tabletEvent(QTabletEvent* event)
{
    if (m_cameraTool)
        event->ignore();
    else
        Canvas::tabletEvent(event);
}

void BoardCameraCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_drag != Drag::None)
        cancelDrag();
    else
        Canvas::keyPressEvent(event);
}

void BoardCameraCanvas::focusOutEvent(QFocusEvent* event)
{
    cancelDrag();
    Canvas::focusOutEvent(event);
}

void BoardCameraCanvas::cancelDrag()
{
    if (m_drag != Drag::None) {
        m_drag = Drag::None;
        m_pose = m_startPose;
        update();
    }
}
