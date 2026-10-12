// Eclipse - the Camera view: the pen pressed - what each tool starts.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"

void Canvas::penDown(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers)
{
    m_lastWidgetPos = widgetPos;
    if (panning || m_spaceDown || m_tool == Hand) {
        m_drag = DragPan;
        updateCursor();
        return;
    }
    if (m_tool == Zoom) {
        zoomAt(widgetPos, modifiers & Qt::AltModifier ? 1.0 / 1.5 : 1.5);
        return;
    }
    if (m_tool == RotateView) {
        m_drag = DragRotateView;
        return;
    }
    if (m_playing)
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    if (m_tool == Dropper) {
        pickColourAt(widgetPos);
        return;
    }
    if (m_tool == Transform) {
        transformDown(widgetPos);
        update();
        return;
    }
    if (m_tool == IK) {
        ikDown(widgetPos);
        update();
        return;
    }
    if (!scene.layers[m_layer].holdsDrawings()) {
        emit message(scene.layers[m_layer].kind == Layer::Sound
                         ? tr("A sound layer holds no drawings. Select a drawing layer to draw.")
                         : scene.layers[m_layer].kind == Layer::Camera
                         ? tr("The camera holds no drawings. Select a drawing layer to draw, or use the Transform tool to move the camera.")
                         : scene.layers[m_layer].kind == Layer::Peg
                         ? tr("A peg holds no drawings. Select a drawing layer to draw, or use the Transform tool to move the peg.")
                         : tr("This layer holds no drawings. Select a drawing layer to draw."));
        return;
    }
    if (!scene.layers[m_layer].visible)
        return;
    if (scene.layers[m_layer].locked) {
        emit message(tr("This layer is locked. Unlock it in the Timeline to draw on it."));
        return;
    }
    if (scene.layers[m_layer].morphing(m_frame)) {
        emit message(tr("This frame is in a morphing: its drawing is made from the two either side of it. Draw on one of those, or use Animation > Morphing > Convert Morphing to Drawings."));
        return;
    }
    QPointF const p = toLayer(widgetPos);

    if (m_tool == EditGradient) {
        gradientDown(widgetPos, p);
        return;
    }
    if (m_tool == Pencil || m_tool == StrokeTool)
        pressure = 1.0f;  // a pencil line has one thickness
    if (m_tool == Rigging) {
        riggingDown(widgetPos, p, modifiers);
    } else if (m_tool == ContourEditor) {
        contourDown(p, modifiers);
        emit pointPicked(pointThickness());
    } else if (m_tool == Text) {
        endText(true);
        // On words written before: they are taken up again, in the font they have.
        int words = -1;
        if (Drawing const* const d = currentDrawing())
            for (int i = int(d->fills.size()) - 1; i >= 0 && words < 0; --i)
                if (!d->fills[i].text.isEmpty() && d->fills[i].bounds().contains(p))
                    words = i;
        if (words >= 0) {
            Fill const& fill = currentDrawing()->fills[words];
            QFont font;
            if (font.fromString(fill.textFont)) {
                m_textFont = font;
                emit textFontPicked(font);
            }
            m_textAlign = fill.textAlign;
            m_textSpacing = int(std::lround(fill.textSpacing * 100.0));
            m_textKerning = fill.textKerning;
            emit textLayoutPicked(m_textAlign, m_textSpacing, m_textKerning);
            beginText(layerViewTransform().map(fill.textAt), fill.textAt, fill.text);
            m_textEdit = words;
        } else {
            beginText(widgetPos, p);
        }
    } else if (m_tool == Polyline) {
        if (!m_polyActive) {
            m_polyActive = true;
            m_stroke = Stroke();
            m_stroke.swatch = m_swatch;
            m_stroke.color = scene.resolve(m_swatch, Qt::black);
            m_stroke.width = float(m_brushWidth);
            m_stroke.join = m_join;
            m_stroke.capStart = m_capStart;
            m_stroke.capEnd = m_capEnd;
            setMouseTracking(true);
        } else if (m_stroke.points.size() >= 3 && QLineF(layerViewTransform().map(m_stroke.points.first().pos), widgetPos).length() <= 7) {
            // Back on the first point: the line closes into a shape.
            m_stroke.closed = true;
            finishPolyline(true);
            update();
            return;
        }
        // A click on the point just made adds nothing (the second click of a double click).
        if (m_stroke.points.isEmpty() || QLineF(layerViewTransform().map(m_stroke.points.last().pos), widgetPos).length() > 2) {
            StrokePoint point{snapped(p), 1.0f};
            point.corner = m_stroke.curve;      // on a line that bends elsewhere, a point clicked is a corner
            m_stroke.points.push_back(point);
            m_drag = DragPolyHandle;            // dragged on from here, the line bends through it
        }
        m_polyHover = snapped(p);
    } else if (m_tool == Line || m_tool == Rectangle || m_tool == Ellipse || m_tool == CloseGap) {
        m_drag = DragShape;
        m_dragStart = snapped(p);
        m_stroke = Stroke();
        m_stroke.swatch = m_swatch;
        m_stroke.color = scene.resolve(m_swatch, Qt::black);
        m_stroke.width = m_tool == CloseGap ? 0.0f : float(m_brushWidth);
        m_stroke.join = m_join;
        m_stroke.capStart = m_capStart;
        m_stroke.capEnd = m_capEnd;
        updateShape(snapped(p), modifiers);
    } else if (m_tool == Brush || m_tool == Pencil || m_tool == StrokeTool) {
        m_drag = DragDraw;
        m_stroke = Stroke();
        m_stroke.swatch = m_swatch;
        m_stroke.color = scene.resolve(m_swatch, Qt::black);
        m_stroke.width = m_tool == StrokeTool ? 0.0f : float(m_brushWidth);
        m_stroke.join = m_join;
        m_stroke.capStart = m_capStart;
        m_stroke.capEnd = m_capEnd;
        m_stroke.points.push_back({p, pressure});
    } else if (m_tool == Eraser) {
        m_drag = DragErase;
        if (m_eraserCuts) {
            // What it passes over is cut out as it goes, in a drawing of its own; the scene
            // gets the result, as one step, when it lifts.
            m_stroke = Stroke();
            m_stroke.width = float(m_brushWidth);
            m_stroke.points.push_back({p, 1.0f});
            Drawing const* const under = currentDrawing();
            m_eraseArt = under ? *under : Drawing();
            m_eraseAny = false;
            eraseStretch(0);
            update();
        } else {
            eraseArtAt(p, m_brushWidth / 2.0);
        }
    } else if (isPaintTool(m_tool)) {
        // A click paints what is under it (when the pen lifts); a drag draws round things to paint them all.
        m_drag = DragPaint;
        m_dragLasso = m_lasso != bool(modifiers & Qt::AltModifier);
        m_dragStart = m_dragNow = p;
        m_lassoPath = QPolygonF({p});
    } else if (selecting()) {
        m_dragStart = m_dragNow = p;
        m_dragTransform = QTransform();
        m_dragBounds = selectionBounds();
        m_dragHandle = handleAt(widgetPos);
        bool const shift = modifiers & Qt::ShiftModifier;
        if (m_dragHandle == Rotate) {
            m_drag = DragRotate;
        } else if (m_dragHandle != NoHandle) {
            m_drag = DragScale;
        } else {
            // Press inside the selection drags it.  Otherwise press on an item selects it
            // (and drags it); on empty space, start a marquee.  Shift adds or removes.
            if (hasSelection() && !shift && m_dragBounds.contains(p)) {
                m_drag = DragMove;
            } else if (m_tool != Cutter && selectAt(p, shift)) {
                m_dragBounds = selectionBounds();
                m_drag = shift || !hasSelection() ? DragNone : DragMove;
            } else {
                m_drag = DragMarquee;
                m_dragLasso = m_lasso != bool(modifiers & Qt::AltModifier);
                m_marqueeAdds = shift;
                m_lassoPath = QPolygonF({p});
            }
        }
    }
    update();
}
