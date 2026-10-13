// Eclipse - the Camera view: the Transform tool (layers, pegs, quadmaps), and the shape tools.
#include "ui/canvas/Canvas.h"

#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

// ---- transform tool -----------------------------------------------------------------------
// Moves, scales and turns a whole layer - and with a peg, everything attached to it - by
// setting a keyframe on the current frame.

QRectF Canvas::subjectBounds() const
{
    Scene const& scene = m_document->scene();
    if (scene.layers[m_layer].kind == Layer::Camera)
        return QRectF(0, 0, scene.width, scene.height);  // what it sees
    QTransform const inv = scene.worldTransform(m_layer, m_frame).inverted();
    QRectF r;
    for (int l = 0; l < scene.layers.size(); ++l) {
        if (l != m_layer && !scene.isAncestor(m_layer, l))
            continue;
        int const index = scene.layers[l].drawingAt(m_frame);
        if (index == Layer::kNoDrawing || scene.layers[l].drawings[index].isEmpty())
            continue;
        QTransform const toSubject = scene.worldTransform(l, m_frame) * inv;
        // (As a deformer shows it: bent along its bones, pulled by its curve or envelope.)
        Layer const& sub = scene.layers[l];
        QRectF const own = sub.bones.size() >= 2 || sub.hasWarp() ? Deform::shown(sub.drawings[index], sub, sub.poseAt(m_frame)).bounds() : sub.drawings[index].bounds();
        QRectF const b = toSubject.map(QPolygonF(own)).boundingRect();
        r = r.isNull() ? b : r.united(b);
    }
    if (r.isNull()) {
        // Nothing to show on this frame: a small box about the pivot, to have handles at all.
        QPointF const pivot = scene.layers[m_layer].pivot;
        r = QRectF(pivot.x() - 60, pivot.y() - 60, 120, 120);
    }
    return r;
}

int Canvas::layerUnder(QPointF scenePos) const
{
    Scene const& scene = m_document->scene();
    QVector<int> const order = scene.drawOrder(m_frame);
    for (int place = int(order.size()) - 1; place >= 0; --place) {
        int const l = order[place];
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(m_frame);
        if (!layer.visible || index == Layer::kNoDrawing)
            continue;
        QPointF const p = scene.worldTransform(l, m_frame).inverted().map(scenePos);
        Drawing const d = shown(l, m_frame, layer.drawings[index]);
        for (Stroke const& s : d.strokes)
            if (s.hit(p, 4.0 / m_zoom))
                return l;
        for (Fill const& f : d.fills)
            if (f.contains(p))
                return l;
    }
    return -1;
}

// The corners of the rectangle a quadmap pulls, in its own coordinates.
static QVector<QPointF> quadSource(Layer const& layer)
{
    return {QPointF(0, 0), QPointF(layer.quadSize.width(), 0), QPointF(layer.quadSize.width(), layer.quadSize.height()),
            QPointF(0, layer.quadSize.height())};
}

void Canvas::paintQuadAids(QPainter& painter)
{
    // Where the four corners are now, joined up, with a handle on each.
    Scene const& scene = m_document->scene();
    QTransform const toWidget = worldOf(m_layer, m_frame) * viewTransform();
    QPolygonF outline;
    for (QPointF const& corner : quadSource(scene.layers[m_layer]))
        outline << toWidget.map(corner);
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 3));
    painter.drawPolygon(outline);
    painter.setPen(QPen(QColor(120, 220, 235), 1.4));
    painter.drawPolygon(outline);
    for (int i = 0; i < outline.size(); ++i) {
        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(m_drag == DragQuad && i == m_quadCorner ? QColor(Qt::white) : QColor(120, 220, 235));
        painter.drawRect(QRectF(outline[i].x() - 5, outline[i].y() - 5, 10, 10));
    }
}

void Canvas::paintBoneLayers(QPainter& painter)
{
    // Bones show while one of them, or something that hangs on one, is the current layer.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    bool wanted = m_showControl || scene.layers[m_layer].kind == Layer::Bone;
    for (int guard = 0, l = scene.parentIndex(m_layer); !wanted && l >= 0 && guard < 256; ++guard, l = scene.parentIndex(l))
        wanted = scene.layers[l].kind == Layer::Bone;
    if (!wanted)
        return;
    QTransform const view = viewTransform();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        if (layer.kind != Layer::Bone || !scene.shows(l))
            continue;
        QTransform const toWidget = worldOf(l, m_frame) * view;
        QPointF const start = toWidget.map(layer.pivot);
        QPointF const tip = toWidget.map(layer.pivot + QPointF(layer.boneLength, 0.0));
        QPointF const along = tip - start;
        double const length = std::hypot(along.x(), along.y());
        if (length < 1.0)
            continue;
        // A kite: widest a little way from where it hangs, a point at its tip.
        QPointF const across = QPointF(-along.y(), along.x()) / length * std::min(9.0, length * 0.18);
        QPolygonF const kite({start, start + along * 0.16 + across, tip, start + along * 0.16 - across});
        painter.setPen(QPen(QColor(20, 20, 20), 1.2));
        painter.setBrush(l == m_layer ? QColor(255, 255, 255, 230) : QColor(120, 220, 235, 200));
        painter.drawPolygon(kite);
        painter.drawEllipse(start, 3.5, 3.5);
    }
}

void Canvas::transformDown(QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    if (scene.layers[m_layer].kind == Layer::Group || scene.layers[m_layer].kind == Layer::ColourCard) {
        emit message(scene.layers[m_layer].kind == Layer::Group
                         ? tr("A group has no position of its own. Put it under a peg to move what is in it.")
                         : tr("A colour-card fills the frame: there is nothing to move."));
        return;
    }
    if (scene.layers[m_layer].kind == Layer::Quadmap) {
        // A corner of the quadmap: drag it.
        QTransform const quadToWidget = scene.worldTransform(m_layer, m_frame) * viewTransform();
        QVector<QPointF> const corners = quadSource(scene.layers[m_layer]);
        for (int i = 0; i < corners.size(); ++i) {
            QPointF const w = quadToWidget.map(corners[i]);
            if (std::abs(w.x() - widgetPos.x()) <= 8 && std::abs(w.y() - widgetPos.y()) <= 8) {
                m_quadCorner = i;
                m_dragQuad = scene.layers[m_layer].quadAt(scene.layers[m_layer].poseAt(m_frame));
                m_drag = DragQuad;
                return;
            }
        }
        emit message(tr("Drag a corner of the quadmap."));
        return;
    }
    auto begin = [&] {
        Layer const& layer = m_document->scene().layers[m_layer];
        m_poseStart = m_dragPose = layer.poseAt(m_frame);
        m_dragPivot = layer.pivot;
        m_world0 = m_document->scene().worldTransform(m_layer, m_frame);
        m_dragBounds = subjectBounds();
        m_dragStartScene = toScene(widgetPos);
    };
    begin();
    QTransform const toWidget = m_world0 * viewTransform();
    auto isAt = [&](QPointF layerPos) {
        QPointF const w = toWidget.map(layerPos);
        return std::abs(w.x() - widgetPos.x()) <= 7 && std::abs(w.y() - widgetPos.y()) <= 7;
    };
    // The knob stands a fixed distance on screen above the middle of the top edge.
    QPointF const top = toWidget.map(QPointF(m_dragBounds.center().x(), m_dragBounds.top()));
    QPointF const centre = toWidget.map(m_dragBounds.center());
    QPointF up = top - centre;
    double const len = std::hypot(up.x(), up.y());
    up = len > 1e-6 ? up / len : QPointF(0, -1);
    // The frame of a camera fills the view, so its knob stands inside the frame, where it can be reached.
    QPointF const knob = top + up * (m_document->scene().layers[m_layer].kind == Layer::Camera ? -28.0 : 28.0);

    // A joint of the chain (not the first, which is where the chain hangs): turn the bone
    // that ends there.
    if (scene.layers[m_layer].bones.size() >= 2) {
        QVector<QPointF> const posed = Deform::posedJoints(scene.layers[m_layer].bones, m_dragPose.bend);
        for (int j = int(posed.size()) - 1; j >= 1; --j) {
            if (isAt(posed[j])) {
                m_bone = j - 1;
                m_drag = DragBone;
                return;
            }
        }
    }
    // A point or a handle of the deformer: pull it, and the artwork is reshaped.
    if (scene.layers[m_layer].hasWarp()) {
        int const at = warpAt(Deform::posedWarp(scene.layers[m_layer].warp, m_dragPose.warp), widgetPos);
        if (at >= 0) {
            m_warpIndex = at;
            m_drag = DragWarp;
            return;
        }
    }
    if (isAt(scene.layers[m_layer].pivot)) {
        m_drag = DragPivot;
        return;
    }
    m_scaleFromPress = false;
    m_maintaining = m_transformMode == MaintainSize;
    if (m_maintaining) {
        m_drag = DragPoseMove;
        return;
    }
    // One of the tools that do one thing only: that, wherever the pen went down.
    if (m_transformMode == TranslateOnly) {
        m_drag = DragPoseMove;
        return;
    }
    if (m_transformMode == RotateOnly) {
        m_drag = DragPoseRotate;
        return;
    }
    if (m_transformMode == SkewOnly) {
        m_drag = DragPoseSkew;
        return;
    }
    if (m_transformMode == ScaleOnly) {
        m_scaleFromPress = true;
        m_dragHandle = BottomRight;
        m_drag = DragPoseScale;
        return;
    }
    if (std::abs(knob.x() - widgetPos.x()) <= 7 && std::abs(knob.y() - widgetPos.y()) <= 7) {
        m_drag = DragPoseRotate;
        return;
    }
    for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft}) {
        if (isAt(handlePos(h, m_dragBounds))) {
            m_dragHandle = h;
            m_drag = DragPoseScale;
            return;
        }
    }
    // On artwork: that of this layer (or of something attached to it) moves this layer;
    // that of another layer picks that layer and moves it.
    int const hit = layerUnder(m_dragStartScene);
    if (hit >= 0 && hit != m_layer && !scene.isAncestor(m_layer, hit)) {
        emit layerPicked(hit);  // comes back through setCurrentCell
        if (m_layer != hit)
            return;
        begin();
        m_drag = DragPoseMove;
    } else if (hit >= 0 || m_dragBounds.contains(m_world0.inverted().map(m_dragStartScene))) {
        m_drag = DragPoseMove;
    }
}

void Canvas::paintTransformAids(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    QColor const accent(255, 170, 40);
    // While dragging, the box is the one the drag began with, carried by the new pose.
    QRectF const bounds = m_drag == DragNone || m_drag == DragPan ? subjectBounds() : m_dragBounds;
    QTransform const toWidget = layerViewTransform();
    painter.resetTransform();
    QPen box(accent);
    box.setStyle(Qt::DashLine);
    painter.setPen(box);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(toWidget.map(QPolygonF(bounds)));

    painter.setPen(QPen(accent));
    painter.setBrush(Qt::white);
    for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft}) {
        QPointF const p = toWidget.map(handlePos(h, bounds));
        painter.drawRect(QRectF(p.x() - 4, p.y() - 4, 8, 8));
    }
    QPointF const top = toWidget.map(QPointF(bounds.center().x(), bounds.top()));
    QPointF const centre = toWidget.map(bounds.center());
    QPointF up = top - centre;
    double const len = std::hypot(up.x(), up.y());
    up = len > 1e-6 ? up / len : QPointF(0, -1);
    // The frame of a camera fills the view, so its knob stands inside the frame, where it can be reached.
    QPointF const knob = top + up * (m_document->scene().layers[m_layer].kind == Layer::Camera ? -28.0 : 28.0);
    painter.drawLine(top, knob);
    painter.drawEllipse(knob, 5, 5);

    // The pivot: a ring with a cross.
    QPointF const pivot = toWidget.map(m_drag == DragPivot ? m_dragPivot : scene.layers[m_layer].pivot);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 3));
    painter.drawEllipse(pivot, 6, 6);
    painter.setPen(QPen(accent, 1.5));
    painter.drawEllipse(pivot, 6, 6);
    painter.drawLine(pivot - QPointF(10, 0), pivot + QPointF(10, 0));
    painter.drawLine(pivot - QPointF(0, 10), pivot + QPointF(0, 10));

    if (scene.layers[m_layer].bones.size() >= 2)
        paintBones(painter, Deform::posedJoints(scene.layers[m_layer].bones, poseOf(m_layer, m_frame).bend), m_drag == DragBone ? m_bone + 1 : -1);
    if (scene.layers[m_layer].hasWarp())
        paintWarp(painter, Deform::posedWarp(scene.layers[m_layer].warp, poseOf(m_layer, m_frame).warp), scene.layers[m_layer].warpClosed,
                  m_drag == DragWarp ? m_warpIndex : -1);
}

// ---- shapes -------------------------------------------------------------------------------

void Canvas::updateShape(QPointF p, Qt::KeyboardModifiers modifiers)
{
    QPointF const a = m_dragStart;
    bool const shift = modifiers & Qt::ShiftModifier;
    m_stroke.points.clear();
    m_stroke.curve = false;
    m_stroke.closed = false;
    if (m_tool == Line || m_tool == CloseGap) {
        if (shift) {  // every 15 degrees
            double const length = std::hypot(p.x() - a.x(), p.y() - a.y());
            double const step = 3.14159265358979323846 / 12.0;
            double const angle = std::round(std::atan2(p.y() - a.y(), p.x() - a.x()) / step) * step;
            p = a + QPointF(std::cos(angle), std::sin(angle)) * length;
        }
        m_stroke.points = {{a, 1.0f}, {p, 1.0f}};
        return;
    }
    if (shift) {  // a square, a circle
        double const side = std::max(std::abs(p.x() - a.x()), std::abs(p.y() - a.y()));
        p = a + QPointF(p.x() < a.x() ? -side : side, p.y() < a.y() ? -side : side);
    }
    // With Alt it is drawn out from its middle: where the pen went down is the centre of it.
    QRectF const r = (modifiers & Qt::AltModifier ? QRectF(a - (p - a), p) : QRectF(a, p)).normalized();
    m_stroke.closed = true;
    if (m_tool == Rectangle) {
        m_stroke.points = {{r.topLeft(), 1.0f}, {r.topRight(), 1.0f}, {r.bottomRight(), 1.0f}, {r.bottomLeft(), 1.0f}};
    } else {
        // Twelve points round the ellipse; the curve through them is the rest of it.
        m_stroke.curve = true;
        for (int i = 0; i < 12; ++i) {
            double const angle = i * 3.14159265358979323846 / 6.0;
            m_stroke.points.push_back({r.center() + QPointF(std::cos(angle) * r.width() / 2.0, std::sin(angle) * r.height() / 2.0), 1.0f});
        }
    }
}
