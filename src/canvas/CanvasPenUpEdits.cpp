// Eclipse - the Camera view: the pen let go after editing - contour points and handles, paint, marquees, rigs, poses, the selection.
#include "canvas/Canvas.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QSet>
#include <QInputDialog>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <numeric>
#include "model/document/Commands.h"

bool Canvas::penUpSegment(Drag drag)
{
    Q_UNUSED(drag);
    bool const moved = QLineF(m_dragStart, m_dragNow).length() * m_zoom > 1.0;
    Drawing const edited = moved ? previewDrawing() : Drawing();
    m_drag = DragNone;
    if (moved)
        commitDrawing(edited, tr("Bend Line"));
    m_segIndex = -1;
    update();
    return false;
}

bool Canvas::penUpContourBox(Drag drag)
{
    Q_UNUSED(drag);
    bool const moved = contourValid() && QLineF(m_dragStart, m_dragNow).length() * m_zoom > 1.0;
    Drawing const edited = moved ? previewDrawing() : Drawing();
    m_drag = DragNone;
    if (moved)
        commitDrawing(edited, m_ceBoxCorner == 4 ? tr("Rotate Points") : tr("Scale Points"));
    m_ceBoxCorner = -1;
    update();
    return false;
}

bool Canvas::penUpPoint(Drag drag)
{
    Q_UNUSED(drag);
    if (contourValid() && contourLine(*currentDrawing())->points[m_cePoint].pos != m_dragNow) {
        Drawing edited = previewDrawing();
        m_drag = DragNone;
        // An end dropped on an end: the line closes, or two lines become one.
        bool const joined = joinDroppedEnd(edited) || jointDroppedPoint(edited);
        int const stroke = m_ceStroke, point = m_cePoint;
        commitDrawing(edited, joined ? tr("Join Points") : tr("Move Point"));
        if (joined) {
            m_ceStroke = stroke;
            m_cePoint = point;
            m_ceFill = -1;
        }
    }
    return false;
}

bool Canvas::penUpBothHandles(Drag drag)
{
    Q_UNUSED(drag);
    if (contourValid()) {
        bool const pulled = pulledHandles();
        Drawing edited = pulled ? previewDrawing() : *currentDrawing();
        m_drag = DragNone;
        int const stroke = m_ceStroke, point = m_cePoint, fill = m_ceFill, contour = m_ceContour;
        QString name = tr("Pull Handles");
        if (!pulled) {
            // Only clicked: a corner becomes smooth, a smooth point a corner (and the points lying on it with it).
            QVector<QPair<int, int>> const stacked = stackedPoints(edited);
            bool const corner = contourLine(edited)->isCorner(point);
            for (auto const& [line, at] : stacked) {
                Stroke& target = line < 0 || m_ceFill >= 0 ? *contourLine(edited) : edited.strokes[line];
                target.setCorner(at, !corner);
            }
            contourEdited(edited);
            name = corner ? tr("Smooth Point") : tr("Corner Point");
        }
        commitDrawing(edited, name);
        m_ceStroke = stroke;     // the point stays picked, with its handles showing
        m_cePoint = point;
        m_ceFill = fill;
        m_ceContour = contour;
    }
    m_ceHandle = -1;
    return false;
}

bool Canvas::penUpHandle(Drag drag)
{
    Q_UNUSED(drag);
    if (contourValid() && m_dragNow != m_dragStart) {
        Drawing const edited = previewDrawing();
        m_drag = DragNone;
        int const stroke = m_ceStroke, point = m_cePoint, fill = m_ceFill, contour = m_ceContour;
        commitDrawing(edited, tr("Move Handle"));
        m_ceStroke = stroke;     // the point stays picked, with its handles showing
        m_cePoint = point;
        m_ceFill = fill;
        m_ceContour = contour;
    }
    m_ceHandle = -1;
    return false;
}

bool Canvas::penUpPaint(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
    QPolygonF const shape = m_lassoPath;
    m_lassoPath.clear();
    QRectF const reach = m_dragLasso ? shape.boundingRect() : rect;
    bool const lasso = m_dragLasso;
    QPointF const at = m_dragStart;
    auto paint = [&] {
        if (reach.width() * m_zoom <= 3 && reach.height() * m_zoom <= 3)
            paintAt(at);
        else if (lasso)
            paintInside([&shape](QPointF q) { return shape.containsPoint(q, Qt::OddEvenFill); }, shape.boundingRect());
        else
            paintInside([&rect](QPointF q) { return rect.contains(q); }, rect);
    };
    Scene const& scene = m_document->scene();
    QVector<QPair<int, QTransform>> others;
    if (m_paintLayers && isPaintTool(m_tool) && m_layer >= 0 && m_layer < scene.layers.size()) {
        for (int l = 0; l < scene.layers.size(); ++l) {
            if (l == m_layer || !scene.layers[l].holdsDrawings() || scene.layers[l].locked || !scene.shows(l))
                continue;
            QTransform const theirs = worldOf(l, m_frame);
            if (theirs.isInvertible())
                others.push_back({l, worldOf(m_layer, m_frame) * theirs.inverted()});
        }
    }
    if (!others.isEmpty()) {
        bool const small = reach.width() * m_zoom <= 3 && reach.height() * m_zoom <= 3;
        int const mine = m_layer, here = m_frame;
        m_document->beginMacro(m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
        others.push_front({mine, QTransform()});
        for (auto const& [l, to] : std::as_const(others)) {
            // (With Apply to All Drawings as well: every drawing of each of them.)
            QVector<int> frames = {here};
            if (m_paintAll) {
                frames.clear();
                QVector<int> const cells = m_document->scene().layers[l].cells;
                QVector<int> seen;
                for (int f = 0; f < cells.size(); ++f) {
                    if (cells[f] == Layer::kNoDrawing || seen.contains(cells[f]))
                        continue;
                    seen.push_back(cells[f]);
                    frames.push_back(f);
                }
            }
            QPolygonF const there = to.map(lasso ? shape : QPolygonF(rect));
            m_layer = l;
            for (int f : std::as_const(frames)) {
                if (m_document->scene().layers[l].drawingAt(f) == Layer::kNoDrawing)
                    continue;
                m_frame = f;
                if (small)
                    paintAt(to.map(at));
                else
                    paintInside([&there](QPointF q) { return there.containsPoint(q, Qt::OddEvenFill); }, there.boundingRect());
            }
        }
        m_layer = mine;
        m_frame = here;
        m_document->endMacro();
        update();
    } else if (!m_paintAll || m_layer < 0 || m_layer >= scene.layers.size()) {
        paint();
    } else {
        // The same in every drawing of the layer, as one step: each drawing is come to in turn, by a frame that shows it.
        QVector<int> const cells = scene.layers[m_layer].cells;
        QVector<int> frames, seen;
        for (int f = 0; f < cells.size(); ++f) {
            if (cells[f] == Layer::kNoDrawing || seen.contains(cells[f]))
                continue;
            seen.push_back(cells[f]);
            frames.push_back(f);
        }
        int const here = m_frame;
        m_document->beginMacro(m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
        for (int f : std::as_const(frames)) {
            m_frame = f;
            paint();
        }
        m_frame = here;
        m_document->endMacro();
    }
    return false;
}

bool Canvas::penUpPointMarquee(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
    Drawing const* const d = currentDrawing();
    m_cePoint = -1;
    m_ceExtra.clear();
    if (rect.width() * m_zoom <= 2 && rect.height() * m_zoom <= 2) {
        m_ceStroke = m_ceFill = -1;     // a click on nothing: no curve is edited any more
    } else if (d && contourLine(*d)) {
        QVector<StrokePoint> const& points = contourLine(*d)->points;
        for (int i = 0; i < points.size(); ++i) {
            if (!rect.contains(points[i].pos))
                continue;
            if (m_cePoint < 0)
                m_cePoint = i;
            else
                m_ceExtra.push_back(i);
        }
    }
    emit pointPicked(pointThickness());
    return false;
}

bool Canvas::penUpMarquee(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
    // A click without movement on empty space just clears the selection.
    if (m_tool == Cutter || (m_tool == Select && bitmapNow())) {
        // (The Select tool on a bitmap layer picks pixels: those in the shape are cut loose, to be moved.)
        QPolygonF const shape = m_dragLasso ? m_lassoPath : QPolygonF(rect);
        m_lassoPath.clear();
        if (m_tool == Cutter && m_cutterGesture != GestureOff && m_dragLasso && currentDrawing() && isGesture(shape, m_zoom)) {
            // A gesture: a quick straight stroke across lines.
            Drawing edited = *currentDrawing();
            clearSelection();
            if (gestureCut(edited, shape.first(), shape.last(), m_cutterGesture, m_cutterTip))
                commitDrawing(edited, m_cutterGesture == GestureBreak ? tr("Break Lines") : tr("Cut Lines"));
        } else if (shape.size() >= 3 && (shape.boundingRect().width() * m_zoom > 2 || shape.boundingRect().height() * m_zoom > 2))
            cutInside(shape);
        else
            clearSelection();
    } else if (m_dragLasso) {
        QPolygonF const shape = m_lassoPath;
        m_lassoPath.clear();
        if (shape.size() >= 3 && (shape.boundingRect().width() * m_zoom > 2 || shape.boundingRect().height() * m_zoom > 2))
            selectInside([&shape](QPointF q) { return shape.containsPoint(q, Qt::OddEvenFill); }, m_marqueeAdds);
        else if (!m_marqueeAdds)
            clearSelection();
    } else if (rect.width() * m_zoom > 2 || rect.height() * m_zoom > 2)
        selectInRect(rect, m_marqueeAdds);
    return false;
}

bool Canvas::penUpWeights(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    QVector<SkinDab> const dabs = m_weightDabs;
    m_weightDabs.clear();
    if (!dabs.isEmpty())
        m_document->editLayers({m_layer}, dabs.first().strength < 0 ? tr("Erase Weights") : tr("Paint Weights"),
                               [dabs](Layer& layer) { layer.skin += dabs; });
    return false;
}

bool Canvas::penUpJoint(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    QVector<QPointF> joints = m_document->scene().layers[m_layer].bones;
    if (m_bone >= 0 && m_bone < joints.size()) {
        joints[m_bone] = m_dragNow;
        m_document->setBones(m_layer, joints, tr("Move Joint"));
    }
    return false;
}

bool Canvas::penUpWarpRest(Drag drag)
{
    Q_UNUSED(drag);
    QVector<QPointF> const points = warpRestDragged();
    m_drag = DragNone;
    m_document->setWarp(m_layer, points, m_document->scene().layers[m_layer].warpClosed,
                        m_warpIndex % 3 == 0 ? tr("Move Deformer Point") : tr("Move Deformer Handle"));
    return false;
}

bool Canvas::penUpPose(Drag drag)
{
    QVector<QPair<int, Pose>> const others = posedOthers();
    m_drag = DragNone;
    {
        // What was changed of a value that follows another layer does not show: say why.
        Scene const& scene = m_document->scene();
        for (Driver const& d : scene.layers[m_layer].drivers) {
            int const by = scene.layerIndex(d.source);
            if (by >= 0 && std::abs(Driver::read(m_dragPose, d.value) - Driver::read(m_poseStart, d.value)) > 1e-9) {
                emit message(tr("This layer follows %1 there (Layer Properties > Attribute Controls): the change is kept in its keyframes, "
                                "but shows only once it no longer follows it.").arg(scene.layers[by].name));
                break;
            }
        }
    }
    if (m_dragPose == m_poseStart)
        return false;
    auto editable = [this](int layer, Pose const& was, Pose const& now) {
        Scene const& scene = m_document->scene();
        for (ValueLink const& link : scene.layers[layer].links) {
            FunctionColumn const* column = scene.function(link.function);
            if (column && column->kind != FunctionColumn::Bezier && column->kind != FunctionColumn::Ease
                && std::abs(Driver::read(was, link.value) - Driver::read(now, link.value)) > 1e-9)
                return false;
        }
        return true;
    };
    bool canEdit = editable(m_layer, m_poseStart, m_dragPose);
    for (auto const& [other, pose] : others)
        canEdit = canEdit && editable(other, m_document->scene().poseOf(other, m_frame), pose);
    if (!canEdit) {
        emit message(tr("Edit the linked column's expression or path in the Function Editor to change this value."));
        return false;
    }
    QString const text = drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone")
                         : drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer")
                         : drag == DragPoseTurn ? tr("Turn Layer in Space") : tr("Rotate Layer");
    m_document->beginMacro(text);
    int const first = m_animateRange ? std::max(0, m_frame - m_onionPrev) : -1, last = m_animateRange ? m_frame + m_onionNext : -1;
    // A keyframe here of the layer as posed: of every value, or (as Preferences may say) only of those changed.
    auto keyHere = [this, &text](int layer, Pose const& was, Pose const& now) {
        int const parts = Canvas::changedParts(was, now);
        int const keyed = !m_tx.allKeys && parts != 0 && m_document->scene().layers[layer].keyIndex(m_frame) < 0 ? parts : Keyframe::AllParts;
        if (!m_document->animatePose(layer, m_frame, was, now, text, keyed))
            emit message(tr("Edit the linked column's expression or path in the Function Editor to change this value."));
    };
    Layer const before = m_document->scene().layers[m_layer];
    if (!m_animate) {
        if (!m_document->animatePose(m_layer, m_frame, m_poseStart, m_dragPose, text, Keyframe::AllParts, true, first, last))
            emit message(tr("Edit the linked column's expression or path in the Function Editor to change this value."));
    } else {
        if (m_keyFrameOne && m_frame > 0 && before.keys.isEmpty())
            m_document->animatePose(m_layer, 0, m_poseStart, m_poseStart, text);
        keyHere(m_layer, m_poseStart, m_dragPose);
        // Snap Keyframe: a stop-motion keyframe changed, the next one that held the same values follows it.
        int const at = before.keyIndex(m_frame);
        if (m_tx.snapKeyframe && at >= 0 && !before.keys[at].tween && at + 1 < before.keys.size() && before.keys[at + 1].pose == before.keys[at].pose)
            m_document->animatePose(m_layer, before.keys[at + 1].frame, m_poseStart, m_dragPose, text);
    }
    for (auto const& [other, pose] : others) {
        Pose const was = m_document->scene().poseOf(other, m_frame);
        if (!m_animate)
            m_document->animatePose(other, m_frame, was, pose, text, Keyframe::AllParts, true, first, last);
        else
            keyHere(other, was, pose);
    }
    m_document->endMacro();
    return false;
}

bool Canvas::penUpPivot(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    m_document->setPivot(m_layer, m_dragPivot);
    return false;
}

bool Canvas::penUpQuad(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    m_document->setQuad(m_layer, m_dragQuad, m_frame);
    return false;
}

bool Canvas::penUpSelection(Drag drag)
{
    Q_UNUSED(drag);
    if (!m_dragTransform.isIdentity()) {
        Drawing const edited = previewDrawing();
        m_drag = DragNone;
        QString const text = drag == DragMove ? tr("Move") : drag == DragScale ? (m_tool == Perspective ? tr("Perspective") : tr("Scale")) : tr("Rotate");
        bool const everywhere = (m_applyAll || m_applyOnion) && m_tool == Select;
        QSet<int> const onion = m_applyAll ? QSet<int>() : onionDrawings();
        QRectF const region = everywhere ? selectionBounds() : QRectF();
        QTransform const done = m_dragTransform;
        bool const all = m_tool == Reposition && !m_repositionCurrent;
        if (everywhere || all)
            m_document->beginMacro(text);
        commitDrawing(edited, text);
        if (all) {
            repositionOthers(done, text);
            m_document->endMacro();
        }
        if (everywhere) {
            alsoInOtherDrawings(region, &done, text, m_applyAll ? nullptr : &onion);
            m_document->endMacro();
        }
    }
    return false;
}
