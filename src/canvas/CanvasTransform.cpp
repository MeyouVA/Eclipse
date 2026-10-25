// Eclipse - the Camera view: the Transform tool (layers, pegs, quadmaps), and the shape tools.
#include "canvas/Canvas.h"

#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"
#include "actions/theme/InterfaceColours.h"

// ---- transform tool -----------------------------------------------------------------------
// Moves, scales and turns a whole layer - and with a peg, everything attached to it - by
// setting a keyframe on the current frame.

QRectF Canvas::subjectBounds() const
{
    Scene const& scene = m_document->scene();
    if (scene.layers[m_layer].kind == Layer::Camera)
        return QRectF(0, 0, scene.width, scene.height);  // what it sees
    QRectF const r = layerBounds(m_layer);
    if (!r.isNull())
        return r;
    // Nothing to show on this frame: a small box about the pivot, to have handles at all.
    QPointF const pivot = scene.pivotOf(m_layer, m_frame);
    return QRectF(pivot.x() - 60, pivot.y() - 60, 120, 120);
}

QRectF Canvas::layerBounds(int layerIndex) const
{
    Scene const& scene = m_document->scene();
    if (layerIndex < 0 || layerIndex >= scene.layers.size())
        return QRectF();
    QTransform const inv = scene.worldTransform(layerIndex, m_frame).inverted();
    QRectF r;
    for (int l = 0; l < scene.layers.size(); ++l) {
        if (l != layerIndex && !scene.isAncestor(layerIndex, l))
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

void Canvas::transformDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers)
{
    Scene const& scene = m_document->scene();
    // A layer the animation tools pass by (or a drawing, in peg selection mode): the peg above it is worked on.
    if (int const target = animationTarget(m_layer); target != m_layer) {
        if (target < 0) {
            emit message(tr("This layer is not animated with the animation tools (Layer Properties > Animate Using Animation Tools). Attach it to a peg to move it."));
            return;
        }
        emit layerPicked(target);
        if (m_layer != target)
            return;
    }
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
        m_poseStart = m_dragPose = m_document->scene().poseOf(m_layer, m_frame);
        m_dragPivot = m_document->scene().pivotOf(m_layer, m_frame);
        m_world0 = m_document->scene().worldTransform(m_layer, m_frame);
        m_dragBounds = subjectBounds();
        m_dragStartScene = toScene(widgetPos);
    };
    begin();
    TransformOptions const& o = m_tx;
    double const reach = std::max(4.0, o.pointSize / 2.0 + 1.0);
    QTransform const toWidget = m_world0 * viewTransform();
    auto isAt = [&](QPointF layerPos) {
        QPointF const w = toWidget.map(layerPos);
        return std::abs(w.x() - widgetPos.x()) <= reach && std::abs(w.y() - widgetPos.y()) <= reach;
    };
    Layer const& current = scene.layers[m_layer];
    auto canSkew = [&] {
        if (current.separateScale)
            return true;
        emit message(tr("To skew with the Transform tool, set the Scale option to Separate in the Layer Properties."));
        return false;
    };

    if (!o.hideControls && current.bones.size() >= 2) {
        QVector<QPointF> const posed = Deform::posedJoints(current.bones, m_dragPose.bend);
        for (int j = int(posed.size()) - 1; j >= 1; --j) {
            if (isAt(posed[j])) {
                m_bone = j - 1;
                m_drag = DragBone;
                return;
            }
        }
    }
    // A point or a handle of the deformer: pull it, and the artwork is reshaped.
    if (!o.hideControls && current.hasWarp()) {
        int const at = warpAt(Deform::posedWarp(current.warp, m_dragPose.warp), widgetPos);
        if (at >= 0) {
            m_warpIndex = at;
            m_drag = DragWarp;
            return;
        }
    }
    if (!o.hideControls && !o.pivotFixed && o.pivotDraggable && isAt(m_dragPivot)) {
        if (m_dragPivot != current.pivot) {
            emit message(tr("The pivot comes from the drawing: move it with the Drawing Pivot tool."));
            return;
        }
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
        if (canSkew())
            m_drag = DragPoseSkew;
        return;
    }
    if (m_transformMode == ScaleOnly) {
        m_scaleFromPress = true;
        m_dragHandle = BottomRight;
        m_drag = DragPoseScale;
        return;
    }
    if (!o.hideControls) {
        if (o.rotationLever) {
            QPointF const knob = transformKnob(m_dragBounds, toWidget);
            if (std::abs(knob.x() - widgetPos.x()) <= reach && std::abs(knob.y() - widgetPos.y()) <= reach) {
                m_drag = DragPoseRotate;
                return;
            }
        }
        for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft, TopSide, RightSide, BottomSide, LeftSide}) {
            if (isAt(handlePos(h, m_dragBounds))) {
                m_dragHandle = h;
                m_drag = DragPoseScale;
                return;
            }
        }
        QPolygonF const box = toWidget.map(QPolygonF(m_dragBounds));
        if (!o.rotationLever && !box.containsPoint(widgetPos, Qt::OddEvenFill)) {
            // Just outside a corner: turning.
            for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft}) {
                QPointF const corner = toWidget.map(handlePos(h, m_dragBounds));
                double const away = std::hypot(corner.x() - widgetPos.x(), corner.y() - widgetPos.y());
                if (away > reach && away <= reach + 16.0) {
                    m_drag = DragPoseRotate;
                    return;
                }
            }
        }
        if (o.allowSkew && box.size() >= 4) {
            // On the top or bottom edge, between the handles: leaning.
            for (int edge : {0, 2}) {
                QPointF const from = box[edge], along = box[edge + 1] - box[edge];
                double const length = std::hypot(along.x(), along.y());
                if (length < 1.0)
                    continue;
                QPointF const off = widgetPos - from;
                double const t = (off.x() * along.x() + off.y() * along.y()) / (length * length);
                double const distance = std::abs(off.x() * along.y() - off.y() * along.x()) / length;
                if (t > 0.0 && t < 1.0 && distance <= 3.5) {
                    if (canSkew())
                        m_drag = DragPoseSkew;
                    return;
                }
            }
        }
    }
    int const hit = layerUnder(m_dragStartScene);
    bool const inBox = m_dragBounds.contains(m_world0.inverted().map(m_dragStartScene));
    bool const ours = hit == m_layer || (hit >= 0 && scene.isAncestor(m_layer, hit)) || (hit >= 0 && m_transformLayers.contains(animationTarget(hit)));
    if (o.lockSelection) {
        // The selection stays as it is: only what is picked can be dragged.
        if (ours || inBox)
            m_drag = DragPoseMove;
        return;
    }
    if (hit >= 0 && (modifiers & Qt::ShiftModifier)) {
        // Shift and a click: that layer is picked as well (or no longer).
        int const target = animationTarget(hit);
        if (target >= 0 && target != m_layer) {
            QVector<int> more = m_transformLayers;
            if (more.contains(target))
                more.removeAll(target);
            else
                more.push_back(target);
            setTransformLayers(more);
        }
        return;
    }
    if (hit >= 0 && !ours) {
        int const target = animationTarget(hit);
        if (target < 0) {
            emit message(tr("This layer is not animated with the animation tools (Layer Properties > Animate Using Animation Tools). Attach it to a peg to move it."));
            return;
        }
        emit layerPicked(target);  // comes back through setCurrentCell
        if (m_layer != target)
            return;
        begin();
        m_drag = DragPoseMove;
    } else if (hit >= 0 || inBox) {
        m_drag = DragPoseMove;
    } else {
        // Empty space: a lasso (or a box) round the layers to pick.
        m_layerPick = QPolygonF() << widgetPos;
        m_drag = DragLayerPick;
    }
}

QPointF Canvas::transformKnob(QRectF const& bounds, QTransform const& toWidget) const
{
    // The lever stands a fixed distance on screen above the middle of the top edge (inside the frame for a camera, where it can be reached).
    QPointF const top = toWidget.map(QPointF(bounds.center().x(), bounds.top()));
    QPointF const centre = toWidget.map(bounds.center());
    QPointF up = top - centre;
    double const len = std::hypot(up.x(), up.y());
    up = len > 1e-6 ? up / len : QPointF(0, -1);
    bool const camera = m_layer >= 0 && m_layer < m_document->scene().layers.size() && m_document->scene().layers[m_layer].kind == Layer::Camera;
    return top + up * (camera ? -28.0 : 28.0);
}

void Canvas::paintTransformAids(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    TransformOptions const& o = m_tx;
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor const accent = InterfaceColours::get("transformTool");
    QColor const handle = InterfaceColours::get("transformHandle");
    if (m_drag == DragLayerPick && m_layerPick.size() >= 2) {
        QPen pick(accent);
        pick.setStyle(Qt::DashLine);
        painter.setPen(pick);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(o.lasso ? m_layerPick : QPolygonF(QRectF(m_layerPick.first(), m_layerPick.last()).normalized()));
    }
    // The other layers picked: a box round each.
    for (int other : m_transformLayers) {
        if (other < 0 || other >= scene.layers.size() || other == m_layer)
            continue;
        QRectF const b = layerBounds(other);
        if (b.isNull())
            continue;
        QPen box(accent.lighter(130));
        box.setStyle(Qt::DotLine);
        painter.setPen(box);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon((worldOf(other, m_frame) * viewTransform()).map(QPolygonF(b)));
    }
    if (o.hideControls)
        return;
    // While dragging, the box is the one the drag began with, carried by the new pose.
    QRectF const bounds = m_drag == DragNone || m_drag == DragPan || m_drag == DragLayerPick ? subjectBounds() : m_dragBounds;
    if (!o.showOnEmpty && layerBounds(m_layer).isNull())
        return;
    QTransform const toWidget = layerViewTransform();
    QPen box(accent);
    box.setStyle(Qt::DashLine);
    painter.setPen(box);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(toWidget.map(QPolygonF(bounds)));

    double const half = std::max(2.5, o.pointSize / 3.0);
    painter.setPen(QPen(accent));
    painter.setBrush(handle);
    for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft, TopSide, RightSide, BottomSide, LeftSide}) {
        QPointF const p = toWidget.map(handlePos(h, bounds));
        double const r = h >= TopSide ? half * 0.75 : half;
        painter.drawRect(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r));
    }
    if (o.rotationLever) {
        QPointF const top = toWidget.map(QPointF(bounds.center().x(), bounds.top()));
        QPointF const knob = transformKnob(bounds, toWidget);
        painter.drawLine(top, knob);
        painter.drawEllipse(knob, half + 1, half + 1);
    }

    // The pivot: a ring with a cross (hollow in the middle, as a donut, when Preferences say so).
    bool const peg = scene.layers[m_layer].kind == Layer::Peg;
    QColor const dark = InterfaceColours::get(peg ? "pegPivotDark" : "pivotDark");
    QColor const light = InterfaceColours::get(peg ? "pegPivotLight" : "pivotLight");
    QPointF const pivot = toWidget.map(m_drag == DragPivot ? m_dragPivot : scene.pivotOf(m_layer, m_frame));
    double const ring = std::max(3.0, o.pointSize / 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(dark, 3));
    painter.drawEllipse(pivot, ring, ring);
    painter.setPen(QPen(light, 1.5));
    painter.drawEllipse(pivot, ring, ring);
    if (o.donutPivot) {
        painter.setPen(QPen(dark, 1));
        painter.drawEllipse(pivot, ring * 0.45, ring * 0.45);
    } else {
        painter.setPen(QPen(dark, 3));
        painter.drawLine(pivot - QPointF(ring * 1.7, 0), pivot + QPointF(ring * 1.7, 0));
        painter.drawLine(pivot - QPointF(0, ring * 1.7), pivot + QPointF(0, ring * 1.7));
        painter.setPen(QPen(light, 1.5));
        painter.drawLine(pivot - QPointF(ring * 1.7, 0), pivot + QPointF(ring * 1.7, 0));
        painter.drawLine(pivot - QPointF(0, ring * 1.7), pivot + QPointF(0, ring * 1.7));
    }
    // The tools that do one thing: a circle of their size about the pivot, coloured for what they do; the angle turned so far.
    if (m_transformMode == RotateOnly || m_transformMode == ScaleOnly || m_transformMode == SkewOnly || m_drag == DragPoseRotate) {
        double const radius = o.toolSize * std::clamp(o.rotationControl, 0.05, 2.0);
        QColor const colour = m_transformMode == ScaleOnly ? InterfaceColours::get("scaleControl")
                              : m_transformMode == SkewOnly ? InterfaceColours::get("skewControl") : InterfaceColours::get("rotationControl1");
        painter.setPen(QPen(colour, 1.2));
        painter.drawEllipse(pivot, radius, radius);
        if (m_drag == DragPoseRotate) {
            double const turned = m_dragPose.rotation - m_poseStart.rotation;
            QColor fill = InterfaceColours::get("rotationControl2");
            fill.setAlpha(70);
            painter.setBrush(fill);
            painter.setPen(Qt::NoPen);
            // (Up the picture is down the numbers: a turn the clock's way is drawn so.)
            painter.drawPie(QRectF(pivot - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)), 90 * 16, int(-turned * 16.0));
        }
    }

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
        } else {
            p = endMagnet(p);       // onto the end of a line near it: the two meet, and the shape they make closes
        }
        m_stroke.points = {{a, 1.0f}, {p, 1.0f}};
        return;
    }
    // With a guide to draw along, its sides run as the guide does.
    if (guidedShape(a, p))
        return;
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
        m_stroke.curve = true;
        double const k = 0.5522847498, rx = r.width() / 2.0, ry = r.height() / 2.0;
        QPointF const c = r.center();
        struct Side { QPointF at, in, out; };
        Side const sides[4] = {{c + QPointF(rx, 0), QPointF(0, -k * ry), QPointF(0, k * ry)},
                               {c + QPointF(0, ry), QPointF(k * rx, 0), QPointF(-k * rx, 0)},
                               {c + QPointF(-rx, 0), QPointF(0, k * ry), QPointF(0, -k * ry)},
                               {c + QPointF(0, -ry), QPointF(-k * rx, 0), QPointF(k * rx, 0)}};
        for (Side const& side : sides) {
            StrokePoint point;
            point.pos = side.at;
            point.handles = true;
            point.in = side.in;
            point.out = side.out;
            m_stroke.points.push_back(point);
        }
    }
}
