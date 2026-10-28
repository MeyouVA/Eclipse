#include "ui/views/DepthView.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QSettings>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"
#include "actions/theme/InterfaceColours.h"

namespace {
constexpr double kMargin = 26.0;
}

DepthView::DepthView(Document* document, Mode mode, QWidget* parent) : QWidget(parent), m_document(document), m_mode(mode)
{
    setMinimumSize(160, 120);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    QSettings const settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    setPlanningPreferences(settings.value(QStringLiteral("prefs/planningDefaultZoom"), 100).toDouble() / 100.0,
                           settings.value(QStringLiteral("prefs/planningThumbnailSize"), 64).toInt());
    m_zoom = m_defaultZoom;
    connect(m_document, &Document::changed, this, [this] {
        m_thumbnails.clear();
        if (m_dragControlKey >= 0) { m_dragControlKey = m_dragControlIndex = -1; m_held = false; m_dragBy = 0.0; }
        update();
    });
}

void DepthView::setAnimationCanvas(Canvas* canvas)
{
    if (m_canvas)
        disconnect(m_canvas, &Canvas::planningControlsChanged, this, nullptr);
    m_canvas = canvas;
    if (m_canvas)
        connect(m_canvas, &Canvas::planningControlsChanged, this, [this] {
            if (!m_canvas->showsPlanningControls() && m_dragControlKey >= 0) {
                m_dragControlKey = m_dragControlIndex = -1; m_dragBy = 0.0; m_held = false;
            }
            update();
        });
    update();
}

void DepthView::setCurrentCell(int layer, int frame)
{
    if (m_frame != frame)
        m_thumbnails.clear();
    if (m_layer != layer || m_frame != frame) {
        m_dragControlKey = m_dragControlIndex = -1;
        if (m_dragLayer < 0) m_held = false;
    }
    m_layer = layer;
    m_frame = frame;
    update();
}

QVector<DepthView::Mark> DepthView::marks() const
{
    Scene const& scene = m_document->scene();
    QVector<Mark> found;
    QTransform const camera = scene.cameraTransform(m_frame);
    int const active = scene.cameraIndex();
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        Mark mark;
        mark.layer = l;
        mark.depth = scene.depthOf(l, m_frame);
        if (layer.kind == Layer::Camera) {
            if (l != active)
                continue;       // the scene is drawn as the active camera sees it
            mark.camera = true;
            mark.from = 0.0;
            mark.to = m_mode == Top ? scene.width : scene.height;
            mark.depthFrom = mark.depthTo = mark.depth;
            found.push_back(mark);
            continue;
        }
        if (!layer.holdsDrawings() || !scene.enabled(l))
            continue;
        int const index = layer.drawingAt(m_frame);
        if (index == Layer::kNoDrawing || layer.drawings[index].isEmpty())
            continue;
        // Where its drawing lies on its own plane, as the camera is turned.
        QRectF const box = (scene.flatTransform(l, m_frame) * camera).map(QPolygonF(layer.drawings[index].bounds())).boundingRect();
        mark.from = m_mode == Top ? box.left() : box.top();
        mark.to = m_mode == Top ? box.right() : box.bottom();
        mark.depthFrom = mark.depthTo = mark.depth;
        if (scene.turned(l, m_frame)) {
            // Turned in space: the corner furthest to each side says where that end stands.
            bool first = true;
            for (QPointF const& corner : QPolygonF(layer.drawings[index].bounds()).mid(0, 4)) {
                double x, y, z;
                scene.toSpace(l, m_frame, corner, x, y, z);
                QPointF const seen = camera.map(QPointF(x, y));
                double const across = m_mode == Top ? seen.x() : seen.y();
                if (first || across < mark.from) {
                    mark.from = across;
                    mark.depthFrom = z;
                }
                if (first || across > mark.to) {
                    mark.to = across;
                    mark.depthTo = z;
                }
                first = false;
            }
        }
        found.push_back(mark);
    }
    return found;
}

double DepthView::shownDepth(Mark const& mark) const
{
    return mark.layer == m_dragLayer ? mark.depth + m_dragBy : mark.depth;
}

void DepthView::depthRange(QVector<Mark> const& marks, double& back, double& front) const
{
    if (m_held) {
        back = m_heldBack;
        front = m_heldFront;
        return;
    }
    Scene const& scene = m_document->scene();
    int const camera = scene.cameraIndex();
    double const cameraDepth = camera >= 0 ? scene.depthOf(camera, m_frame) : 0.0;
    // From the camera itself back to the plane it sees as it is - and as far again behind.
    front = cameraDepth + scene.focalDistance(m_frame);
    back = cameraDepth - scene.focalDistance(m_frame) * 0.5;
    for (Mark const& mark : marks) {
        if (mark.camera)
            continue;
        front = std::max(front, mark.depth);
        back = std::min(back, mark.depth);
    }
    double const room = (front - back) * 0.06;
    front += room;
    back -= room;
}

QPointF DepthView::place(double across, double depth, double back, double front) const
{
    Scene const& scene = m_document->scene();
    double const span = std::max(front - back, 1e-6);
    if (m_mode == Top) {
        // Across the view as in the picture; the camera at the bottom, looking up.
        double const reach = scene.width * 2.0;                    // half a frame of room either side
        double const x = kMargin + (across + scene.width * 0.5) / reach * (width() - 2 * kMargin);
        double const y = kMargin + (depth - back) / span * (height() - 2 * kMargin);
        return (QPointF(x, y) - rect().center()) * m_zoom + rect().center() + m_pan;
    }
    // The camera at the left, looking right; up is up.
    double const reach = scene.height * 2.0;
    double const x = kMargin + (front - depth) / span * (width() - 2 * kMargin);
    double const y = kMargin + (across + scene.height * 0.5) / reach * (height() - 2 * kMargin);
    return (QPointF(x, y) - rect().center()) * m_zoom + rect().center() + m_pan;
}

double DepthView::depthPerPixel() const
{
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    double const pixels = (m_mode == Top ? height() : width()) - 2 * kMargin;
    return (front - back) / std::max(pixels, 1.0);
}

double DepthView::depthAt(QPointF widgetPos) const
{
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    QPointF const at = fitted(widgetPos);
    if (m_mode == Top)
        return back + (at.y() - kMargin) * depthPerPixel();
    return front - (at.x() - kMargin) * depthPerPixel();
}

QPointF DepthView::layerPoint(int layer) const
{
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    Scene const& scene = m_document->scene();
    for (Mark const& mark : all) {
        if (mark.layer != layer)
            continue;
        if (mark.camera)
            return place((m_mode == Top ? scene.width : scene.height) / 2.0, shownDepth(mark) + scene.focalDistance(m_frame), back, front);
        // The middle of its line (which lies at a slant when the layer is turned in space).
        double const by = shownDepth(mark) - mark.depth;
        return (place(mark.from, mark.depthFrom + by, back, front) + place(mark.to, mark.depthTo + by, back, front)) / 2.0;
    }
    return QPointF();
}

void DepthView::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter painter(this);
    painter.fillRect(rect(), QColor(58, 58, 58));
    painter.setRenderHint(QPainter::Antialiasing, true);
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    double const extent = m_mode == Top ? scene.width : scene.height;
    int const cameraLayer = scene.cameraIndex();
    double cameraDepth = cameraLayer >= 0 ? scene.depthOf(cameraLayer, m_frame) : 0.0;
    if (cameraLayer >= 0 && cameraLayer == m_dragLayer)
        cameraDepth += m_dragBy;
    double const distance = scene.focalDistance(m_frame);

    // The plane that is seen as it is drawn: a faint line right across.
    QPointF const planeA = place(-extent * 0.5, cameraDepth, back, front), planeB = place(extent * 1.5, cameraDepth, back, front);
    painter.setPen(QPen(QColor(95, 95, 95), 1, Qt::DashLine));
    painter.drawLine(planeA, planeB);

    // The cone: from the camera through the two edges of the frame, and on to the back.
    QPointF const eye = place(extent / 2.0, cameraDepth + distance, back, front);
    auto edgeAt = [&](double edge, double depth) {
        // On the line from the eye through `edge` of the frame, at this depth.
        double const t = (cameraDepth + distance - depth) / distance;
        return place(extent / 2.0 + (edge - extent / 2.0) * t, depth, back, front);
    };
    QPolygonF cone;
    cone << eye << edgeAt(0.0, back) << edgeAt(extent, back);
    QColor const cameraColour = InterfaceColours::get("planningCamera");
    QColor coneFill = cameraColour;
    coneFill.setAlphaF(coneFill.alphaF() * 0.1);
    painter.setPen(QPen(cameraColour, 1.2));
    painter.setBrush(coneFill);
    if (m_cameraCone)
        painter.drawPolygon(cone);
    // The frame itself, where the cone crosses that plane.
    painter.setPen(QPen(cameraColour, 2.2));
    painter.drawLine(place(0.0, cameraDepth, back, front), place(extent, cameraDepth, back, front));
    // The camera: a small body behind the point of the cone.
    {
        bool const current = cameraLayer >= 0 && cameraLayer == m_layer;
        painter.setPen(QPen(QColor(20, 30, 44), 1));
        painter.setBrush(current ? QColor(255, 255, 255) : cameraColour);
        QPointF const away = m_mode == Top ? QPointF(0, 1) : QPointF(-1, 0);
        QPointF const side = m_mode == Top ? QPointF(1, 0) : QPointF(0, 1);
        QPolygonF body;
        body << eye << eye + away * 12 + side * 8 << eye + away * 12 - side * 8;
        painter.drawPolygon(body);
        if (cameraLayer < 0) {
            painter.setPen(QColor(150, 150, 150));
            painter.drawText(QRectF(4, height() - 20, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("No camera: the scene is seen as it is."));
        }
    }

    // The layers, the furthest first.
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    painter.setFont(small);
    for (Mark const& mark : all) {
        if (mark.camera)
            continue;
        double const depth = shownDepth(mark), by = depth - mark.depth;
        QPointF const a = place(mark.from, mark.depthFrom + by, back, front), b = place(mark.to, mark.depthTo + by, back, front);
        bool const current = mark.layer == m_layer;
        QColor colour = scene.layers[mark.layer].trackColor.isValid() ? scene.layers[mark.layer].trackColor : QColor(214, 214, 214);
        if (current)
            colour = QColor(255, 190, 70);
        painter.setPen(QPen(QColor(20, 20, 20), current ? 5.5 : 4.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(a, b);
        painter.setPen(QPen(colour, current ? 3.5 : 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(a, b);
        if (m_thumbnailSize > 0) {
            QRectF const box = thumbnailRect(a, b);
            painter.fillRect(box, QColor(45, 45, 45));
            painter.drawImage(box, thumbnail(mark.layer));
            painter.setPen(QPen(colour, current ? 2.0 : 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
        }
        painter.setPen(current ? QColor(255, 220, 150) : QColor(190, 190, 190));
        QString const label = depth == 0.0 ? scene.layers[mark.layer].name
                                            : tr("%1  (%2 %3)").arg(scene.layers[mark.layer].name).arg(QString::number(std::abs(depth), 'f', 0), depth > 0 ? tr("F") : tr("B"));
        if (m_mode == Top)
            painter.drawText(QPointF(std::max(a.x(), b.x()) + 6, a.y() + 4), label);
        else
{
            // Kept inside the view: to the left of the line when there is no room to its right.
            int const wide = painter.fontMetrics().horizontalAdvance(label);
            double const x = a.x() + 6 + wide > width() - 2 ? a.x() - 6 - wide : a.x() + 6;
            painter.drawText(QPointF(x, std::min(a.y(), b.y()) - 4), label);
        }
    }
    paintMotionPath(painter, back, front);
    painter.setPen(QColor(140, 140, 140));
    painter.drawText(QRectF(4, 2, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, m_mode == Top ? tr("Top") : tr("Side"));
}

QPointF DepthView::fitted(QPointF widgetPos) const
{
    QPointF const centre = rect().center();
    return (widgetPos - m_pan - centre) / m_zoom + centre;
}

void DepthView::resetView()
{
    m_zoom = m_defaultZoom;
    m_pan = QPointF();
    update();
}

void DepthView::onActionFrameSelected()
{
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    for (Mark const& mark : all) {
        if (mark.layer != m_layer)
            continue;
        if (mark.camera) {
            m_pan += QPointF(rect().center()) - layerPoint(m_layer);
        } else {
            QPointF const a = place(mark.from, mark.depthFrom, back, front), b = place(mark.to, mark.depthTo, back, front);
            QPointF const centre = (a + b) / 2.0;
            double const factor = std::min(width() * 0.7 / std::max(std::abs(a.x() - b.x()), 40.0),
                                           height() * 0.7 / std::max(std::abs(a.y() - b.y()), 40.0));
            double const zoom = std::clamp(m_zoom * factor, 0.2, 40.0);
            m_pan = (m_pan - centre + rect().center()) * (zoom / m_zoom);
            m_zoom = zoom;
        }
        update();
        return;
    }
}

int DepthView::markAt(QPointF at) const
{
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    Scene const& scene = m_document->scene();
    int picked = -1;
    double nearest = 9.0;   // pixels
    for (Mark const& mark : all) {
        double d = 1e9;
        if (mark.camera) {
            QPointF const eye = place((m_mode == Top ? scene.width : scene.height) / 2.0, mark.depth + scene.focalDistance(m_frame), back, front);
            d = std::hypot(at.x() - eye.x(), at.y() - eye.y()) - 6.0;
        } else {
            QPointF const a = place(mark.from, mark.depthFrom, back, front), b = place(mark.to, mark.depthTo, back, front);
            if (m_thumbnailSize > 0 && thumbnailRect(a, b).contains(at)) {
                if (picked < 0 || nearest > 0.0 || mark.layer == m_layer) {
                    nearest = 0.0;
                    picked = mark.layer;
                }
                continue;
            }
            if (mark.depthFrom != mark.depthTo) {
                // At a slant: how far from the line itself.
                QPointF const ab = b - a, ap = at - a;
                double const l2 = ab.x() * ab.x() + ab.y() * ab.y();
                double const u = l2 > 1e-9 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0) : 0.0;
                d = std::hypot(at.x() - (a.x() + ab.x() * u), at.y() - (a.y() + ab.y() * u));
                if (d < 9.0 && (picked < 0 || d < nearest - 0.5 || (mark.layer == m_layer && d < nearest + 0.5))) {
                    nearest = d;
                    picked = mark.layer;
                }
                continue;
            }
            // How far from the line: along the depth, and past its ends.
            double const off = m_mode == Top ? std::abs(at.y() - a.y()) : std::abs(at.x() - a.x());
            double const lo = m_mode == Top ? std::min(a.x(), b.x()) : std::min(a.y(), b.y());
            double const hi = m_mode == Top ? std::max(a.x(), b.x()) : std::max(a.y(), b.y());
            double const along = m_mode == Top ? at.x() : at.y();
            d = std::max(off, std::max(lo - along, along - hi) - 6.0);
        }
        // The current layer first among those as near, so that it can be taken from a pile.
        if (d < 9.0 && (picked < 0 || d < nearest - 0.5 || (mark.layer == m_layer && d < nearest + 0.5))) {
            nearest = d;
            picked = mark.layer;
        }
    }
    return picked;
}

void DepthView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_panning = true;
        m_last = event->position();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    QPointF const at = event->position();
    if (beginMotionControlDrag(at))
        return;
    int const picked = markAt(at);
    if (picked < 0)
        return;
    if (picked != m_layer)
        emit layerPicked(picked);
    if (m_document->scene().layers[picked].locked || (m_canvas && m_canvas->noZDragging()))
        return;
    QVector<Mark> const all = marks();
    double back = 0.0, front = 0.0;
    depthRange(all, back, front);
    m_heldBack = back;
    m_heldFront = front;
    m_held = true;
    m_dragLayer = picked;
    m_dragStart = depthAt(at);
    m_dragBy = 0.0;
    update();
}

bool DepthView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && m_dragControlKey >= 0 && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void DepthView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_dragControlKey >= 0) {
        m_dragControlKey = m_dragControlIndex = -1; m_dragBy = 0.0; m_held = false;
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void DepthView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && markAt(event->position()) < 0)
        resetView();
}

void DepthView::wheelEvent(QWheelEvent* event)
{
    double const zoom = std::clamp(m_zoom * std::pow(1.15, event->angleDelta().y() / 120.0), 0.2, 40.0);
    // What is under the pointer stays under it.
    QPointF const centre = rect().center();
    QPointF const from = event->position() - centre - m_pan;
    m_pan = event->position() - centre - from * (zoom / m_zoom);
    m_zoom = zoom;
    update();
    event->accept();
}

void DepthView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        m_pan += event->position() - m_last;
        m_last = event->position();
        update();
        return;
    }
    if (m_dragLayer < 0 && m_dragControlKey < 0)
        return;
    m_dragBy = depthAt(event->position()) - m_dragStart;
    if (m_dragControlKey >= 0) {
        QPointF const delta = event->position() - m_dragControlStart;
        double const determinant = m_dragControlAxis.x() * m_dragControlDepthAxis.y() - m_dragControlAxis.y() * m_dragControlDepthAxis.x();
        if (m_dragControlNoZ) {
            double const across = m_mode == Top ? delta.x() : delta.y();
            m_dragControlDelta = m_dragNoZCoefficients * across;
        } else if (m_dragControlCoordinate >= 0 && std::abs(determinant) >= 1e-9) {
            m_dragControlDelta.setX((delta.x() * m_dragControlDepthAxis.y() - delta.y() * m_dragControlDepthAxis.x()) / determinant);
            m_dragControlDelta.setY((m_dragControlAxis.x() * delta.y() - m_dragControlAxis.y() * delta.x()) / determinant);
        } else {
            m_dragControlDelta = QPointF(0.0, m_dragBy / m_dragControlSlope);
        }
        if (event->modifiers() & Qt::ShiftModifier) {
            if (m_dragControlNoZ) {
                double const length = std::hypot(m_dragControlDelta.x(), m_dragControlDelta.y());
                if (length > 1e-9) m_dragControlDelta *= std::round(length / 10.0) * 10.0 / length;
            } else {
                m_dragControlDelta.setX(std::round(m_dragControlDelta.x() / 10.0) * 10.0);
                m_dragControlDelta.setY(std::round(m_dragControlDelta.y() / 10.0) * 10.0);
            }
        }
        update();
        return;
    }
    // With Shift, by whole tens.
    if (event->modifiers() & Qt::ShiftModifier)
        m_dragBy = std::round(m_dragBy / 10.0) * 10.0;
    update();
}

void DepthView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
        m_panning = false;
    if (event->button() != Qt::LeftButton)
        return;
    if (m_dragControlKey >= 0) {
        int const key = m_dragControlKey, control = m_dragControlIndex;
        QPointF position;
        double depth = 0.0;
        motionControlDragTarget(position, depth);
        m_dragControlKey = m_dragControlIndex = -1; m_held = false; m_dragBy = 0.0;
        if (m_canvas && (!m_canvas->noZDragging() || m_dragControlNoZ) && m_layer >= 0 && m_layer < m_document->scene().layers.size()
            && !m_document->scene().layers[m_layer].locked && std::isfinite(depth))
            m_document->moveControlPointInSpace(m_layer, key, control, position, depth);
        update();
        return;
    }
    if (m_dragLayer < 0)
        return;
    int const layer = m_dragLayer;
    double const by = m_dragBy;
    m_dragLayer = -1;
    m_dragBy = 0.0;
    m_held = false;
    Scene const& scene = m_document->scene();
    if (layer < scene.layers.size() && std::abs(by) > 1e-9) {
        Pose const was = scene.poseOf(layer, m_frame);
        Pose pose = was;
        pose.z += by;
        bool const offset = m_canvas && !m_canvas->animates();
        bool const ranged = m_canvas && m_canvas->animatesOnionRange();
        int const first = ranged ? std::max(0, m_frame - m_canvas->onionPrevious()) : -1;
        int const last = ranged ? m_frame + m_canvas->onionNext() : -1;
        if (!m_document->animatePose(layer, m_frame, was, pose, tr("Move in Depth"), Keyframe::PartZ, offset, first, last))
            emit message(tr("Edit the linked column's expression or path in the Function Editor to change its depth."));
    }
    update();
}
