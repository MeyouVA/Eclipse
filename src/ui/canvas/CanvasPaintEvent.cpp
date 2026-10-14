// Eclipse - the Camera view: what is painted in the view - the layers, the onion skin, and the aids over them.
#include "ui/canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

// A drawing of the onion skin: whole, in its colour - or only its outlines.
void Canvas::paintOnion(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const& tint) const
{
    if (!m_onionOutlines) {
        paintDrawing(painter, scene, drawing, opacity, &tint);
        return;
    }
    painter.save();
    painter.setOpacity(painter.opacity() * opacity);
    QPen edge(tint, 1.4);
    edge.setCosmetic(true);
    painter.setPen(edge);
    painter.setBrush(Qt::NoBrush);
    for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::LineSheet), int(Drawing::OverlaySheet)}) {
        for (Fill const& fill : drawing.fillsOf(sheet))
            for (QPolygonF const& outline : fill.outlines)
                painter.drawPolygon(outline);
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            if (stroke.width <= 0.0f)
                continue;       // a line that is not seen has no outline to show
            QPolygonF centre;
            for (StrokePoint const& p : stroke.path())
                centre << p.pos;
            if (stroke.closed)
                painter.drawPolygon(centre);
            else
                painter.drawPolyline(centre);
        }
    }
    painter.restore();
}

void Canvas::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter painter(this);
    // The view is one even grey; the camera frame is only an outline on it, and what is
    // drawn outside the frame shows too.  (The paper colour is for what is exported.)
    painter.fillRect(rect(), m_backdrop);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(cameraViewTransform());

    QRectF const frameRect(0, 0, scene.width, scene.height);

    // Playing from the preview: the frame as it was rendered, and nothing else to work out.
    if (m_playing && m_usePreview && !m_drawingView && m_frame >= 0 && m_frame < m_preview.size() && !m_preview[m_frame].isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(frameRect, m_preview[m_frame]);
        QPen border(QColor(70, 70, 70));
        border.setCosmetic(true);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frameRect);
        return;
    }

    bool const transforming = m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate || m_drag == DragPoint || m_drag == DragHandle;
    // The eraser cuts as it goes: the drawing is shown as it is so far, before it is in the scene.
    // (So is the gradient being placed: the area is shown with it where the pointer has it.)
    bool const erasing = (m_drag == DragErase && (m_eraserCuts || bitmapNow()) && m_eraseAny) || m_drag == DragGradient || m_drag == DragThickness || ((m_drag == DragInk || m_drag == DragSmooth || m_drag == DragRepaint) && m_inked) || m_drag == DragEnvelope;
    QTransform const view = viewTransform();
    // The grid, when it is to lie behind the drawings.
    if (m_grid && m_gridUnder && !m_playing)
        paintGrid(painter, frameRect);
    // Back to front - with the layer being drawn on last of all when that is asked for.
    QVector<int> order;
    // (In the Drawing view there is no camera and no depth: the layers as they are stacked.)
    QVector<int> stacked(scene.layers.size());
    std::iota(stacked.begin(), stacked.end(), 0);
    bool heldBack = false;
    for (int l : m_drawingView ? stacked : scene.drawOrder(m_frame)) {
        if (!scene.inDisplay(l))
            continue;      // (not in what the display shows)
        if (m_currentOnTop && !m_playing && l == m_layer)
            heldBack = true;
        else
            order.push_back(l);
    }
    if (heldBack && scene.inDisplay(m_layer))
        order.push_back(m_layer);
    if (m_drawingView) {
        // The sheet of paper the drawing is on.
        painter.setTransform(cameraViewTransform());
        painter.fillRect(frameRect, Qt::white);
    }
    for (int l : std::as_const(order)) {
        Layer const& layer = scene.layers[l];
        // In the Drawing view: the current layer; and the others only on the light table.
        if (m_drawingView && l != m_layer && (!m_lightTable || !layer.holdsDrawings()))
            continue;
        if (layer.kind == Layer::ColourCard && scene.shows(l)) {
            QColor colour = layer.cardColor;
            colour.setAlphaF(colour.alphaF() * float(layer.opacity));
            painter.setTransform(cameraViewTransform());
            painter.fillRect(frameRect, colour);
            continue;
        }
        if (!scene.shows(l) || !layer.holdsDrawings())
            continue;
        painter.setTransform(worldOf(l, m_frame) * view);
        // Onion skin: the neighbouring drawings of the layer being edited, tinted, underneath it.
        if (m_onionSkin && !m_playing && (l == m_layer || layer.onion)) {
            // Each is shown where the layer is on its own frame; the further off, the fainter.
            double const strength = m_onionOpacity / 100.0;
            for (int i = 0, prev = m_frame; i < m_onionPrev; ++i) {
                prev = m_onionByFrame ? prev - 1 : layer.previousDrawingFrame(prev);
                if (prev < 0)
                    break;
                if (layer.drawingAt(prev) == Layer::kNoDrawing)
                    continue;       // an empty frame: nothing of it to show
                if (m_onionMark != Drawing::NoMark && layer.drawings[layer.cells[prev]].mark != m_onionMark) {
                    --i;            // not one of the drawings asked for: the next one back is looked at instead
                    continue;
                }
                painter.setTransform(worldOf(l, prev) * view);
                if (m_onionStrength[0][i] > 0)
                    paintOnion(painter, scene, shown(l, prev, layer.drawings[layer.cells[prev]]),
                               strength * (m_onionPrev - i) / m_onionPrev * m_onionStrength[0][i] / 100.0, onionStepColour(false, i));
            }
            for (int i = 0, next = m_frame; i < m_onionNext; ++i) {
                next = m_onionByFrame ? next + 1 : layer.nextDrawingFrame(next);
                if (next < 0 || next >= scene.frameCount())
                    break;
                if (layer.drawingAt(next) == Layer::kNoDrawing)
                    continue;
                if (m_onionMark != Drawing::NoMark && layer.drawings[layer.cells[next]].mark != m_onionMark) {
                    --i;
                    continue;
                }
                painter.setTransform(worldOf(l, next) * view);
                if (m_onionStrength[1][i] > 0)
                    paintOnion(painter, scene, shown(l, next, layer.drawings[layer.cells[next]]),
                               strength * 0.86 * (m_onionNext - i) / m_onionNext * m_onionStrength[1][i] / 100.0, onionStepColour(true, i));
            }
            painter.setTransform(worldOf(l, m_frame) * view);
        }
        int const index = layer.drawingAt(m_frame);
        // The light table: while drawing, the layers that are not being drawn on are faint.
        double const faint = m_lightTable && !m_playing && l != m_layer && m_tool != Transform && m_tool != IK && m_tool != Rigging ? m_lightWash / 100.0 : 1.0;
        if (index != Layer::kNoDrawing && layer.morphing(m_frame)) {
            // In between the two key drawings of a morphing: the drawing made for this frame.
            // (It is not one that can be drawn on, so none of the editing aids apply.)
            static QColor const dark(34, 34, 38);
            Drawing const made = shown(l, m_frame, layer.shownAt(m_frame));
            if (!m_playing && m_backlight)
                paintDrawing(painter, scene, made, 1.0, &dark);
            else
                paintLayerDrawing(painter, scene, l, m_frame, made, layer.opacity * poseOf(l, m_frame).opacity * faint, view);
            continue;
        }
        if (index != Layer::kNoDrawing && !m_playing && m_backlight) {
            // Backlight: everything drawn as one dark shape, as if lit from behind.
            static QColor const dark(34, 34, 38);
            paintDrawing(painter, scene, shown(l, m_frame, l == m_layer && erasing ? withArt(m_eraseArt) : l == m_layer && transforming ? withArt(previewDrawing()) : layer.drawings[index]), 1.0, &dark);
        } else if (index != Layer::kNoDrawing && !m_playing && m_outlineLocked && layer.locked) {
            // A locked layer as outlines: the edges of its paint and the paths of its lines.
            Drawing const lines = shown(l, m_frame, layer.drawings[index]);
            QPen edge(layer.trackColor.isValid() ? layer.trackColor : QColor(200, 70, 60));
            edge.setCosmetic(true);
            painter.setPen(edge);
            painter.setBrush(Qt::NoBrush);
            for (QVector<Fill> const* fills : {&lines.underlayFills, &lines.colourFills, &lines.fills, &lines.overlayFills})
                for (Fill const& fill : *fills)
                    for (QPolygonF const& outline : fill.outlines)
                        painter.drawPolygon(outline);
            for (QVector<Stroke> const* strokes : {&lines.underlayStrokes, &lines.colourStrokes, &lines.strokes, &lines.overlayStrokes})
                for (Stroke const& stroke : *strokes) {
                    QPolygonF centre;
                    for (StrokePoint const& p : stroke.path())
                        centre << p.pos;
                    if (stroke.closed)
                        painter.drawPolygon(centre);
                    else
                        painter.drawPolyline(centre);
                }
        } else if (l == m_layer && transforming)
            paintLayerDrawing(painter, scene, l, m_frame, withArt(previewDrawing()), layer.opacity * poseOf(l, m_frame).opacity, view, !m_previewAllArt);
        else if (index != Layer::kNoDrawing && l == m_layer && !m_previewAllArt && !m_playing) {
            // Only the sheet being worked on.
            Drawing one = erasing ? withArt(m_eraseArt) : layer.drawings[index];
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                if (sheet != sheetOf(m_art) && !(bothArtsNow() && (sheet == Drawing::LineSheet || sheet == Drawing::ColourSheet))) {
                    one.fillsOf(sheet).clear();
                    one.strokesOf(sheet).clear();
                }
            }
            paintLayerDrawing(painter, scene, l, m_frame, shown(l, m_frame, one), layer.opacity * poseOf(l, m_frame).opacity, view, true);
        } else if (index != Layer::kNoDrawing)
            paintLayerDrawing(painter, scene, l, m_frame, shown(l, m_frame, erasing && l == m_layer ? withArt(m_eraseArt) : layer.drawings[index]),
                              layer.opacity * poseOf(l, m_frame).opacity * faint
                                  * (m_showStrokes && m_strokesWash && l == m_layer && !m_playing ? 0.3 : 1.0), view);
        if ((m_showStrokes || m_tool == StrokeTool || m_tool == CloseGap) && !m_playing && l == m_layer && index != Layer::kNoDrawing) {
            // Where the pen went, and the edges of the painted areas: hair lines over the artwork.
            Drawing const lines = erasing ? withArt(m_eraseArt) : transforming ? previewDrawing() : layer.drawings[index];
            QPen hair(QColor(40, 200, 235));
            hair.setCosmetic(true);
            painter.setPen(hair);
            painter.setBrush(Qt::NoBrush);
            for (Fill const& fill : lines.fills)
                for (QPolygonF const& outline : fill.outlines)
                    painter.drawPolygon(outline);
            for (Stroke const& stroke : lines.strokes) {
                QPolygonF centre;
                for (StrokePoint const& p : stroke.path())
                    centre << p.pos;
                if (stroke.closed)
                    painter.drawPolygon(centre);
                else
                    painter.drawPolyline(centre);
            }
        }
        if (l == m_layer && (m_drag == DragDraw || m_drag == DragShape))
            paintStroke(painter, m_stroke, scene.brushFor(m_stroke));
        if (l == m_layer && (m_drag == DragDraw || m_drag == DragShape) && m_stroke.width <= 0.0f) {
            // A line with no thickness, as it is drawn: a hair line.
            QPen hair(QColor(40, 200, 235));
            hair.setCosmetic(true);
            painter.setPen(hair);
            painter.setBrush(Qt::NoBrush);
            QPolygonF centre;
            for (StrokePoint const& p : m_stroke.path())
                centre << p.pos;
            painter.drawPolyline(centre);
        }
        if (l == m_layer && m_tool == Stamp && m_hoverKnown && !m_playing && m_drag == DragNone && layer.holdsDrawings()) {
            // The stamp, faint, where a click would put it.
            double const k = m_stampScale / 100.0;
            QPointF const at = toLayer(m_hoverWidget);
            painter.save();
            painter.setTransform(QTransform::fromScale(k, k) * QTransform::fromTranslate(at.x(), at.y()) * layerViewTransform());
            paintDrawing(painter, scene, stampNow(), 0.45);
            painter.restore();
        }
        if (l == m_layer && m_tool == PencilEditor && !m_playing && layer.holdsDrawings()) {
            // The points of the pencil lines, to take hold of.
            Drawing const* const lines = m_drag == DragThickness ? &m_eraseArt : currentDrawing();
            if (lines) {
                QTransform const where = layerViewTransform();
                painter.save();
                painter.resetTransform();
                painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                for (int i = 0; i < lines->strokes.size(); ++i) {
                    if (lines->strokes[i].width <= 0.0f)
                        continue;
                    for (int k = 0; k < lines->strokes[i].points.size(); ++k) {
                        QPointF const at = where.map(lines->strokes[i].points[k].pos);
                        painter.setBrush(i == m_thickStroke && k == m_thickPoint ? QColor(255, 255, 255) : QColor(60, 200, 230));
                        painter.drawRect(QRectF(at.x() - 3, at.y() - 3, 6, 6));
                    }
                }
                painter.restore();
            }
        }
        if (l == m_layer && m_tool == Envelope && !m_playing && hasSelection()) {
            // The outline round what is selected, and its points to take hold of.
            QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
            if (points.size() >= 9) {
                QTransform const where = layerViewTransform();
                painter.save();
                painter.resetTransform();
                QPen outline(QColor(240, 160, 60), 1.2);
                painter.setPen(outline);
                painter.setBrush(Qt::NoBrush);
                painter.drawPolygon(where.map(Deform::warpLine(points, true, 12)));
                if (m_envPicked >= 0 && m_envPicked + 2 < points.size()) {
                    // The handles of the point pressed last.
                    QPointF const at = where.map(points[m_envPicked]);
                    for (int which : {1, 2}) {
                        QPointF const handle = where.map(points[m_envPicked] + points[m_envPicked + which]);
                        painter.setPen(QPen(QColor(240, 160, 60), 1.0, Qt::DotLine));
                        painter.drawLine(at, handle);
                        painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                        painter.setBrush(m_envPoint == m_envPicked + which ? QColor(255, 255, 255) : QColor(240, 160, 60));
                        painter.drawEllipse(handle, 3.5, 3.5);
                    }
                    painter.setBrush(Qt::NoBrush);
                }
                painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                for (int i = 0; i < points.size(); i += 3) {
                    QPointF const at = where.map(points[i]);
                    painter.setBrush(i == m_envPoint || (m_envPoint < 0 && i == m_envPicked) ? QColor(255, 255, 255) : QColor(240, 160, 60));
                    painter.drawRect(QRectF(at.x() - 3.5, at.y() - 3.5, 7, 7));
                }
                painter.restore();
            }
        }
        if (l == m_layer && m_polyActive) {
            // The polyline so far, and on to where the next point would go.
            Stroke preview = m_stroke;
            StrokePoint next{m_polyHover, 1.0f};
            next.corner = preview.curve;
            if (m_drag != DragPolyHandle)
                preview.points.push_back(next);
            paintStroke(painter, preview, scene.brushFor(preview));
        }
    }

    painter.setTransform(cameraViewTransform());
    painter.setClipping(false);
    QPen border(QColor(70, 70, 70));
    border.setCosmetic(true);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frameRect);
    if (m_cameraMask) {
        // Everything but the frame, darkened.
        QPainterPath outside;
        outside.addRect(frameRect.adjusted(-1e6, -1e6, 1e6, 1e6));
        outside.addRect(frameRect);
        painter.fillPath(outside, QColor(20, 20, 20, 215));
    }
    if (m_grid && !m_gridUnder && !m_playing)
        paintGrid(painter, frameRect);
    // Guides: right across the view, over the drawings.
    if (m_showGuides && !m_playing && !scene.guides.isEmpty()) {
        painter.save();
        painter.setTransform(cameraViewTransform());
        painter.setRenderHint(QPainter::Antialiasing, false);      // a crisp line a pixel wide
        QPen guidePen(QColor(60, 200, 230));
        guidePen.setCosmetic(true);
        painter.setPen(guidePen);
        double const reach = 100000.0;
        // (While one is dragged: as they are being put.)
        for (Scene::Guide const& guide : m_drag == DragGuide ? m_guideEdit : scene.guides) {
            if (guide.kind != Scene::Guide::Line)
                continue;
            if (guide.vertical)
                painter.drawLine(QPointF(guide.at, -reach), QPointF(guide.at, reach));
            else
                painter.drawLine(QPointF(-reach, guide.at), QPointF(reach, guide.at));
        }
        painter.restore();
    }
    paintGuide(painter);
    if (m_proportionGrid && !m_playing) {
        // The frame in thirds each way (or as many parts as Preferences says), to compose the picture by.
        QPen thirds(QColor(111, 168, 232, 200));
        thirds.setCosmetic(true);
        painter.setPen(thirds);
        for (int i = 1; i < m_propAcross; ++i) {
            double const x = frameRect.left() + frameRect.width() * i / m_propAcross;
            painter.drawLine(QPointF(x, frameRect.top()), QPointF(x, frameRect.bottom()));
        }
        for (int i = 1; i < m_propDown; ++i) {
            double const y = frameRect.top() + frameRect.height() * i / m_propDown;
            painter.drawLine(QPointF(frameRect.left(), y), QPointF(frameRect.right(), y));
        }
    }
    if (m_safeArea && !m_playing) {
        // Inside these the picture and the titles are safe from being cut off by a screen.
        QPen action(QColor(40, 40, 40, 200));
        action.setCosmetic(true);
        QPen title(QColor(111, 168, 232, 220));
        title.setCosmetic(true);
        title.setStyle(Qt::DashLine);
        QPointF const c = frameRect.center();
        auto inner = [&](double part) {
            return QRectF(c.x() - frameRect.width() * part / 2, c.y() - frameRect.height() * part / 2, frameRect.width() * part, frameRect.height() * part);
        };
        painter.setPen(action);
        painter.drawRect(inner(1.0 - m_safeMargin));
        painter.drawLine(c - QPointF(frameRect.width() * 0.02, 0), c + QPointF(frameRect.width() * 0.02, 0));
        painter.drawLine(c - QPointF(0, frameRect.width() * 0.02), c + QPointF(0, frameRect.width() * 0.02));
        painter.setPen(title);
        painter.drawRect(inner(1.0 - 2.0 * m_safeMargin));
    }

    if (!m_playing)
        paintBoneLayers(painter);
        if (m_symbolPivots && !m_drawingView && !m_playing) {
            // Show > Symbol Pivot: a mark on the pivot of each layer that shows a symbol.
            painter.save();
            painter.resetTransform();
            painter.setRenderHint(QPainter::Antialiasing, true);
            for (int l = 0; l < scene.layers.size(); ++l) {
                if (!scene.layers[l].showsSymbol() || !scene.shows(l))
                    continue;
                QPointF const at = pivotInView(l);
                // (A diamond, to tell it from the pivot of the layer in hand.)
                QPolygonF const diamond({at + QPointF(0, -11), at + QPointF(11, 0), at + QPointF(0, 11), at + QPointF(-11, 0)});
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(20, 20, 20), 3.4));
                painter.drawPolygon(diamond);
                painter.setPen(QPen(QColor(210, 110, 235), 2.0));
                painter.drawPolygon(diamond);
            }
            painter.restore();
        }
        paintMorphHints(painter);
    if (m_tool == Transform && !m_playing) {
        if (m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].kind == Layer::Quadmap)
            paintQuadAids(painter);
        else
            paintTransformAids(painter);
        return;
    }
    if (m_showControl && !m_playing && m_layer >= 0 && m_layer < scene.layers.size()) {
        // The pivot of the current layer, and its bones, whatever tool is in hand.
        Layer const& layer = scene.layers[m_layer];
        QTransform const toWidget = worldOf(m_layer, m_frame) * viewTransform();
        painter.resetTransform();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPointF const pivot = toWidget.map(layer.pivot);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(20, 20, 20), 3));
        painter.drawEllipse(pivot, 6, 6);
        painter.setPen(QPen(QColor(255, 190, 70), 1.4));
        painter.drawEllipse(pivot, 6, 6);
        painter.drawLine(pivot - QPointF(9, 0), pivot + QPointF(9, 0));
        painter.drawLine(pivot - QPointF(0, 9), pivot + QPointF(0, 9));
        paintMotionPath(painter);
        if (layer.bones.size() >= 2 && m_tool != Rigging && m_tool != IK)
            paintBones(painter, Deform::posedJoints(layer.bones, poseOf(m_layer, m_frame).bend), -1);
        if (layer.hasWarp() && m_tool != Rigging && m_tool != Transform)
            paintWarp(painter, showsRest(m_layer) ? layer.warp : Deform::posedWarp(layer.warp, poseOf(m_layer, m_frame).warp), layer.warpClosed, -1);
    }
    if (m_tool == EditGradient && !m_playing) {
        paintGradientAids(painter);
        return;
    }
    if (m_tool == ContourEditor && !m_playing) {
        paintContourAids(painter);
        return;
    }
    if (m_tool == IK && !m_playing && m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        if (layer.bones.size() >= 2)
            paintBones(painter, Deform::posedJoints(layer.bones, poseOf(m_layer, m_frame).bend), m_drag == DragIK ? m_bone : -1);
        return;
    }
    if (m_tool == Rigging && !m_playing && m_layer >= 0 && m_layer < scene.layers.size()) {
        if (m_rigMode != RigBones) {
            paintWarp(painter, warpRestDragged(), scene.layers[m_layer].warpClosed, m_drag == DragWarpRest ? m_warpIndex : -1);
            return;
        }
        // The chain at rest, with the joint being dragged where the pointer has it.
        QVector<QPointF> joints = scene.layers[m_layer].bones;
        if (m_drag == DragJoint && m_bone >= 0 && m_bone < joints.size())
            joints[m_bone] = m_dragNow;
        paintBones(painter, joints, m_drag == DragJoint ? m_bone : -1);
        return;
    }
    // ---- selection aids ----
    if (!selecting() || m_playing)
        return;
    // The selection lives in the coordinates of its layer.
    QTransform const layerView = layerViewTransform();
    painter.setTransform(layerView);
    QColor const accent(64, 156, 255);
    if (hasSelection()) {
        Drawing const shown = previewDrawing();
        // Highlight what is selected: a thin line along each stroke, the edge of each fill.
        QPen mark(accent);
        mark.setCosmetic(true);
        mark.setWidthF(1.5);
        painter.setPen(mark);
        painter.setBrush(Qt::NoBrush);
        for (int i : m_selStrokes) {
            if (m_bboxSelection)
                break;      // BBox Selection Style: the box says it all
            QPolygonF line;
            for (StrokePoint const& p : shown.strokes[i].path())
                line.push_back(p.pos);
            painter.drawPolyline(line);
        }
        for (int i : m_selFills) {
            if (m_bboxSelection)
                break;
            for (QPolygonF const& outline : shown.fills[i].outlines)
                painter.drawPolygon(outline);
        }

        // Bounding box and handles, drawn in widget space so they keep their size when zooming.
        QRectF const bounds = m_drag != DragNone ? m_dragTransform.mapRect(m_dragBounds) : selectionBounds();
        painter.resetTransform();
        if ((m_drag == DragMove || m_drag == DragScale) && !m_alignLines.isEmpty()) {
            // What it is in line with.
            painter.setPen(QPen(QColor(240, 90, 160), 1.0));
            for (QLineF const& line : std::as_const(m_alignLines))
                painter.drawLine(layerView.map(line.p1()), layerView.map(line.p2()));
        }
        QPen box(accent);
        box.setStyle(Qt::DashLine);
        painter.setPen(box);
        if (m_drag == DragRotate || (m_tool == Perspective && m_drag == DragScale)) {
            painter.drawPolygon(layerView.map(m_dragTransform.map(QPolygonF(m_dragBounds))));
        } else {
            painter.drawPolygon(layerView.map(QPolygonF(bounds)));
            painter.setPen(QPen(accent));
            painter.setBrush(Qt::white);
            for (Handle h : {TopLeft, TopRight, BottomRight, BottomLeft}) {
                QPointF const p = layerView.map(handlePos(h, bounds));
                painter.drawRect(QRectF(p.x() - 4, p.y() - 4, 8, 8));
            }
            QPointF const top = layerView.map(QPointF(bounds.center().x(), bounds.top()));
            QPointF const knob = layerView.map(handlePos(Rotate, bounds));
            painter.drawLine(top, knob);
            painter.drawEllipse(knob, 5, 5);
        }
    }
    if (m_drag == DragMarquee || m_drag == DragPaint || m_drag == DragPointMarquee) {
        painter.setTransform(layerView);
        QPen pen(accent);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 40));
        if (m_dragLasso && m_drag != DragPointMarquee)
            painter.drawPolygon(m_lassoPath);
        else
            painter.drawRect(QRectF(m_dragStart, m_dragNow).normalized());
    }
}
