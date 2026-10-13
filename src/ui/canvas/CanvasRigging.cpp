// Eclipse - the Camera view: bones, curve and envelope deformers, the motion path, Distribute to Layers,
// and the Edit Gradient tool.
#include "ui/canvas/Canvas.h"

#include <QPainter>

#include <QHash>
#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

// ---- bones --------------------------------------------------------------------------------
// The rigging tool lays the chain out on the artwork at rest: each click adds a joint (and
// so, from the second on, a bone), a joint can be dragged, Delete takes the last one off.
// The transform tool then bends it: drag a joint to turn the bone that ends there.

void Canvas::riggingDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers)
{
    Scene const& scene = m_document->scene();
    if (m_rigMode != RigBones) {
        // A curve or an envelope: a point or handle of it under the pointer is dragged; a
        // click anywhere else adds a point after the last.
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

// The way the layer goes: where its pivot is on every frame from its first keyframe to its
// last, with a mark on each keyframe.
void Canvas::paintMotionPath(QPainter& painter)
{
    Scene const& scene = m_document->scene();
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

// ---- the Edit Gradient tool ----------------------------------------------------------------
// Press on an area painted with a gradient colour and drag: the gradient runs from where the
// press was to where the pointer is let go (a round one: from its middle to its rim).  The
// area clicked stays in hand, with a handle at each end of its gradient to pull.

bool Canvas::gradientValid() const
{
    Drawing const* const d = currentDrawing();
    if (!d || m_gradFill < 0 || m_gradFill >= d->fills.size())
        return false;
    Swatch const* const s = m_document->scene().findSwatch(d->fills[m_gradFill].swatch);
    return s && s->isGradient();
}

void Canvas::gradientDown(QPointF widgetPos, QPointF p)
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!d) {
        emit message(tr("There is no drawing on this frame."));
        return;
    }
    m_gradMoved = false;
    m_gradStart = p;
    m_gradStartWidget = widgetPos;
    // A handle of the gradient in hand.
    if (gradientValid()) {
        QPointF from, to;
        scene.gradientLine(d->fills[m_gradFill], from, to);
        QTransform const toWidget = layerViewTransform();
        auto isAt = [&](QPointF layerPos) { return QLineF(toWidget.map(layerPos), widgetPos).length() <= 8.0; };
        int const handle = isAt(to) ? 2 : isAt(from) ? 1 : 0;
        if (handle) {
            m_gradHandle = handle;
            m_gradFrom = from;
            m_gradTo = to;
            m_eraseArt = *d;
            m_drag = DragGradient;
            return;
        }
    }
    // The topmost area under the pointer that is painted with a gradient.
    for (int i = int(d->fills.size()) - 1; i >= 0; --i) {
        Swatch const* const s = scene.findSwatch(d->fills[i].swatch);
        if (!s || !s->isGradient() || !d->fills[i].contains(p))
            continue;
        m_gradFill = i;
        m_gradHandle = 0;
        scene.gradientLine(d->fills[i], m_gradFrom, m_gradTo);
        m_eraseArt = *d;
        m_drag = DragGradient;
        update();
        return;
    }
    m_gradFill = -1;
    emit message(tr("Click an area painted with a gradient colour. (A colour is made a gradient in the Colour view: right-click it, Edit Gradient.)"));
    update();
}

void Canvas::paintGradientAids(QPainter& painter)
{
    if (!gradientValid())
        return;
    QPointF from = m_gradFrom, to = m_gradTo;
    if (m_drag != DragGradient || !m_gradMoved)
        m_document->scene().gradientLine(currentDrawing()->fills[m_gradFill], from, to);
    QTransform const toWidget = layerViewTransform();
    QPointF const a = toWidget.map(from), b = toWidget.map(to);
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(20, 20, 20), 3.5));
    painter.drawLine(a, b);
    painter.setPen(QPen(QColor(255, 255, 255), 1.5));
    painter.drawLine(a, b);
    // A ring where it starts, a square where it ends.
    painter.setPen(QPen(QColor(20, 20, 20), 1.2));
    painter.setBrush(m_drag == DragGradient && m_gradHandle == 1 ? QColor(255, 190, 70) : QColor(255, 255, 255));
    painter.drawEllipse(a, 5.5, 5.5);
    painter.setBrush(m_drag == DragGradient && m_gradHandle == 2 ? QColor(255, 190, 70) : QColor(255, 255, 255));
    painter.drawRect(QRectF(b - QPointF(5, 5), QSizeF(10, 10)));
}

int Canvas::warpAt(QVector<QPointF> const& points, QPointF widgetPos) const
{
    QTransform const toWidget = layerViewTransform();
    auto isAt = [&](QPointF layerPos) {
        QPointF const w = toWidget.map(layerPos);
        return std::abs(w.x() - widgetPos.x()) <= 7 && std::abs(w.y() - widgetPos.y()) <= 7;
    };
    int const count = int(points.size()) / 3;
    // The points before their handles: a handle of no length lies on its point.
    for (int i = count - 1; i >= 0; --i)
        if (isAt(points[i * 3]))
            return i * 3;
    for (int i = count - 1; i >= 0; --i)
        for (int h : {2, 1})
            if (!points[i * 3 + h].isNull() && isAt(points[i * 3] + points[i * 3 + h]))
                return i * 3 + h;
    return -1;
}

QVector<QPointF> Canvas::warpRestDragged() const
{
    QVector<QPointF> points = m_document->scene().layers[m_layer].warp;
    if (m_drag != DragWarpRest || m_warpIndex < 0 || m_warpIndex >= points.size())
        return points;
    if (m_warpIndex % 3 == 0)
        points[m_warpIndex] = m_dragNow;
    else
        points[m_warpIndex] = m_dragNow - points[m_warpIndex / 3 * 3];
    return points;
}

void Canvas::paintWarp(QPainter& painter, QVector<QPointF> const& points, bool closed, int picked)
{
    int const count = int(points.size()) / 3;
    if (count < 1)
        return;
    QTransform const toWidget = layerViewTransform();
    QColor const line(90, 220, 140), dark(20, 40, 28), handle(255, 190, 70);
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPolygonF curve = toWidget.map(Deform::warpLine(points, closed && count >= 3, 24));
    if (closed && count >= 3 && !curve.isEmpty())
        curve.push_back(curve.first());
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(dark, 3.5));
    painter.drawPolyline(curve);
    painter.setPen(QPen(line, 1.6));
    painter.drawPolyline(curve);
    for (int i = 0; i < count; ++i) {
        QPointF const at = toWidget.map(points[i * 3]);
        for (int h : {1, 2}) {
            if (points[i * 3 + h].isNull())
                continue;
            QPointF const end = toWidget.map(points[i * 3] + points[i * 3 + h]);
            painter.setPen(QPen(handle, 1));
            painter.drawLine(at, end);
            painter.setPen(QPen(dark, 1));
            painter.setBrush(i * 3 + h == picked ? QColor(Qt::white) : handle);
            painter.drawRect(QRectF(end - QPointF(3, 3), QSizeF(6, 6)));
        }
    }
    for (int i = 0; i < count; ++i) {
        painter.setPen(QPen(dark, 1));
        painter.setBrush(i * 3 == picked ? QColor(Qt::white) : line);
        painter.drawEllipse(toWidget.map(points[i * 3]), i == 0 ? 6.0 : 5.0, i == 0 ? 6.0 : 5.0);
    }
}

void Canvas::paintBones(QPainter& painter, QVector<QPointF> const& joints, int picked)
{
    if (joints.isEmpty())
        return;
    QTransform const toWidget = layerViewTransform();
    QColor const bone(90, 220, 140), dark(20, 40, 28);
    painter.resetTransform();
    for (int i = 0; i + 1 < joints.size(); ++i) {
        // A bone is a long kite: wide near where it starts, a point where it ends.
        QPointF const a = toWidget.map(joints[i]), b = toWidget.map(joints[i + 1]);
        QPointF const along = b - a;
        double const length = std::hypot(along.x(), along.y());
        if (length < 1.0)
            continue;
        QPointF const side = QPointF(-along.y(), along.x()) / length * std::min(7.0, length * 0.15);
        QPolygonF kite;
        kite << a << a + along * 0.18 + side << b << a + along * 0.18 - side;
        painter.setPen(QPen(dark, 1));
        painter.setBrush(QColor(bone.red(), bone.green(), bone.blue(), 110));
        painter.drawPolygon(kite);
    }
    for (int i = 0; i < joints.size(); ++i) {
        painter.setPen(QPen(dark, 1));
        painter.setBrush(i == picked ? QColor(Qt::white) : bone);
        painter.drawEllipse(toWidget.map(joints[i]), i == 0 ? 6.0 : 4.5, i == 0 ? 6.0 : 4.5);
    }
}

// Inverse kinematics tool: drag a joint and the bones before it turn so that it follows.
// Pressed on a layer that rides on a bone of its parent (a hand), it takes hold of the end
// of that bone instead, so that the hand can be pulled and the arm comes along.
void Canvas::ikDown(QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    int grabbed = -1;
    if (scene.layers[m_layer].bones.size() < 2) {
        int const parent = scene.parentIndex(m_layer);
        if (parent < 0 || scene.layers[parent].bones.size() < 2 || scene.layers[m_layer].parentBone < 0) {
            emit message(tr("This layer has no bones. Lay a chain on it with the Rigging tool first."));
            return;
        }
        grabbed = scene.layers[m_layer].parentBone + 1;
        emit layerPicked(parent);  // comes back through setCurrentCell
        if (m_layer != parent)
            return;
    }
    Layer const& layer = m_document->scene().layers[m_layer];
    m_poseStart = m_dragPose = layer.poseAt(m_frame);
    m_world0 = m_document->scene().worldTransform(m_layer, m_frame);
    m_dragStartScene = toScene(widgetPos);
    if (grabbed < 0) {
        QTransform const toWidget = m_world0 * viewTransform();
        QVector<QPointF> const posed = Deform::posedJoints(layer.bones, m_dragPose.bend);
        for (int j = int(posed.size()) - 1; j >= 1; --j) {
            QPointF const w = toWidget.map(posed[j]);
            if (std::abs(w.x() - widgetPos.x()) <= 8 && std::abs(w.y() - widgetPos.y()) <= 8) {
                grabbed = j;
                break;
            }
        }
    }
    if (grabbed >= 1) {
        m_bone = grabbed;
        m_drag = DragIK;
    }
}
