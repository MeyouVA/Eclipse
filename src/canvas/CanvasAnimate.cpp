// Eclipse - the Camera view: the Transform tool's options, layers transformed together, nudging, pivots and stepping along motion paths.
#include "canvas/Canvas.h"

#include <QCursor>
#include <QPainter>

#include <cmath>

#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"

int Canvas::changedParts(Pose const& a, Pose const& b)
{
    auto differs = [](double x, double y) { return std::abs(x - y) > 1e-6; };
    int parts = 0;
    if (differs(a.x, b.x)) parts |= Keyframe::PartX;
    if (differs(a.y, b.y)) parts |= Keyframe::PartY;
    if (differs(a.z, b.z)) parts |= Keyframe::PartZ;
    if (differs(a.scaleX, b.scaleX)) parts |= Keyframe::PartScaleX;
    if (differs(a.scaleY, b.scaleY)) parts |= Keyframe::PartScaleY;
    if (differs(a.rotation, b.rotation)) parts |= Keyframe::PartAngle;
    if (differs(a.opacity, b.opacity)) parts |= Keyframe::PartOpacity;
    if (differs(a.skew, b.skew)) parts |= Keyframe::PartSkew;
    if (differs(a.fov, b.fov)) parts |= Keyframe::PartFov;
    if (differs(a.rotX, b.rotX) || differs(a.rotY, b.rotY) || a.quad != b.quad || a.bend != b.bend || a.warp != b.warp || a.fx != b.fx || a.points != b.points)
        parts |= Keyframe::PartRest;
    return parts;
}

int Canvas::animationTarget(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return layer;
    Layer const& l = scene.layers[layer];
    bool const passBy = (m_tx.pegSelection && l.kind != Layer::Peg && l.kind != Layer::Camera) || (l.holdsDrawings() && !l.animatable);
    if (!passBy)
        return layer;
    for (int guard = 0, up = scene.parentIndex(layer); up >= 0 && guard < 256; ++guard, up = scene.parentIndex(up))
        if (scene.layers[up].kind == Layer::Peg)
            return up;
    return l.holdsDrawings() && !l.animatable ? -1 : layer;
}

void Canvas::setTransformLayers(QVector<int> const& layers)
{
    QVector<int> kept;
    for (int l : layers)
        if (l >= 0 && l < m_document->scene().layers.size() && l != m_layer && !kept.contains(l))
            kept.push_back(l);
    if (kept == m_transformLayers)
        return;
    m_transformLayers = kept;
    update();
    emit transformLayersChanged(m_transformLayers);
}

QVector<QPair<int, Pose>> Canvas::posedOthers() const
{
    // The current layer's drag, seen in the scene, done to the other layers picked too.
    QVector<QPair<int, Pose>> out;
    if (m_transformLayers.isEmpty() || !(m_drag == DragPoseMove || m_drag == DragPoseScale || m_drag == DragPoseRotate || m_drag == DragPoseSkew))
        return out;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !m_world0.isInvertible())
        return out;
    PoseOverride main;
    main.layer = m_layer;
    main.frame = m_frame;
    main.pose = m_dragPose;
    QTransform const delta = m_world0.inverted() * scene.worldTransform(m_layer, m_frame, &main);
    for (int other : m_transformLayers) {
        if (other < 0 || other >= scene.layers.size() || other == m_layer || scene.isAncestor(m_layer, other))
            continue;
        bool carried = false;   // (one that hangs under another picked one moves with it already)
        for (int up : m_transformLayers)
            carried = carried || (up != other && scene.isAncestor(up, other));
        Layer const& l = scene.layers[other];
        if (carried || l.kind == Layer::Quadmap || l.kind == Layer::Group || l.kind == Layer::ColourCard)
            continue;
        Pose const was = scene.poseOf(other, m_frame);
        QPointF const pivot = scene.pivotOf(other, m_frame);
        QTransform const local = l.matrixFor(was, pivot);
        QTransform const world = scene.worldTransform(other, m_frame);
        if (!local.isInvertible())
            continue;
        QTransform const parent = local.inverted() * world;
        if (!parent.isInvertible())
            continue;
        QTransform const moved = world * delta * parent.inverted();
        Pose pose = was;
        if (!Layer::poseOf(QTransform::fromTranslate(pivot.x(), pivot.y()) * moved * QTransform::fromTranslate(-pivot.x(), -pivot.y()), pose))
            continue;
        pose.rotation = l.limitedAngle(was.rotation + std::remainder(pose.rotation - was.rotation, 360.0));
        out.push_back({other, pose});
    }
    return out;
}

bool Canvas::penUpLayerPick()
{
    m_drag = DragNone;
    QPolygonF const area = m_tx.lasso ? m_layerPick : QPolygonF(QRectF(m_layerPick.value(0), m_layerPick.value(m_layerPick.size() - 1)).normalized());
    m_layerPick.clear();
    update();
    if (area.boundingRect().width() < 3.0 && area.boundingRect().height() < 3.0) {
        setTransformLayers({});
        return false;
    }
    Scene const& scene = m_document->scene();
    QVector<int> picked;
    for (int l : scene.drawOrder(m_frame)) {
        Layer const& layer = scene.layers[l];
        if (!layer.visible || layer.locked || !layer.holdsDrawings())
            continue;
        QRectF const own = layerBounds(l);
        if (own.isNull())
            continue;
        QPolygonF const seen = (worldOf(l, m_frame) * viewTransform()).map(QPolygonF(own));
        int const target = animationTarget(l);
        if (target >= 0 && !picked.contains(target) && area.intersects(seen))
            picked.push_back(target);
    }
    if (picked.isEmpty()) {
        setTransformLayers({});
        return false;
    }
    emit layerPicked(picked.first());   // comes back through setCurrentCell
    setTransformLayers(picked.mid(1));
    return false;
}

void Canvas::onActionPegSelectionMode(bool on)
{
    m_tx.pegSelection = on;
    emit message(on ? tr("Peg Selection Mode: clicking a drawing picks the peg it is attached to.") : QString());
}

void Canvas::onActionLockSelection(bool on)
{
    m_tx.lockSelection = on;
    emit message(on ? tr("Selection locked: clicks in the view no longer pick other layers.") : QString());
}

void Canvas::onActionShowTransformControls(bool on)
{
    m_tx.hideControls = !on;
    update();
}

void Canvas::onActionDisablePivotManipulator(bool on)
{
    m_tx.pivotFixed = on;
    update();
}

void Canvas::onActionMovePivotToCursor()
{
    // The permanent pivot of the current layer (or peg), where the pointer is.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_drawingView)
        return;
    QPointF const widgetPos = mapFromGlobal(QCursor::pos());
    if (!rect().contains(widgetPos.toPoint())) {
        emit message(tr("Point at where the pivot should go, in the Camera view, then press the shortcut."));
        return;
    }
    QTransform const world = scene.worldTransform(m_layer, m_frame);
    if (!world.isInvertible())
        return;
    m_document->setPivot(m_layer, world.inverted().map(toScene(widgetPos)));
    update();
}

void Canvas::nudgeLayer(QPointF direction, bool far)
{
    // A pixel on screen (times the nudging factor), ten with Shift: the layer moved by that, in what it hangs on.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    double const step = m_tx.nudge * (far ? 10.0 : 1.0) / std::max(m_zoom, 1e-6);
    QVector<int> layers{m_layer};
    layers += m_transformLayers;
    m_document->beginMacro(tr("Nudge"));
    for (int l : std::as_const(layers)) {
        int const target = animationTarget(l);
        if (target < 0 || target >= scene.layers.size())
            continue;
        int const parent = scene.parentIndex(target);
        QTransform const up = parent >= 0 ? scene.worldTransform(parent, m_frame) : QTransform();
        if (!up.isInvertible())
            continue;
        QPointF const at = toScene(QPointF(width() / 2.0, height() / 2.0));
        QPointF const by = up.inverted().map(at + direction * step) - up.inverted().map(at);
        Pose const was = m_document->scene().poseOf(target, m_frame);
        Pose now = was;
        now.x += by.x();
        now.y += by.y();
        if (!m_document->animatePose(target, m_frame, was, now, tr("Nudge"), Keyframe::AllParts, !m_animate))
            emit message(tr("Edit the linked column's expression or path in the Function Editor to change this value."));
    }
    m_document->endMacro();
}

// ---- the Drawing Pivot tool --------------------------------------------------------------------

void Canvas::drawingPivotDown(QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[m_layer];
    int const index = layer.holdsDrawings() ? layer.drawingAt(m_frame) : Layer::kNoDrawing;
    if (index == Layer::kNoDrawing) {
        emit message(tr("The Drawing Pivot tool sets the pivot of a drawing: go to a frame that shows one."));
        return;
    }
    QPointF const at = toLayer(widgetPos);
    bool const all = m_pivotAllDrawings;
    int const target = m_layer;
    m_document->editLayers({target}, all ? tr("Set Pivot on Frame Range") : tr("Set Pivot"), [index, at, all](Layer& l) {
        for (int d = 0; d < l.drawings.size(); ++d) {
            if (d == index || all) {
                l.drawings[d].pivotSet = true;
                l.drawings[d].pivot = at;
            }
        }
        if (l.embeddedPivot == Layer::PivotNotUsed)
            l.embeddedPivot = Layer::PivotOnLayer;      // (else the pivot set would not be used)
    });
    update();
}

void Canvas::onActionResetDrawingPivot()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    int const index = scene.layers[m_layer].drawingAt(m_frame);
    bool const all = m_pivotAllDrawings;
    if (index == Layer::kNoDrawing && !all)
        return;
    m_document->editLayers({m_layer}, tr("Reset Drawing Pivot"), [index, all](Layer& l) {
        for (int d = 0; d < l.drawings.size(); ++d)
            if (d == index || all)
                l.drawings[d].pivotSet = false;
    });
    update();
}

void Canvas::paintDrawingPivot(QPainter& painter)
{
    // Where the current drawing's own pivot is (or, with none, the layer's), as a ring with a cross.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[m_layer];
    int const index = layer.holdsDrawings() ? layer.drawingAt(m_frame) : Layer::kNoDrawing;
    bool const own = index != Layer::kNoDrawing && layer.drawings[index].pivotSet;
    QPointF const pivot = layerViewTransform().map(own ? layer.drawings[index].pivot : scene.pivotOf(m_layer, m_frame));
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    double const ring = std::max(3.0, m_tx.pointSize / 2.0);
    QColor const dark = InterfaceColours::get("pivotDark"), light = own ? InterfaceColours::get("pivotLight") : QColor(160, 160, 160);
    painter.setBrush(Qt::NoBrush);
    for (int pass = 0; pass < 2; ++pass) {
        painter.setPen(QPen(pass == 0 ? dark : light, pass == 0 ? 3.0 : 1.5));
        painter.drawEllipse(pivot, ring, ring);
        painter.drawLine(pivot - QPointF(ring * 1.7, 0), pivot + QPointF(ring * 1.7, 0));
        painter.drawLine(pivot - QPointF(0, ring * 1.7), pivot + QPointF(0, ring * 1.7));
    }
}

// ---- along the motion path -----------------------------------------------------------------------

bool Canvas::stepPathPoint(int direction, int* frame)
{
    // The keyframes and control points of the current layer's path, in order along it: the one before or after what is picked now.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    Layer const& layer = scene.layers[m_layer];
    struct Place { int frame, key, control; };
    QVector<Place> places;
    for (int k = 0; k < layer.keys.size(); ++k) {
        places.push_back({layer.keys[k].frame, k, -1});
        if (k + 1 < layer.keys.size())
            for (int c = 0; c < layer.keys[k].controls.size(); ++c)
                places.push_back({scene.controlPointFrame(m_layer, k, c), k, c});
    }
    if (places.isEmpty())
        return false;
    int now = -1;
    for (int i = 0; i < places.size(); ++i)
        if (places[i].control >= 0 ? (places[i].key == m_cpKey && places[i].control == m_cpIndex) : (m_cpIndex < 0 && places[i].frame == m_frame))
            now = i;
    int next = -1;
    if (now >= 0) {
        next = now + direction;
    } else {
        // Nothing picked: the nearest one before or after this frame.
        for (int i = 0; i < places.size(); ++i) {
            if (direction > 0 && places[i].frame > m_frame) {
                next = i;
                break;
            }
            if (direction < 0 && places[i].frame < m_frame)
                next = i;
        }
    }
    if (next < 0 || next >= places.size())
        return false;
    Place const& to = places[next];
    if (to.control >= 0) {
        m_showControl = true;
        pickControlPoint(to.key, to.control);
    } else {
        pickControlPoint(-1, -1);
    }
    *frame = to.frame;
    update();
    return true;
}

bool Canvas::pickNearestControlPoint()
{
    // The control point (of the current layer's path) closest to the pointer.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    QPointF const pointer = mapFromGlobal(QCursor::pos());
    Layer const& layer = scene.layers[m_layer];
    int bestKey = -1, bestControl = -1;
    double best = 1e18;
    for (int k = 0; k + 1 < layer.keys.size(); ++k) {
        for (int c = 0; c < layer.keys[k].controls.size(); ++c) {
            QPointF const at = controlPointPos(k, c);
            double const d = std::hypot(at.x() - pointer.x(), at.y() - pointer.y());
            if (d < best)
                best = d, bestKey = k, bestControl = c;
        }
    }
    if (bestKey < 0)
        return false;
    m_showControl = true;
    pickControlPoint(bestKey, bestControl);
    update();
    return true;
}
