// Eclipse - the Camera view: the pen let go after the tools that finish at once, and after drawing a line or a shape.
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

bool Canvas::penUpTool(Drag drag)
{
    Q_UNUSED(drag);
if (drag == DragSplineOffset) {
    QPointF const by = splineOffset();
    m_drag = DragNone;
    if (QLineF(m_splineFrom, m_splineNow).length() >= 1.0)
        m_document->offsetPath(m_layer, by);
    update();
    return true;
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
    return true;
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
    return true;
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
    return true;
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
    return true;
}
if (drag == DragInk || drag == DragSmooth || drag == DragRetouch) {
    m_drag = DragNone;
    if (m_inked) {
        Drawing art = m_eraseArt;
        if (drag == DragInk)
            finishInk(art, m_inkOrderNow);
        commitDrawing(art, drag == DragInk ? tr("Ink") : drag == DragRetouch ? tr("Pencil Retouch") : tr("Smooth"));
        if (drag == DragInk && m_inkSelectNew)
            setSelection(m_inkedNow, {});
    }
    m_inkedNow.clear();
    m_eraseArt = Drawing();
    m_inked = false;
    update();
    return true;
}
if (drag == DragThickness) {
    m_drag = DragNone;
    Drawing const* const d = currentDrawing();
    if (d && m_thickStroke >= 0 && m_thickStroke < m_eraseArt.strokes.size() && m_thickStroke < d->strokes.size()) {
        Stroke& line = m_eraseArt.strokes[m_thickStroke];
        if (!m_thickMoved && m_thickOneSide && m_thickExisting) {
            // Alt and a click on a thickness point: it goes (the thickness runs on between its neighbours).
            if (line.thickness.size() > 1 && m_thickKey >= 0 && m_thickKey < line.thickness.size()) {
                line.thickness.removeAt(m_thickKey);
                Drawing const art = m_eraseArt;
                commitDrawing(art, tr("Remove Thickness Point"));
            }
        } else if (m_thickMoved) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, tr("Pencil Line Thickness"));
        }
    }
    m_eraseArt = Drawing();
    m_thickStroke = m_thickKey = -1;
    update();
    return true;
}
if (drag == DragGradient) {
    m_drag = DragNone;
    // Kept, for Use Stored Colour Gradient: new paint in a gradient lies the same way.
    if (m_gradMoved) {
        m_storedGradFrom = m_gradFrom;
        m_storedGradTo = m_gradTo;
        m_hasStoredGradient = true;
    }
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
    return true;
}
if (drag == DragHint) {
    m_drag = DragNone;
    hintUp();
    return true;
}
if (drag == DragBoneIK) {
    m_drag = DragNone;
    if (m_ikBegun)
        m_document->endMacro();
    m_ikBegun = false;
    m_ikChain.clear();
    update();
    return true;
}
if (drag == DragErase && (m_eraserCuts || bitmapNow())) {
    m_drag = DragNone;
    cutWithEraser();
    update();
    return true;
}
    return false;
}

bool Canvas::penUpDraw(Drag drag)
{
    Q_UNUSED(drag);
    m_drag = DragNone;
    // The stabiliser's Catch Up: the line goes on to where the pen lifted.
    if (m_stabiliser == StabiliserCatchUp && (m_tool == Brush || m_tool == Pencil) && !m_stroke.points.isEmpty()) {
        QPointF const from = m_stroke.points.last().pos;
        float const pressure = m_stroke.points.last().pressure;
        double const length = QLineF(from, m_stabiliserPen).length() * m_zoom;
        int const steps = int(std::ceil(length / 2.0));
        for (int i = 1; i <= steps; ++i)
            m_stroke.points.push_back({from + (m_stabiliserPen - from) * (double(i) / steps), pressure});
    }
    // Line Pushing, Auto Adjust Thickness: the stroke is done to the line it was drawn along, and is not kept.
    if (m_tool == Pencil && m_pencilMode != PencilDraw && !bitmapNow()) {
        Stroke const highlight = m_stroke;
        m_stroke = Stroke();
        Drawing edited = currentDrawing() ? *currentDrawing() : Drawing();
        if (pushLine(edited, highlight, m_pencilMode, 16.0 / std::max(m_zoom, 1e-6)))
            commitDrawing(edited, m_pencilMode == PencilPush ? tr("Push Line") : tr("Adjust Line Thickness"));
        else
            emit message(tr("Draw along a pencil line to push it, or to make it thicker."));
        update();
        return true;
    }
    double pressed = 1.0;
    if (!m_rawPressure.isEmpty()) {
        pressed = 0.0;
        for (float p : std::as_const(m_rawPressure))
            pressed += p;
        pressed /= m_rawPressure.size();
    }
    double const tolerance = (0.2 + smoothingFor(pressed) * 0.038) / m_zoom;
    if (m_tool == Pencil && m_pencilMinOpacity < 100)
        smoothOpacity(m_stroke, m_opacitySmoothing);
    bool const ownThick = (m_tool == Pencil || m_tool == Brush) && m_stroke.width > 0.0f && !bitmapNow();
    Stroke thick = m_stroke;
    if (m_tool == Brush) {
        LineOptions speedOnly = m_lineOptions;
        speedOnly.startTaper = speedOnly.endTaper = speedOnly.fade = 0;
        applyTaperAndSpeed(thick, speedOnly, m_zoom);
    }
    if (m_tool == Brush || m_tool == Pencil)
        thick.points = evenlySampled(thick.points, 2.0 / std::max(m_zoom, 1e-6));
    int const stencil = thicknessStencilOf(Pencil);
    QVector<float> const& custom = m_customStencils[stencilSlot(Pencil)];
    if (m_tool == Pencil && stencil == StencilCount && !custom.isEmpty())
        applyProfile(thick, custom);
    else if (m_tool == Pencil && stencil != StencilUniform && stencil < StencilCount)
        applyStencil(thick, stencil);
    if (m_tool == Brush) {
        LineOptions shapeOnly = m_lineOptions;
        shapeOnly.speed = 0;
        applyTaperAndSpeed(thick, shapeOnly, m_zoom);
    }
    // (A line that faded away ends where it did; a bitmap stroke is painted along all of its samples.)
    if ((m_tool == Brush || m_tool == Pencil) && (!ownThick || (m_tool == Brush && m_lineOptions.fade > 0)))
        m_stroke.points = thick.points;
    // The line options (Tool Properties): trimmed and carried on to the lines it meets.
    double const near = 12.0 / std::max(m_zoom, 1e-6);
    if ((m_tool == Pencil || m_tool == StrokeTool) && currentDrawing()) {
        if (m_lineOptions.trim)
            trimExtra(m_stroke, *currentDrawing(), 30.0 / std::max(m_zoom, 1e-6));
        if (m_lineOptions.connect)
            connectEnds(m_stroke, *currentDrawing(), near);
    }
    if (m_snapping && m_snapGrid && (m_tool == Brush || m_tool == Pencil) && m_stroke.points.size() >= 2) {
        // Snap to Grid: the line begins and ends on a crossing of the grid when it is near one.
        m_stroke.points.first().pos = snapped(m_stroke.points.first().pos);
        m_stroke.points.last().pos = snapped(m_stroke.points.last().pos);
    }
    QVector<StrokePoint> const drawnSamples = thick.points;
    if (ownThick)
        for (StrokePoint& point : m_stroke.points) {
            point.pressure = 1.0f;
            point.lean = 0.0f;
        }
    m_stroke.fit(tolerance, m_cornerAngle);
    if (ownThick)
        m_stroke.takeThicknessFrom(drawnSamples);
    if (m_tool == StrokeTool && m_lineOptions.straight) {
        // Straight pieces from point to point.
        m_stroke.curve = false;
        for (StrokePoint& point : m_stroke.points) {
            point.handles = false;
            point.in = point.out = QPointF();
        }
    }
    bool const colourToo = m_autoColourArt && m_art == LineArt && (m_tool == Brush || m_tool == Pencil) && !bitmapNow();
    bool const finishing = ((m_tool == Brush || m_tool == Pencil) && !bitmapNow() && (m_lineOptions.closeGap || m_lineOptions.building)) || colourToo;
    if (finishing)
        m_document->beginMacro(tr("Draw Stroke"));
    double const reach = m_stroke.width / 2.0 + 8.0 / std::max(m_zoom, 1e-6);
    QRectF const box = m_stroke.bounds();
    double room = 0.0;
    {
        QVector<StrokePoint> const run = m_stroke.path();
        for (int i = 0; i < run.size(); ++i) {
            QPointF const a = run[i].pos, b = run[(i + 1) % run.size()].pos;
            room += a.x() * b.y() - b.x() * a.y();
        }
        room = std::abs(room) / 2.0;
    }
    double const least = std::max(double(m_stroke.width), 2.0 / std::max(m_zoom, 1e-6));
    if (m_lineAutoFill && (m_tool == Brush || m_tool == Pencil) && m_stroke.points.size() >= 3 && !bitmapNow()
        && QLineF(m_stroke.points.first().pos, m_stroke.points.last().pos).length() <= reach && std::max(box.width(), box.height()) > reach * 2.0
        && room > least * least * 4.0) {
        m_document->beginMacro(tr("Draw Stroke"));
        fillUnder(m_stroke);
        keepStroke(m_stroke, true, tolerance);
        m_document->endMacro();
    } else {
        keepStroke(m_stroke, true, tolerance);
    }
    if (finishing) {
        finishDrawnLine(near);
        if (colourToo && realDrawing()) {
            // The line just drawn (or the edge of the brush shape just made), with no thickness, into the colour art.
            Drawing whole = *realDrawing();
            QVector<Stroke> edges;
            if (m_tool == Brush && m_brushShapes && !whole.fills.isEmpty())
                edges = whole.fills.last().contours;
            else if (!whole.strokes.isEmpty())
                edges = {whole.strokes.last()};
            for (Stroke edge : std::as_const(edges)) {
                edge.width = 0.0f;
                edge.group = 0;
                whole.colourStrokes.push_back(edge);
            }
            if (!edges.isEmpty()) {
                m_committing = true;
                m_document->setDrawing(m_layer, m_frame, whole, tr("Draw Stroke"));
                m_committing = false;
            }
        }
        m_document->endMacro();
    }
    return false;
}

bool Canvas::penUpShape(Drag drag)
{
    Q_UNUSED(drag);
    if (m_bending)
        return true;
    // A click without a drag draws nothing.
    bool const drawn = m_stroke.bounds().width() * m_zoom > m_stroke.width * m_zoom + 3 || m_stroke.bounds().height() * m_zoom > m_stroke.width * m_zoom + 3;
    if (drawn && m_tool == Line && m_lineMode != LineStraight && m_stroke.points.size() == 2) {
        // The Line tool's curves: now the pointer bends the line, and a click puts it down.
        m_bending = true;
        m_bendTail = false;
        m_bendFrom = m_stroke.points[0].pos;
        m_bendTo = m_stroke.points[1].pos;
        update();
        return true;
    }
    m_drag = DragNone;
    if (drawn)
        keepShape();
    return false;
}
