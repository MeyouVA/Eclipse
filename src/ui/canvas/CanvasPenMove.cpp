// Eclipse - the Camera view: the pen moved - what each drag does as it goes.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

void Canvas::penMove(QPointF widgetPos, float pressure, Qt::KeyboardModifiers modifiers)
{
    // While a pose is being dragged the layer moves under the pointer: those drags work
    // from the scene position instead.
    QPointF const p = posing() || m_drag == DragPivot || m_drag == DragPan || m_drag == DragJoint || m_drag == DragWarpRest || m_drag == DragQuad || m_drag == DragRotateView ? QPointF() : toLayer(widgetPos);
    switch (m_drag) {
    case DragPoseMove: {
        // The offset is in the coordinates of the parent.
        if (modifiers & Qt::AltModifier) {
            // With Alt: nearer or further instead - up the view is away from the camera.
            double const dy = viewTransform().map(m_dragStartScene).y() - widgetPos.y();
            m_dragPose = m_poseStart;
            m_dragPose.z = m_poseStart.z - dy / std::max(m_zoom, 1e-6) * 2.0;
            update();
            break;
        }
        QTransform const parent = parentSeen().inverted();
        QPointF const d = parent.map(toScene(widgetPos)) - parent.map(m_dragStartScene);
        m_dragPose = m_poseStart;
        m_dragPose.x += d.x();
        m_dragPose.y += d.y();
        update();
        break;
    }
    case DragPoseRotate: {
        QPointF const c = m_world0.map(m_document->scene().layers[m_layer].pivot);
        QPointF const now = toScene(widgetPos);
        double const a0 = std::atan2(m_dragStartScene.y() - c.y(), m_dragStartScene.x() - c.x());
        double const a1 = std::atan2(now.y() - c.y(), now.x() - c.x());
        double degrees = (a1 - a0) * 180.0 / 3.14159265358979323846;
        // Take the short way round when the pointer crosses the half turn.
        degrees = std::remainder(degrees, 360.0);
        if (modifiers & Qt::ShiftModifier)
            degrees = std::round(degrees / 15.0) * 15.0;
        m_dragPose = m_poseStart;
        m_dragPose.rotation += degrees;
        update();
        break;
    }
    case DragPoseScale: {
        // Measured in the layer as it was when the drag began, from the pivot.
        QPointF const pivot = m_document->scene().layers[m_layer].pivot;
        QTransform const inv = m_world0.inverted();
        QPointF const from = handlePos(m_dragHandle, m_dragBounds) - pivot;
        QPointF const to = inv.map(toScene(widgetPos)) - pivot;
        double sx = std::abs(from.x()) > 1e-6 ? to.x() / from.x() : 1.0;
        double sy = std::abs(from.y()) > 1e-6 ? to.y() / from.y() : 1.0;
        if (modifiers & Qt::ShiftModifier) {
            double const s = std::max(std::abs(sx), std::abs(sy));
            sx = sx < 0 ? -s : s;
            sy = sy < 0 ? -s : s;
        }
        auto keep = [](double v) { return std::abs(v) < 0.01 ? (v < 0 ? -0.01 : 0.01) : v; };
        m_dragPose = m_poseStart;
        m_dragPose.scaleX = keep(m_poseStart.scaleX * sx);
        m_dragPose.scaleY = keep(m_poseStart.scaleY * sy);
        update();
        break;
    }
    case DragPivot:
        m_dragPivot = m_world0.inverted().map(toScene(widgetPos));
        update();
        break;
    case DragQuad: {
        // The corner goes under the pointer.  Corners are kept where the pull puts them,
        // before the quadmap's own position and what it is attached to.
        Scene const& scene = m_document->scene();
        Layer const& layer = scene.layers[m_layer];
        QTransform const after = Layer::matrix(layer.poseAt(m_frame), layer.pivot) * parentSeen();
        if (m_quadCorner >= 0 && m_quadCorner < m_dragQuad.size())
            m_dragQuad[m_quadCorner] = after.inverted().map(toScene(widgetPos));
        update();
        break;
    }
    case DragJoint:
    case DragWarpRest:
        m_dragNow = toLayer(widgetPos);
        update();
        break;
    case DragWarp: {
        // The point goes where the pointer is; a handle too, measured from its point as the pose has it.
        Layer const& layer = m_document->scene().layers[m_layer];
        if (m_warpIndex < 0 || m_warpIndex >= layer.warp.size())
            break;
        QPointF const target = m_world0.inverted().map(toScene(widgetPos));
        if (m_dragPose.warp.size() < layer.warp.size())
            m_dragPose.warp.resize(layer.warp.size());
        int const point = m_warpIndex / 3 * 3;
        if (m_warpIndex == point)
            m_dragPose.warp[point] = target - layer.warp[point];
        else
            m_dragPose.warp[m_warpIndex] = target - (layer.warp[point] + m_dragPose.warp[point]) - layer.warp[m_warpIndex];
        update();
        break;
    }
    case DragIK:
        // From the angles it has now, so that the arm keeps bending the way it bends.
        m_dragPose.bend = Deform::solveIK(m_document->scene().layers[m_layer].bones, m_dragPose.bend, m_bone,
                                          m_world0.inverted().map(toScene(widgetPos)));
        update();
        break;
    case DragBone: {
        // Turn the bone so that it points at the pointer.
        Layer const& layer = m_document->scene().layers[m_layer];
        if (m_bone < 0 || m_bone + 1 >= layer.bones.size())
            break;
        QPointF const target = m_world0.inverted().map(toScene(widgetPos));
        if (m_dragPose.bend.size() <= m_bone)
            m_dragPose.bend.resize(m_bone + 1);
        QVector<QPointF> const posed = Deform::posedJoints(layer.bones, m_dragPose.bend);
        QPointF const from = posed[m_bone];
        QPointF const rest = layer.bones[m_bone + 1] - layer.bones[m_bone];
        double const degrees = 180.0 / 3.14159265358979323846;
        double parents = 0.0;  // how far the bones before it have turned it already
        for (int i = 0; i < m_bone; ++i)
            parents += m_dragPose.bend[i];
        double const wanted = std::atan2(target.y() - from.y(), target.x() - from.x()) * degrees - std::atan2(rest.y(), rest.x()) * degrees - parents;
        // The nearest way round from where it is, so that it never jumps a whole turn.
        double const now = m_dragPose.bend[m_bone];
        m_dragPose.bend[m_bone] = now + std::remainder(wanted - now, 360.0);
        update();
        break;
    }
    case DragPan:
        m_pan += widgetPos - m_lastWidgetPos;
        m_fitted = false;
        update();
        break;
    case DragRotateView: {
        // By the angle the pointer has gone round the middle of the view.
        QPointF const middle(width() / 2.0, height() / 2.0);
        QLineF const before(middle, m_lastWidgetPos), now(middle, widgetPos);
        if (before.length() > 2.0 && now.length() > 2.0)
            rotateView(before.angleTo(now) > 180.0 ? 360.0 - before.angleTo(now) : -before.angleTo(now));
        break;
    }
    case DragShape:
        updateShape(snapped(p), modifiers);
        update();
        break;
    case DragPoint:
        m_dragNow = snapped(p);
        update();
        break;
    case DragPolyHandle:
        // Dragged while placing a point of the polyline: the line bends through the point,
        // its handles reaching as far as the drag (the points only clicked stay corners).
        if (m_polyActive && !m_stroke.points.isEmpty()) {
            StrokePoint& point = m_stroke.points.last();
            QPointF const reach = p - point.pos;
            if (std::hypot(reach.x(), reach.y()) * m_zoom > 4.0) {
                if (!m_stroke.curve) {
                    for (StrokePoint& other : m_stroke.points)
                        other.corner = !other.handles;
                    m_stroke.curve = true;
                }
                point.handles = true;
                point.corner = false;
                point.out = reach;
                point.in = -reach;
            }
            m_polyHover = p;
            update();
        }
        break;
    case DragHandle:
        m_dragNow = p;
        m_ceBreak = m_ceBreak || (modifiers & Qt::AltModifier);
        update();
        break;
    case DragDraw: {
        if (m_tool == Pencil)
            pressure = 1.0f;
        // Skip samples that did not move: they add weight to the file and nothing to the line.
        QPointF const d = p - m_stroke.points.last().pos;
        if (std::hypot(d.x(), d.y()) * m_zoom >= 1.0) {
            m_stroke.points.push_back({p, pressure});
            update();
        }
        break;
    }
    case DragErase: {
        if (m_eraserCuts) {
            if (m_stroke.points.isEmpty() || QLineF(m_stroke.points.last().pos, p).length() * m_zoom >= 1.0) {
                m_stroke.points.push_back({p, 1.0f});
                eraseStretch(std::max(0, int(m_stroke.points.size()) - 2));
                update();
            }
            break;
        }
        // Erase along the whole movement, not just where the samples landed: a fast drag
        // would otherwise jump over thin lines.
        QPointF const from = toLayer(m_lastWidgetPos);
        double const radius = m_brushWidth / 2.0;
        double const length = std::hypot(p.x() - from.x(), p.y() - from.y());
        int const steps = std::max(1, int(std::ceil(length / std::max(radius, 0.5))));
        for (int i = 1; i <= steps; ++i)
            eraseArtAt(from + (p - from) * (double(i) / steps), radius);
        break;
    }
    case DragPointMarquee:
        m_dragNow = p;
        update();
        break;
    case DragGradient: {
        if (!gradientValid())
            break;
        if (m_gradHandle == 1) {
            m_gradFrom = p;
            m_gradMoved = true;
        } else if (m_gradHandle == 2) {
            m_gradTo = p;
            m_gradMoved = true;
        } else if (m_gradMoved || QLineF(m_gradStartWidget, widgetPos).length() > 3.0) {
            // Drawn out afresh: from where the press was to where the pointer is.
            m_gradFrom = m_gradStart;
            m_gradTo = p;
            m_gradMoved = true;
        }
        if (m_gradMoved && m_gradFill < m_eraseArt.fills.size()) {
            Fill& fill = m_eraseArt.fills[m_gradFill];
            fill.gradPlaced = true;
            fill.gradFrom = m_gradFrom;
            fill.gradTo = m_gradTo;
        }
        update();
        break;
    }
    case DragPaint:
    case DragMarquee:
        m_dragNow = p;
        if (m_dragLasso && (m_lassoPath.isEmpty() || QLineF(m_lassoPath.last(), p).length() * m_zoom >= 2.0))
            m_lassoPath.push_back(p);
        update();
        break;
    case DragMove:
        m_dragTransform = QTransform::fromTranslate(p.x() - m_dragStart.x(), p.y() - m_dragStart.y());
        update();
        break;
    case DragScale: {
        // Scale about the corner opposite the one being dragged.
        Handle const opposite = Handle((int(m_dragHandle) - TopLeft + 2) % 4 + TopLeft);
        QPointF const anchor = handlePos(opposite, m_dragBounds);
        QPointF const corner = handlePos(m_dragHandle, m_dragBounds);
        double sx = std::abs(corner.x() - anchor.x()) > 1e-6 ? (p.x() - anchor.x()) / (corner.x() - anchor.x()) : 1.0;
        double sy = std::abs(corner.y() - anchor.y()) > 1e-6 ? (p.y() - anchor.y()) / (corner.y() - anchor.y()) : 1.0;
        if (modifiers & Qt::ShiftModifier) {  // keep proportions
            double const s = std::max(std::abs(sx), std::abs(sy));
            sx = sx < 0 ? -s : s;
            sy = sy < 0 ? -s : s;
        }
        // Never collapse to nothing: that could not be undone by dragging back.
        if (std::abs(sx) < 0.01)
            sx = sx < 0 ? -0.01 : 0.01;
        if (std::abs(sy) < 0.01)
            sy = sy < 0 ? -0.01 : 0.01;
        m_dragTransform = QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(sx, sy)
            * QTransform::fromTranslate(anchor.x(), anchor.y());
        update();
        break;
    }
    case DragRotate: {
        QPointF const c = m_dragBounds.center();
        double const a0 = std::atan2(m_dragStart.y() - c.y(), m_dragStart.x() - c.x());
        double const a1 = std::atan2(p.y() - c.y(), p.x() - c.x());
        double degrees = (a1 - a0) * 180.0 / 3.14159265358979323846;
        if (modifiers & Qt::ShiftModifier)  // snap to 15 degrees
            degrees = std::round(degrees / 15.0) * 15.0;
        m_dragTransform = QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(degrees)
            * QTransform::fromTranslate(c.x(), c.y());
        update();
        break;
    }
    default:
        break;
    }
    m_lastWidgetPos = widgetPos;
}
