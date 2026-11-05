// Eclipse - the Camera view: gradient handles, warp points, bones and inverse kinematics.
// and the Edit Gradient tool.
#include "canvas/Canvas.h"

#include <QPainter>

#include <QHash>
#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

bool Canvas::gradientValid() const
{
    Drawing const* const d = currentDrawing();
    if (d && m_gradStroke >= 0 && m_gradStroke < d->strokes.size()) {
        Swatch const* const s = m_document->scene().findSwatch(d->strokes[m_gradStroke].swatch);
        return s && s->isPlaced();
    }
    if (!d || m_gradFill < 0 || m_gradFill >= d->fills.size())
        return false;
    Swatch const* const s = m_document->scene().findSwatch(d->fills[m_gradFill].swatch);
    return s && s->isPlaced();
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
        if (m_gradStroke >= 0)
            scene.gradientLine(d->strokes[m_gradStroke], from, to);
        else
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
    // A line drawn in a gradient colour, under the pointer (lines lie over the paint) ...
    for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
        Swatch const* const s = scene.findSwatch(d->strokes[i].swatch);
        if (!s || !s->isPlaced() || !d->strokes[i].hit(p, std::max(double(d->strokes[i].width) / 2.0, 4.0 / std::max(m_zoom, 1e-6))))
            continue;
        m_gradStroke = i;
        m_gradFill = -1;
        m_gradHandle = 0;
        scene.gradientLine(d->strokes[i], m_gradFrom, m_gradTo);
        m_eraseArt = *d;
        m_drag = DragGradient;
        update();
        return;
    }
    m_gradStroke = -1;
    // ... or the topmost area under it that is painted with one.
    for (int i = int(d->fills.size()) - 1; i >= 0; --i) {
        Swatch const* const s = scene.findSwatch(d->fills[i].swatch);
        if (!s || !s->isPlaced() || !d->fills[i].contains(p))
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
    emit message(tr("Click an area painted with a gradient or a texture colour. (A colour is made one in the Colour view: right-click it.)"));
    update();
}

void Canvas::paintGradientAids(QPainter& painter)
{
    if (!gradientValid())
        return;
    QPointF from = m_gradFrom, to = m_gradTo;
    if (m_drag != DragGradient || !m_gradMoved) {
        if (m_gradStroke >= 0)
            m_document->scene().gradientLine(currentDrawing()->strokes[m_gradStroke], from, to);
        else
            m_document->scene().gradientLine(currentDrawing()->fills[m_gradFill], from, to);
    }
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
        bool const together = m_tool == Transform && m_controlSelect && m_layer >= 0 && m_layer < m_document->scene().layers.size()
                              && m_document->scene().layers[m_layer].id == m_warpPickedLayer && m_warpPicked.contains(i * 3);
        painter.setBrush(i * 3 == picked || together ? QColor(Qt::white) : line);
        painter.drawEllipse(toWidget.map(points[i * 3]), i == 0 ? 6.0 : 5.0, i == 0 ? 6.0 : 5.0);
    }
}

void Canvas::paintBones(QPainter& painter, QVector<QPointF> const& joints, int picked)
{
    if (joints.isEmpty())
        return;
    QTransform const toWidget = layerViewTransform();
    QColor const bone = m_boneColour, dark(20, 40, 28);
    painter.resetTransform();
    for (int i = 0; i + 1 < joints.size(); ++i) {
        // A bone is a long kite: wide near where it starts, a point where it ends (shown as joints: a line between them).
        QPointF const a = toWidget.map(joints[i]), b = toWidget.map(joints[i + 1]);
        if (m_bonesAsJoints) {
            painter.setPen(QPen(dark, 3.0));
            painter.drawLine(a, b);
            painter.setPen(QPen(bone, 1.4));
            painter.drawLine(a, b);
            continue;
        }
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
        painter.setBrush(i == picked ? m_boneSelectedColour : bone);
        painter.drawEllipse(toWidget.map(joints[i]), i == 0 ? 6.0 : 4.5, i == 0 ? 6.0 : 4.5);
    }
}

void Canvas::boneIkMove(QPointF widgetPos)
{
    Scene const& now = m_document->scene();
    if (m_ikChain.isEmpty() || m_ikGrabbed < 0 || m_ikGrabbed >= now.layers.size())
        return;
    for (int l : std::as_const(m_ikChain))
        if (l < 0 || l >= now.layers.size())
            return;
    // The scene's IK, with this view's iterations and nearness (Preferences).
    Scene::IkOptions options = Scene::ikOptions();
    options.iterations = m_ikIterations;
    options.error = m_ikError;
    Scene::setIkOptions(options);
    Scene posed = now;
    QVector<int> touched;
    posed.solveIK(m_ikGrabbed, m_ikLocal, toScene(widgetPos), m_frame, &touched);
    // What changed goes to the scene as it is dragged, all of it one step when the pen lifts.
    m_committing = true;
    for (int l : std::as_const(touched)) {
        Pose const pose = posed.layers[l].poseAt(m_frame);
        if (pose == m_document->scene().layers[l].poseAt(m_frame))
            continue;
        if (!m_ikBegun) {
            m_ikBegun = true;
            m_document->beginMacro(tr("Inverse Kinematics"));
        }
        m_document->setKeyframe(l, m_frame, pose, tr("Inverse Kinematics"));
        if (m_ikEase != 0)
            m_document->setKeyEase(l, m_frame, (m_ikEase & 1) ? 0.5 : 0.0, (m_ikEase & 2) ? 0.5 : 0.0);
    }
    m_committing = false;
    update();
}

void Canvas::ikDown(QPointF widgetPos)
{
    // Bone Selection Mode: the part under the pointer taken hold of, whichever layer is current.
    if (m_ikPickAny) {
        int const under = layerAt(widgetPos);
        if (under >= 0 && under != m_layer) {
            emit layerPicked(under);   // (comes back through setCurrentCell)
            if (m_layer != under)
                return;
        }
    }
    Scene const& scene = m_document->scene();
    if (scene.layers[m_layer].kind == Layer::Bone) {
        if (scene.layers[m_layer].ikNail) {
            emit message(tr("This bone is nailed (Layer Properties > IK): pull a bone that hangs below it."));
            return;
        }
        // A bone is pulled by its far end.
        m_ikGrabbed = m_layer;
        m_ikLocal = scene.layers[m_layer].pivot + QPointF(scene.layers[m_layer].boneLength, 0.0);
        m_ikChain = scene.ikChain(m_layer);
        m_ikBegun = false;
        m_drag = DragBoneIK;
        return;
    }
    int grabbed = -1;
    if (scene.layers[m_layer].bonesAt(m_frame).size() < 2) {
        int const parent = scene.parentIndex(m_layer);
        if (parent < 0 || scene.layers[parent].bonesAt(m_frame).size() < 2 || scene.layers[m_layer].parentBone < 0) {
            // A part of a hierarchy of layers (pegs, drawings): pulled where it was taken hold of, the layers it hangs from
            // turned to follow.
            QVector<int> const chain = scene.ikChain(m_layer);
            if (chain.isEmpty() || (chain.size() == 1 && parent < 0 && !scene.layers[m_layer].ikTranslateTop)) {
                emit message(scene.layers[m_layer].ikNail ? tr("This part is nailed (Rigging > IK Constraints): pull a part that hangs below it.")
                                                         : tr("Inverse Kinematics turns the layers a part hangs from: attach it to a peg or a parent layer first."));
                return;
            }
            QTransform const world = scene.worldTransform(m_layer, m_frame);
            if (!world.isInvertible())
                return;
            m_ikGrabbed = m_layer;
            m_ikLocal = world.inverted().map(toScene(widgetPos));
            m_ikChain = chain;
            m_ikBegun = false;
            m_drag = DragBoneIK;
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
        QVector<QPointF> const posed = Deform::posedJoints(layer.bonesAt(m_frame), m_dragPose.bend);
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
