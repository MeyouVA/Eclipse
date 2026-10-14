// Eclipse - the Camera view: the pen lifted - what each drag leaves in the scene.
#include "ui/canvas/Canvas.h"

#include <QImage>
#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"

void Canvas::commitPenStroke(Stroke const& stroke, double tolerance)
{
    if (bitmapNow()) {
        // A bitmap layer: the stroke is painted into the pixels of the drawing, looking as it would as a line or a shape.
        Scene const& scene = m_document->scene();
        bool const asShape = m_tool == Brush && m_brushShapes;
        Fill shape;
        if (asShape) {
            shape = Fill::fromStroke(stroke, std::max(tolerance * 0.5, 0.05));
            shape.texture = m_brushTexture;
            shape.textureAmount = float(m_brushTextureAmount) / 100.0f;
            shape.textureCrisp = m_brushTextureCrisp;
            shape.textureLow = float(m_brushRampLow) / 100.0f;
            shape.textureHigh = float(m_brushRampHigh) / 100.0f;
            if (shape.outlines.isEmpty())
                return;
        }
        // The stroke at a share of its thickness, painted: as a shape (the Brush) or as a line.
        auto paintAt = [&](QPainter& onto, double share) {
            if (!asShape) {
                Stroke thinner = stroke;
                thinner.width = float(stroke.width * share);
                paintStroke(onto, thinner, scene.brushFor(stroke));
                return;
            }
            Fill piece = shape;
            if (share < 1.0) {
                Stroke thinner = stroke;
                thinner.width = float(stroke.width * share);
                piece = Fill::fromStroke(thinner, std::max(tolerance * 0.5, 0.05));
                piece.texture = shape.texture;
                piece.textureAmount = shape.textureAmount;
                piece.textureCrisp = shape.textureCrisp;
                piece.textureLow = shape.textureLow;
                piece.textureHigh = shape.textureHigh;
            }
            if (!piece.outlines.isEmpty())
                paintFill(onto, piece, scene.brushFor(shape));
        };
        bool const soft = m_tool == Brush && (m_bitmapHardness < 100 || m_bitmapFlow < 100);
        paintIntoBitmap([&](QPainter& painter) {
            QImage* const target = dynamic_cast<QImage*>(painter.device());
            if (!soft || !target) {
                paintAt(painter, 1.0);
                return;
            }
            // A soft edge: the stroke laid down again and again, each time thinner and more opaque, so the paint
            // fades out towards the edge; and all of it at the flow asked for.
            QImage dab(target->size(), QImage::Format_ARGB32_Premultiplied);
            dab.fill(Qt::transparent);
            {
                QPainter onto(&dab);
                onto.setRenderHints(painter.renderHints());
                onto.setTransform(painter.transform());
                double const hard = m_bitmapHardness / 100.0;
                int const passes = hard >= 1.0 ? 1 : 8;
                for (int i = 0; i < passes; ++i) {
                    onto.setOpacity(1.0 / double(passes - i));
                    paintAt(onto, 1.0 - (1.0 - hard) * double(i) / passes);
                }
            }
            painter.save();
            painter.resetTransform();
            painter.setOpacity(m_bitmapFlow / 100.0);
            painter.drawImage(QPointF(0, 0), dab);
            painter.restore();
        }, asShape ? tr("Paint Brush Stroke") : tr("Paint Stroke"), stroke.bounds());
        return;
    }
    if (m_tool == Brush && m_brushShapes) {
        // A brush stroke is a shape, not a line: the area the stroke covers, with a
        // contour of its own that the Contour Editor can pull about.
        double const fine = std::max(tolerance * 0.5, 0.05);
        Fill shape = Fill::fromStroke(stroke, fine);
        shape.texture = m_brushTexture;
        shape.textureAmount = float(m_brushTextureAmount) / 100.0f;
        shape.textureCrisp = m_brushTextureCrisp;
        shape.textureLow = float(m_brushRampLow) / 100.0f;
        shape.textureHigh = float(m_brushRampHigh) / 100.0f;
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
                    } else if (under.swatch == shape.swatch && under.color == shape.color && under.texture == shape.texture
                               && (shape.texture == 0 || (under.textureAmount == shape.textureAmount && under.textureCrisp == shape.textureCrisp
                                                           && under.textureLow == shape.textureLow && under.textureHigh == shape.textureHigh))) {
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
        art.strokes.push_front(stroke);
        commitDrawing(art, tr("Draw Stroke"));
    } else {
        addStrokeToArt(stroke);
    }
}

void Canvas::keepStroke(Stroke const& stroke, bool fromPen, double tolerance)
{
    // With a Symmetry guide: once more over its axis - and over every other axis of symmetry the scene has.
    QVector<QTransform> const mirrors = symmetries();
    if (!mirrors.isEmpty())
        m_document->beginMacro(tr("Draw Stroke"));
    if (fromPen)
        commitPenStroke(stroke, tolerance);
    else
        addStrokeToArt(stroke);
    for (QTransform const& mirror : mirrors) {
        Stroke other = stroke;
        other.transform(mirror);
        if (fromPen)
            commitPenStroke(other, tolerance);
        else
            addStrokeToArt(other);
    }
    if (!mirrors.isEmpty())
        m_document->endMacro();
}

void Canvas::penUp()
{
    Drag const drag = m_drag;
    if (drag == DragSplineOffset) {
        QPointF const by = splineOffset();
        m_drag = DragNone;
        if (QLineF(m_splineFrom, m_splineNow).length() >= 1.0)
            m_document->offsetPath(m_layer, by);
        update();
        return;
    }
    if (drag == DragControlPoint) {
        m_drag = DragNone;
        if (m_cpMoved && m_cpDepth) {
            // Up the view is away from the camera (as Alt with the Transform tool moves a layer), two units a pixel.
            Scene const& scene = m_document->scene();
            Layer const& l = scene.layers[m_layer];
            if (m_cpKey >= 0 && m_cpKey + 1 < l.keys.size()) {
                Keyframe const& a = l.keys[m_cpKey];
                Keyframe const& b = l.keys[m_cpKey + 1];
                QVector<double> placed;
                a.pathLine(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), &placed);
                double const was = m_cpIndex < a.controlsZ.size() ? a.controlsZ[m_cpIndex] : a.pose.z + (b.pose.z - a.pose.z) * placed.value(m_cpIndex, 0.0);
                double const by = (m_cpDragPos.y() - m_cpPressPos.y()) / std::max(m_zoom, 1e-6) * 2.0;
                m_document->setControlPointDepth(m_layer, m_cpKey, m_cpIndex, was - by);
            }
        } else if (m_cpMoved) {
            m_document->moveControlPoint(m_layer, m_cpKey, m_cpIndex, controlPointFromView(m_cpKey, m_cpIndex, m_cpDragPos));
        }
        m_cpMoved = false;
        m_cpDepth = false;
        update();
        return;
    }
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
    if (drag == DragEnvelope) {
        m_drag = DragNone;
        bool const pulled = m_envNow != m_envRest;
        Drawing const art = m_eraseArt;
        m_eraseArt = Drawing();
        m_envRest.clear();
        m_envNow.clear();
        m_envPoint = -1;
        if (pulled)
            commitDrawing(art, tr("Envelope"));
        update();
        return;
    }
    if (drag == DragRepaint) {
        m_drag = DragNone;
        // Once more, the edges fitted with points and handles as the brush makes them.
        repaintTo(m_dragNow, std::max(0.25 / m_zoom, 0.05));
        if (m_inked) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, tr("Repaint Brush"));
        }
        m_eraseArt = Drawing();
        m_repaintFrom = Drawing();
        m_repaintPath = Stroke();
        m_inked = false;
        update();
        return;
    }
    if (drag == DragInk || drag == DragSmooth) {
        m_drag = DragNone;
        if (m_inked) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, drag == DragInk ? tr("Ink") : tr("Smooth"));
        }
        m_eraseArt = Drawing();
        m_inked = false;
        update();
        return;
    }
    if (drag == DragThickness) {
        m_drag = DragNone;
        Drawing const* const d = currentDrawing();
        if (d && m_thickStroke >= 0 && m_thickStroke < d->strokes.size() && m_thickStroke < m_eraseArt.strokes.size()
            && m_thickPoint < d->strokes[m_thickStroke].points.size() && m_thickPoint < m_eraseArt.strokes[m_thickStroke].points.size()
            && d->strokes[m_thickStroke].points[m_thickPoint].pressure != m_eraseArt.strokes[m_thickStroke].points[m_thickPoint].pressure) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, tr("Pencil Line Thickness"));
        }
        m_eraseArt = Drawing();
        m_thickStroke = m_thickPoint = -1;
        update();
        return;
    }
    if (drag == DragGradient) {
        m_drag = DragNone;
        Drawing const* const d = currentDrawing();
        if (m_gradMoved && d && m_gradStroke >= 0 && m_gradStroke < d->strokes.size() && m_gradStroke < m_eraseArt.strokes.size()) {
            Stroke const& was = d->strokes[m_gradStroke];
            Stroke const& now = m_eraseArt.strokes[m_gradStroke];
            if (!was.gradPlaced || was.gradFrom != now.gradFrom || was.gradTo != now.gradTo) {
                Drawing const art = m_eraseArt;
                int const line = m_gradStroke;
                commitDrawing(art, tr("Edit Gradient"));
                m_gradStroke = line;        // it stays in hand
            }
        } else if (m_gradMoved && d && m_gradFill >= 0 && m_gradFill < d->fills.size() && m_gradFill < m_eraseArt.fills.size()) {
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
    if (drag == DragHint) {
        m_drag = DragNone;
        hintUp();
        return;
    }
    if (drag == DragBoneIK) {
        m_drag = DragNone;
        if (m_ikBegun)
            m_document->endMacro();
        m_ikBegun = false;
        m_ikChain.clear();
        update();
        return;
    }
    if (drag == DragErase && (m_eraserCuts || bitmapNow())) {
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
        if (m_snapGrid && (m_tool == Brush || m_tool == Pencil) && m_stroke.points.size() >= 2) {
            // Snap to Grid: the line begins and ends on a crossing of the grid when it is near one.
            m_stroke.points.first().pos = snapped(m_stroke.points.first().pos);
            m_stroke.points.last().pos = snapped(m_stroke.points.last().pos);
        }
        m_stroke.fit(tolerance, m_cornerAngle);
        keepStroke(m_stroke, true, tolerance);
    } else if (drag == DragShape) {
        m_drag = DragNone;
        // A click without a drag draws nothing.
        if (m_stroke.bounds().width() * m_zoom > m_stroke.width * m_zoom + 3 || m_stroke.bounds().height() * m_zoom > m_stroke.width * m_zoom + 3)
            keepStroke(m_stroke, false);
    } else if (drag == DragPoint) {
        if (contourValid() && contourLine(*currentDrawing())->points[m_cePoint].pos != m_dragNow) {
            Drawing const edited = previewDrawing();
            m_drag = DragNone;
            commitDrawing(edited, tr("Move Point"));
        }
    } else if (drag == DragHandle && m_ceHandle == 2) {
        if (contourValid()) {
            bool const pulled = pulledHandles();
            Drawing edited = pulled ? previewDrawing() : *currentDrawing();
            m_drag = DragNone;
            int const stroke = m_ceStroke, point = m_cePoint, fill = m_ceFill, contour = m_ceContour;
            QString name = tr("Pull Handles");
            if (!pulled) {
                // Only clicked: a corner becomes smooth, a smooth point a corner.
                Stroke& target = *contourLine(edited);
                bool const corner = target.isCorner(point);
                target.setCorner(point, !corner);
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
        // Apply to All Layers: the other drawing layers that are seen and may be drawn on, each with the way from
        // this layer's sheet to its own.
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
        if (m_tool == Cutter || (m_tool == Select && bitmapNow())) {
            // (The Select tool on a bitmap layer picks pixels: those in the shape are cut loose, to be moved.)
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
    } else if (drag == DragPoseSkew || drag == DragPoseMove || drag == DragPoseScale || drag == DragPoseRotate || drag == DragBone || drag == DragIK || drag == DragWarp || drag == DragPoseTurn) {
        m_drag = DragNone;
        if (!(m_dragPose == m_poseStart) && !m_animate)
            m_document->shiftPoses(m_layer, m_frame, m_poseStart, m_dragPose,
                                   drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone")
                                   : drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer")
                                   : drag == DragPoseTurn ? tr("Turn Layer in Space") : tr("Rotate Layer"),
                                   m_animateRange ? std::max(0, m_frame - m_onionPrev) : -1, m_animateRange ? m_frame + m_onionNext : -1);
        else if (!(m_dragPose == m_poseStart) && m_keyFrameOne && m_frame > 0 && m_document->scene().layers[m_layer].keys.isEmpty()) {
            // Its first keyframe, and not on the first frame: the first frame gets one too, of the layer as it was.
            QString const text = drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer") : drag == DragPoseRotate ? tr("Rotate Layer")
                                 : drag == DragPoseTurn ? tr("Turn Layer in Space")
                                 : drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : tr("Turn Bone");
            m_document->beginMacro(text);
            m_document->setKeyframe(m_layer, 0, m_poseStart, text);
            m_document->setKeyframe(m_layer, m_frame, m_dragPose, text);
            m_document->endMacro();
        } else if (!(m_dragPose == m_poseStart))
            m_document->setKeyframe(m_layer, m_frame, m_dragPose,
                                    drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone") : drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer")
                                    : drag == DragPoseScale ? tr("Scale Layer") : drag == DragPoseTurn ? tr("Turn Layer in Space") : tr("Rotate Layer"));
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
            QString const text = drag == DragMove ? tr("Move") : drag == DragScale ? (m_tool == Perspective ? tr("Perspective") : tr("Scale")) : tr("Rotate");
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
    m_alignLines.clear();
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
        keepStroke(line, false);
}
