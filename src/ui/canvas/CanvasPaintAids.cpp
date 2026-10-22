// Eclipse - the Camera view painted: over the drawings - the frame's edge, grids, guides, each tool's aids, the selection, the marquee.
#include "ui/canvas/Canvas.h"
#include "ui/window/InterfaceColours.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

bool Canvas::paintViewAids(QPainter& painter, QRectF const& frameRect, QTransform const& view)
{
    Scene const& scene = m_document->scene();
    Q_UNUSED(view);
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
        return true;
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
        return true;
    }
    if (m_tool == ContourEditor && !m_playing) {
        paintContourAids(painter);
        paintMarquee(painter);      // (the box dragged round points of the curve)
        return true;
    }
    if (m_tool == IK && !m_playing && m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        if (layer.bones.size() >= 2)
            paintBones(painter, Deform::posedJoints(layer.bones, poseOf(m_layer, m_frame).bend), m_drag == DragIK ? m_bone : -1);
        return true;
    }
    if (m_tool == Rigging && !m_playing && m_layer >= 0 && m_layer < scene.layers.size()) {
        if (m_rigMode == RigWeights) {
            // The pull painted for the chosen bone: warm where it is added, cool where it is taken away; the bone itself picked out.
            Layer const& layer = scene.layers[m_layer];
            QTransform const toWidget = layerViewTransform();
            painter.save();
            painter.resetTransform();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            QVector<SkinDab> all = layer.skin;
            all += m_weightDabs;
            for (SkinDab const& d : std::as_const(all)) {
                if (d.bone != m_weightBone)
                    continue;
                QPointF const c = toWidget.map(d.at);
                double const r = QLineF(toWidget.map(d.at), toWidget.map(d.at + QPointF(d.radius, 0.0))).length();
                QRadialGradient glow(c, std::max(r, 1.0));
                QColor const tint = d.strength >= 0 ? QColor(255, 90, 40) : QColor(60, 140, 255);
                QColor inner = tint, outer = tint;
                inner.setAlphaF(float(std::min(1.0, std::abs(d.strength)) * 0.35));
                outer.setAlphaF(0.0f);
                glow.setColorAt(0.0, inner);
                glow.setColorAt(1.0, outer);
                painter.setBrush(glow);
                painter.drawEllipse(c, r, r);
            }
            painter.restore();
            QVector<QPointF> const joints = weightJoints();
            paintBones(painter, joints, m_weightBone + 1 < joints.size() ? m_weightBone + 1 : -1);
            return true;
        }
        if (m_rigMode != RigBones) {
            paintWarp(painter, warpRestDragged(), scene.layers[m_layer].warpClosed, m_drag == DragWarpRest ? m_warpIndex : -1);
            return true;
        }
        // The chain at rest, with the joint being dragged where the pointer has it.
        QVector<QPointF> joints = scene.layers[m_layer].bones;
        if (m_drag == DragJoint && m_bone >= 0 && m_bone < joints.size())
            joints[m_bone] = m_dragNow;
        paintBones(painter, joints, m_drag == DragJoint ? m_bone : -1);
        return true;
    }
    return false;
}

void Canvas::paintSelectionAids(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    Q_UNUSED(scene);
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
        if (m_hideManipulator && m_tool == Select) {
            // Hide Manipulator Controls: no box and no handles.
        } else if (m_drag == DragRotate || (m_tool == Perspective && m_drag == DragScale)) {
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
    paintMarquee(painter);
}

void Canvas::paintMarquee(QPainter& painter)
{
    // The box (or lasso) being dragged round what is to be picked.
    if (m_drag != DragMarquee && m_drag != DragPaint && m_drag != DragPointMarquee)
        return;
    QColor const accent(64, 156, 255);
    painter.save();
    painter.setTransform(layerViewTransform());
    QPen pen(accent);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 40));
    if (m_dragLasso && m_drag != DragPointMarquee)
        painter.drawPolygon(m_lassoPath);
    else
        painter.drawRect(QRectF(m_dragStart, m_dragNow).normalized());
    painter.restore();
}
