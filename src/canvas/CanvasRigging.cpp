// Eclipse - the Camera view: rigging, motion paths and their control points, distributing to layers; onion skin and backdrop settings.
// and the Edit Gradient tool.
#include "canvas/Canvas.h"

#include <QPainter>

#include <QHash>
#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

QVector<QPointF> Canvas::weightJoints() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return {};
    Layer const& layer = scene.layers[m_layer];
    if (layer.bones.size() >= 2)
        return layer.bones;
    return layer.bendOnBones ? Deform::gameBoneJoints(scene, m_layer, m_frame) : QVector<QPointF>();
}

void Canvas::riggingDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers)
{
    Scene const& scene = m_document->scene();
    if (m_rigMode == RigWeights) {
        // Painting the pull of a bone onto the drawing: a dab here, more as the pointer goes; with Alt, taking it away.
        QVector<QPointF> const joints = weightJoints();
        if (joints.size() < 2) {
            emit message(tr("Weights are painted for bones: lay down a bone chain on the layer first (Rigging tool, Mode: Bone), "
                            "or have it bend on the Game Bones it hangs from (Layer Properties)."));
            return;
        }
        if (m_weightBone >= joints.size() - 1) {
            emit message(tr("The chain has %n bone(s): choose one of them in the Tool Properties.", nullptr, int(joints.size()) - 1));
            return;
        }
        m_weightDabs = {SkinDab{m_weightBone, p, m_weightRadius, (modifiers & Qt::AltModifier) ? -m_weightStrength : m_weightStrength}};
        m_drag = DragWeights;
        update();
        return;
    }
    if (m_rigMode != RigBones) {
        Layer const& layer = scene.layers[m_layer];
        int const at = warpAt(layer.warp, widgetPos);
        if (at >= 0) {
            m_warpIndex = at;
            m_dragNow = at % 3 == 0 ? layer.warp[at] : layer.warp[at / 3 * 3] + layer.warp[at];
            m_drag = DragWarpRest;
            return;
        }
        // With Ctrl, on the curve: a point there, between the two it lies between.
        if ((modifiers & Qt::ControlModifier) && layer.hasWarp()) {
            QVector<QPointF> points = layer.warp;
            double along = 0.0;
            int const place = Deform::insertWarpPoint(points, layer.warpClosed, p, 8.0 / std::max(m_zoom, 1e-6), &along);
            if (place >= 0)
                m_document->insertWarpPoint(m_layer, points, place, along);
            else
                emit message(tr("Hold Ctrl and click on the curve itself to add a point there."));
            return;
        }
        // A deformer begun as the one kind stays that kind, whatever the tool is set to now.
        bool const closed = layer.warp.isEmpty() ? m_rigMode == RigEnvelope : layer.warpClosed;
        QVector<QPointF> points = layer.warp;
        points << p << QPointF() << QPointF();
        int const count = int(points.size()) / 3;
        for (int i : {count - 1, count - 2, 0})
            if (i >= 0 && (i != 0 || closed || count <= 2))
                Deform::smoothWarpHandles(points, closed, i);
        m_document->setWarp(m_layer, points, closed, count == 1 ? (closed ? tr("Start Envelope") : tr("Start Curve Deformer")) : tr("Add Deformer Point"));
        if (count == 1)
            emit message(closed ? tr("Click round the artwork: each click adds a point of the envelope (three at least). Then pull the points about with the Transform tool.")
                                : tr("Click along the artwork: each click adds a point of the curve it will bend along. Then pull the points about with the Transform tool."));
        return;
    }
    QVector<QPointF> joints = scene.layers[m_layer].bones;
    QTransform const toWidget = layerViewTransform();
    for (int i = int(joints.size()) - 1; i >= 0; --i) {
        QPointF const w = toWidget.map(joints[i]);
        if (std::abs(w.x() - widgetPos.x()) <= 7 && std::abs(w.y() - widgetPos.y()) <= 7) {
            m_bone = i;
            m_dragNow = joints[i];
            m_drag = DragJoint;
            return;
        }
    }
    joints.push_back(p);
    m_document->setBones(m_layer, joints, joints.size() == 1 ? tr("Start Bone Chain") : tr("Add Bone"));
    if (joints.size() == 1)
        emit message(tr("Click where the first bone should end. Each further click adds a bone; Delete removes the last."));
}

void Canvas::paintMotionPath(QPainter& painter)
{
    // A control point being dragged: the path is shown as it will be when it is let go.
    Scene dragged;
    bool const moving = m_drag == DragControlPoint && m_cpMoved && !m_cpDepth;
    if (moving) {
        dragged = m_document->scene();
        Layer& l = dragged.layers[m_layer];
        if (m_cpKey >= 0 && m_cpKey < l.keys.size() && m_cpIndex >= 0 && m_cpIndex < l.keys[m_cpKey].controls.size())
            l.keys[m_cpKey].controls[m_cpIndex] = controlPointFromView(m_cpKey, m_cpIndex, m_cpDragPos);
    }
    Scene const& scene = moving ? dragged : m_document->scene();
    Layer const& layer = scene.layers[m_layer];
    if (m_drawingView || layer.keys.size() < 2)
        return;
    QTransform const view = viewTransform();
    QPolygonF path;
    QVector<QPointF> keys;
    for (int f = layer.keys.first().frame; f <= layer.keys.last().frame; ++f) {
        QPointF const at = (scene.worldTransform(m_layer, f) * view).map(layer.pivot);
        path << at;
        if (layer.keyIndex(f) >= 0)
            keys << at;
    }
    if (path.boundingRect().width() < 1.0 && path.boundingRect().height() < 1.0)
        return;     // it does not go anywhere
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 3));
    painter.drawPolyline(path);
    painter.setPen(QPen(QColor(120, 200, 255), 1.4));
    painter.drawPolyline(path);
    // A tick on each frame, so that the speed can be read off; a square on each keyframe.
    painter.setPen(QPen(QColor(120, 200, 255), 1));
    for (QPointF const& at : std::as_const(path))
        painter.drawEllipse(at, 1.5, 1.5);
    for (QPointF const& at : std::as_const(keys)) {
        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(QColor(120, 200, 255));
        painter.drawRect(QRectF(at - QPointF(3.5, 3.5), QSizeF(7, 7)));
    }
    // A ring on each control point; the one picked is filled.
    for (int k = 0; k + 1 < layer.keys.size(); ++k) {
        for (int c = 0; c < layer.keys[k].controls.size(); ++c) {
            bool const picked = k == m_cpKey && c == m_cpIndex;
            QPointF const at = picked && m_drag == DragControlPoint && m_cpMoved && !m_cpDepth ? m_cpDragPos : controlPointPos(k, c);
            painter.setPen(QPen(QColor(20, 20, 20), 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(at, 4.5, 4.5);
            painter.setPen(QPen(QColor(255, 190, 70), 1.6));
            painter.setBrush(picked ? QBrush(QColor(255, 190, 70)) : QBrush(Qt::NoBrush));
            painter.drawEllipse(at, 4.5, 4.5);
        }
    }
}

// From where a layer's pivot would be, in what the layer hangs on, to the view - on the frame the layer comes by the point.
static bool controlFrame(Scene const& scene, int layerIndex, int key, int control, QTransform const& view, int* frame, QTransform* toView)
{
    if (layerIndex < 0 || layerIndex >= scene.layers.size())
        return false;
    Layer const& layer = scene.layers[layerIndex];
    if (key < 0 || key + 1 >= layer.keys.size() || control < 0 || control >= layer.keys[key].controls.size())
        return false;
    *frame = scene.controlPointFrame(layerIndex, key, control);
    if (*frame < 0)
        return false;
    QTransform const local = layer.localTransform(*frame);
    if (!local.isInvertible())
        return false;
    *toView = local.inverted() * scene.worldTransform(layerIndex, *frame) * view;
    return true;
}

QPointF Canvas::controlPointPos(int key, int control) const
{
    Scene const& scene = m_document->scene();
    int frame = -1;
    QTransform toView;
    if (!controlFrame(scene, m_layer, key, control, viewTransform(), &frame, &toView))
        return QPointF();
    Layer const& layer = scene.layers[m_layer];
    Pose pose = layer.poseAt(frame);
    pose.x = layer.keys[key].controls[control].x();
    pose.y = layer.keys[key].controls[control].y();
    return (layer.matrixFor(pose) * toView).map(layer.pivot);
}

QPointF Canvas::controlPointFromView(int key, int control, QPointF widgetPos) const
{
    Scene const& scene = m_document->scene();
    int frame = -1;
    QTransform toView;
    if (!controlFrame(scene, m_layer, key, control, viewTransform(), &frame, &toView) || !toView.isInvertible())
        return QPointF();
    Layer const& layer = scene.layers[m_layer];
    // Where the pivot would be with the layer not moved at all: the point is as far from there as the pointer is.
    Pose still = layer.poseAt(frame);
    still.x = 0.0;
    still.y = 0.0;
    return toView.inverted().map(widgetPos) - layer.matrixFor(still).map(layer.pivot);
}

bool Canvas::controlPointAt(QPointF widgetPos, int* key, int* control) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    Layer const& layer = scene.layers[m_layer];
    for (int k = 0; k + 1 < layer.keys.size(); ++k) {
        for (int c = 0; c < layer.keys[k].controls.size(); ++c) {
            QPointF const at = controlPointPos(k, c);
            if (std::hypot(at.x() - widgetPos.x(), at.y() - widgetPos.y()) <= 7.0) {
                *key = k;
                *control = c;
                return true;
            }
        }
    }
    return false;
}

void Canvas::pickControlPoint(int key, int control)
{
    if (key == m_cpKey && control == m_cpIndex)
        return;
    m_cpKey = key;
    m_cpIndex = control;
    update();
    emit controlPointPicked(key, control);
}

void Canvas::onActionLockInTime()
{
    int const frame = m_document->lockControlPointInTime(m_layer, m_cpKey, m_cpIndex);
    if (frame < 0) {
        emit message(tr("Pick a control point of the motion path first (Show Control, then the Transform tool)."));
        return;
    }
    pickControlPoint(-1, -1);
    emit message(tr("The control point is a keyframe on frame %1.").arg(frame + 1));
}

void Canvas::onActionRemoveControlPoint()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_cpKey < 0 || m_cpKey >= scene.layers[m_layer].keys.size()
        || m_cpIndex < 0 || m_cpIndex >= scene.layers[m_layer].keys[m_cpKey].controls.size()) {
        emit message(tr("Pick a control point of the motion path first (Show Control, then the Transform tool)."));
        return;
    }
    m_document->removeControlPoint(m_layer, m_cpKey, m_cpIndex);
    pickControlPoint(-1, -1);
}

void Canvas::onActionDistributeToLayers()
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection() || m_layer < 0 || m_layer >= scene.layers.size()) {
        emit message(tr("Select the things to distribute with the Select tool first."));
        return;
    }
    // What goes together: the things of one group, or one thing on its own.
    struct Piece {
        QVector<int> strokes, fills;
    };
    QVector<Piece> pieces;
    QHash<int, int> pieceOfGroup;
    auto pieceFor = [&](int group) -> Piece& {
        if (group > 0 && pieceOfGroup.contains(group))
            return pieces[pieceOfGroup.value(group)];
        if (group > 0)
            pieceOfGroup.insert(group, int(pieces.size()));
        pieces.push_back(Piece());
        return pieces.last();
    };
    for (int i : std::as_const(m_selFills))
        pieceFor(d->fills[i].group).fills.push_back(i);
    for (int i : std::as_const(m_selStrokes))
        pieceFor(d->strokes[i].group).strokes.push_back(i);
    if (pieces.size() < 2) {
        emit message(tr("There is only one thing selected: there is nothing to distribute."));
        return;
    }
    Drawing kept;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            kept.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            kept.fills.push_back(d->fills[i]);
    QVector<Drawing> parts;
    for (Piece const& piece : std::as_const(pieces)) {
        Drawing part;
        for (int i : piece.fills) {
            part.fillsOf(sheetOf(m_art)).push_back(d->fills[i]);
            part.fillsOf(sheetOf(m_art)).last().group = 0;
        }
        for (int i : piece.strokes) {
            part.strokesOf(sheetOf(m_art)).push_back(d->strokes[i]);
            part.strokesOf(sheetOf(m_art)).last().group = 0;
        }
        parts.push_back(part);
    }
    // Each for as long as the drawing they come from is shown.
    int const source = m_layer, frame = m_frame;
    int const start = std::max(0, scene.layers[source].exposureStart(frame));
    int length = 1;
    while (start + length < scene.frameCount() && scene.layers[source].drawingAt(start + length) == scene.layers[source].drawingAt(start))
        ++length;
    QString const name = scene.layers[source].name;
    m_document->beginMacro(tr("Distribute to Layers"));
    commitDrawing(kept, tr("Distribute to Layers"));
    int above = source;
    for (int i = 0; i < parts.size(); ++i) {
        int const at = m_document->addLayer(above);
        m_document->renameLayer(at, m_document->scene().uniqueLayerName(QStringLiteral("%1_%2").arg(name).arg(i + 1)));
        m_document->setDrawing(at, start, parts[i], tr("Distribute to Layers"));
        m_document->setExposure(at, start, length);
        // Attached to the layer they came from, they stay where they were and go where it goes.
        m_document->setParent(at, source);
        above = at;
    }
    m_document->endMacro();
    setSelection({}, {});
    emit message(tr("%n thing(s) put on layers of their own, attached to %1.", nullptr, int(parts.size())).arg(name));
}

void Canvas::onActionCreateDrawingFromSelection()
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection() || m_layer < 0 || m_layer >= scene.layers.size()) {
        emit message(tr("Select the artwork to make a drawing of with the Select tool first."));
        return;
    }
    Drawing kept, part;
    for (int i = 0; i < d->strokes.size(); ++i)
        (m_selStrokes.contains(i) ? part.strokesOf(sheetOf(m_art)) : kept.strokes).push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        (m_selFills.contains(i) ? part.fillsOf(sheetOf(m_art)) : kept.fills).push_back(d->fills[i]);
    // For as long as the drawing it comes from is shown.
    int const source = m_layer, frame = m_frame;
    int const start = std::max(0, scene.layers[source].exposureStart(frame));
    int length = 1;
    while (start + length < scene.frameCount() && scene.layers[source].drawingAt(start + length) == scene.layers[source].drawingAt(start))
        ++length;
    QString const name = scene.layers[source].name;
    m_document->beginMacro(tr("Create Drawing from Selection"));
    clearSelection();
    commitDrawing(kept, tr("Create Drawing from Selection"));
    int const at = m_document->addLayer(source);
    m_document->renameLayer(at, m_document->scene().uniqueLayerName(QStringLiteral("%1_1").arg(name)));
    m_document->setDrawing(at, start, part, tr("Create Drawing from Selection"));
    m_document->setExposure(at, start, length);
    m_document->setParent(at, source);
    m_document->endMacro();
    emit message(tr("The selection is a drawing of its own now, on the layer %1.").arg(m_document->scene().layers[at].name));
}

void Canvas::setPreview(QVector<QImage> const& frames)
{
    m_preview = frames;
    update();
}

void Canvas::setOnionMark(int mark)
{
    if (m_onionMark == mark)
        return;
    m_onionMark = mark;
    emit onionChanged();
    update();
}

void Canvas::setOnionOutlines(bool on)
{
    if (m_onionOutlines == on)
        return;
    m_onionOutlines = on;
    emit onionChanged();
    update();
}

void Canvas::setOnionByFrame(bool on)
{
    if (m_onionByFrame == on)
        return;
    m_onionByFrame = on;
    emit onionChanged();
    update();
}

void Canvas::setOnionColours(QColor const& before, QColor const& after)
{
    if (!before.isValid() || !after.isValid() || (before == m_onionBefore && after == m_onionAfter))
        return;
    m_onionBefore = before;
    m_onionAfter = after;
    emit onionChanged();
    update();
}

void Canvas::setBackdrop(QColor const& colour)
{
    if (!colour.isValid() || colour == m_backdrop)
        return;
    m_backdrop = colour;
    update();
}

void Canvas::setLightTableWash(int percent)
{
    m_lightWash = std::clamp(percent, 10, 90);
    update();
}

void Canvas::setOnionStepColour(bool after, int step, QColor const& colour)
{
    if (step < 0 || step >= kOnionSteps || m_onionStepColour[after ? 1 : 0][step] == colour)
        return;
    m_onionStepColour[after ? 1 : 0][step] = colour;
    emit onionChanged();
    update();
}

QColor Canvas::onionStepColour(bool after, int step) const
{
    if (onionStepHasColour(after, step))
        return m_onionStepColour[after ? 1 : 0][step];
    return after ? m_onionAfter : m_onionBefore;
}

void Canvas::alsoInOtherDrawings(QRectF const& region, QTransform const* transform, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || region.isNull())
        return;
    // A copy: the scene changes under us with every drawing set.
    QVector<Drawing> const drawings = scene.layers[m_layer].drawings;
    QVector<int> const cells = scene.layers[m_layer].cells;
    QRectF const within = region.adjusted(-1.5, -1.5, 1.5, 1.5);
    int const sheet = sheetOf(m_art);
    QVector<bool> done(drawings.size(), false);
    if (m_frame >= 0 && m_frame < cells.size() && cells[m_frame] >= 0 && cells[m_frame] < done.size())
        done[cells[m_frame]] = true;    // the one in hand has been seen to
    m_committing = true;
    for (int f = 0; f < cells.size(); ++f) {
        int const index = cells[f];
        if (index < 0 || index >= drawings.size() || done[index])
            continue;
        done[index] = true;
        Drawing whole = drawings[index];
        bool changed = false;
        QVector<Stroke>& strokes = whole.strokesOf(sheet);
        for (int i = int(strokes.size()) - 1; i >= 0; --i) {
            if (!within.contains(strokes[i].bounds()))
                continue;
            changed = true;
            if (transform)
                strokes[i].transform(*transform);
            else
                strokes.removeAt(i);
        }
        QVector<Fill>& fills = whole.fillsOf(sheet);
        for (int i = int(fills.size()) - 1; i >= 0; --i) {
            if (!within.contains(fills[i].bounds()))
                continue;
            changed = true;
            if (transform)
                fills[i].transform(*transform);
            else
                fills.removeAt(i);
        }
        if (changed)
            m_document->setDrawing(m_layer, f, whole, text);
    }
    m_committing = false;
}
