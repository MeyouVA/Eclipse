// Eclipse - the Camera view: the pen lifted - what each drag leaves in the scene.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"

void Canvas::penUp()
{
    Drag const drag = m_drag;
    if (drag == DragGuide) {
        m_drag = DragNone;
        QVector<Scene::Guide> const moved = m_guideEdit;
        m_guideEdit.clear();
        m_guidePoint = -1;
        m_guideLine = -1;
        if (moved != m_document->scene().guides)
            m_document->setGuides(moved, tr("Move Guide"));
        update();
        return;
    }
    if (drag == DragGradient) {
        m_drag = DragNone;
        Drawing const* const d = currentDrawing();
        if (m_gradMoved && d && m_gradFill >= 0 && m_gradFill < d->fills.size() && m_gradFill < m_eraseArt.fills.size()) {
            Fill const& was = d->fills[m_gradFill];
            Fill const& now = m_eraseArt.fills[m_gradFill];
            if (!was.gradPlaced || was.gradFrom != now.gradFrom || was.gradTo != now.gradTo) {
                Drawing const art = m_eraseArt;
                commitDrawing(art, tr("Edit Gradient"));
            }
        }
        m_eraseArt = Drawing();
        update();
        return;
    }
    if (drag == DragErase && m_eraserCuts) {
        m_drag = DragNone;
        cutWithEraser();
        update();
        return;
    }
    if (drag == DragDraw) {
        m_drag = DragNone;
        // The pen samples become a curve through a few of them: about a pixel of the
        // screen is given up for it.
        // The smoothing setting is how far, in pixels of the screen, the line may stray
        // from the samples: from a fifth of a pixel to four.
        double const tolerance = (0.2 + m_smoothing * 0.038) / m_zoom;
        m_stroke.fit(tolerance, m_cornerAngle);
        if (m_tool == Brush && m_brushShapes) {
            // A brush stroke is a shape, not a line: the area the stroke covers, with a
            // contour of its own that the Contour Editor can pull about.
            double const fine = std::max(tolerance * 0.5, 0.05);
            Fill shape = Fill::fromStroke(m_stroke, fine);
            if (!shape.outlines.isEmpty()) {
                Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
                // Drawn behind, the stroke is the one that gives way: what lies over it is cut out of it.
                QVector<Fill> mine{shape};
                if (m_autoFlatten) {
                    // Flattened as it is drawn: the shapes of its colour that it overlaps
                    // become part of it, and it is cut out of the others it lies over.
                    QVector<Fill> kept;
                    for (Fill const& under : std::as_const(art.fills)) {
                        if (!under.overlaps(shape)) {
                            kept.push_back(under);
                        } else if (under.swatch == shape.swatch && under.color == shape.color) {
                            for (Fill& piece : mine)
                                if (piece.mergeWith(under, fine))
                                    break;
                        } else if (m_drawBehind) {
                            kept.push_back(under);
                            QVector<Fill> left;
                            for (Fill const& piece : std::as_const(mine))
                                left += piece.minus(under.outlines, fine);
                            mine = left;
                        } else {
                            kept += under.minus(shape.outlines, fine);
                        }
                    }
                    art.fills = kept;
                }
                if (m_drawBehind)
                    art.fills = mine + art.fills;
                else
                    art.fills += mine;
                if (!mine.isEmpty())
                    commitDrawing(art, tr("Draw Brush Stroke"));
            }
        } else if (m_tool == Brush && m_drawBehind) {
            Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
            art.strokes.push_front(m_stroke);
            commitDrawing(art, tr("Draw Stroke"));
        } else {
            addStrokeToArt(m_stroke);
        }
    } else if (drag == DragShape) {
        m_drag = DragNone;
        // A click without a drag draws nothing.
        if (m_stroke.bounds().width() * m_zoom > m_stroke.width * m_zoom + 3 || m_stroke.bounds().height() * m_zoom > m_stroke.width * m_zoom + 3)
            addStrokeToArt(m_stroke);
    } else if (drag == DragPoint) {
        if (contourValid() && contourLine(*currentDrawing())->points[m_cePoint].pos != m_dragNow) {
            Drawing const edited = previewDrawing();
            m_drag = DragNone;
            commitDrawing(edited, tr("Move Point"));
        }
    } else if (drag == DragHandle) {
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
    } else if (drag == DragPaint) {
        m_drag = DragNone;
        QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
        QPolygonF const shape = m_lassoPath;
        m_lassoPath.clear();
        QRectF const reach = m_dragLasso ? shape.boundingRect() : rect;
        if (reach.width() * m_zoom <= 3 && reach.height() * m_zoom <= 3)
            paintAt(m_dragStart);
        else if (m_dragLasso)
            paintInside([&shape](QPointF q) { return shape.containsPoint(q, Qt::OddEvenFill); }, shape.boundingRect());
        else
            paintInside([&rect](QPointF q) { return rect.contains(q); }, rect);
    } else if (drag == DragPointMarquee) {
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
    } else if (drag == DragMarquee) {
        m_drag = DragNone;
        QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
        // A click without movement on empty space just clears the selection.
        if (m_tool == Cutter) {
            QPolygonF const shape = m_dragLasso ? m_lassoPath : QPolygonF(rect);
            m_lassoPath.clear();
            if (shape.size() >= 3 && (shape.boundingRect().width() * m_zoom > 2 || shape.boundingRect().height() * m_zoom > 2))
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
    } else if (drag == DragJoint) {
        m_drag = DragNone;
        QVector<QPointF> joints = m_document->scene().layers[m_layer].bones;
        if (m_bone >= 0 && m_bone < joints.size()) {
            joints[m_bone] = m_dragNow;
            m_document->setBones(m_layer, joints, tr("Move Joint"));
        }
    } else if (drag == DragWarpRest) {
        QVector<QPointF> const points = warpRestDragged();
        m_drag = DragNone;
        m_document->setWarp(m_layer, points, m_document->scene().layers[m_layer].warpClosed,
                            m_warpIndex % 3 == 0 ? tr("Move Deformer Point") : tr("Move Deformer Handle"));
    } else if (drag == DragPoseMove || drag == DragPoseScale || drag == DragPoseRotate || drag == DragBone || drag == DragIK || drag == DragWarp) {
        m_drag = DragNone;
        if (!(m_dragPose == m_poseStart) && !m_animate)
            m_document->shiftPoses(m_layer, m_frame, m_poseStart, m_dragPose,
                                   drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone")
                                   : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer") : tr("Rotate Layer"));
        else if (!(m_dragPose == m_poseStart))
            m_document->setKeyframe(m_layer, m_frame, m_dragPose,
                                    drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone") : drag == DragPoseMove ? tr("Move Layer")
                                    : drag == DragPoseScale ? tr("Scale Layer") : tr("Rotate Layer"));
    } else if (drag == DragPivot) {
        m_drag = DragNone;
        m_document->setPivot(m_layer, m_dragPivot);
    } else if (drag == DragQuad) {
        m_drag = DragNone;
        m_document->setQuad(m_layer, m_dragQuad, m_frame);
    } else if (drag == DragMove || drag == DragScale || drag == DragRotate) {
        if (!m_dragTransform.isIdentity()) {
            Drawing const edited = previewDrawing();
            m_drag = DragNone;
            QString const text = drag == DragMove ? tr("Move") : drag == DragScale ? tr("Scale") : tr("Rotate");
            bool const everywhere = m_applyAll && m_tool == Select;
            QRectF const region = everywhere ? selectionBounds() : QRectF();
            QTransform const done = m_dragTransform;
            bool const all = m_tool == Reposition;
            if (everywhere || all)
                m_document->beginMacro(text);
            commitDrawing(edited, text);
            if (all) {
                repositionOthers(done, text);
                m_document->endMacro();
            }
            if (everywhere) {
                alsoInOtherDrawings(region, &done, text);
                m_document->endMacro();
            }
        }
    }
    m_drag = DragNone;
    m_dragTransform = QTransform();
    if (!m_polyActive)
        m_stroke = Stroke();
    updateCursor();
    update();
}

void Canvas::finishPolyline(bool keep)
{
    if (!m_polyActive)
        return;
    m_polyActive = false;
    Stroke const line = m_stroke;
    m_stroke = Stroke();
    update();
    if (keep && line.points.size() >= 2 && m_layer >= 0 && m_layer < m_document->scene().layers.size())
        addStrokeToArt(line);
}
