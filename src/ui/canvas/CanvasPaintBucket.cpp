// Eclipse - the Camera view: the paint bucket and the dropper.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/FillRegion.h"

void Canvas::paintAt(QPointF p)
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!d) {
        emit message(tr("There is no drawing on this frame to paint."));
        return;
    }
    QColor const color = scene.resolve(m_swatch, Qt::black);
    Drawing edited = *d;

    if (m_tool == Unpaint) {
        // Takes the paint under the point away.  Lines stay.
        for (int i = int(edited.fills.size()) - 1; i >= 0; --i) {
            if (edited.fills[i].contains(p)) {
                edited.fills.removeAt(i);
                commitDrawing(edited, tr("Unpaint"));
                return;
            }
        }
        return;
    }
    // On a line: recolour the line.
    for (int i = int(edited.strokes.size()) - 1; i >= 0; --i) {
        if (edited.strokes[i].hit(p, 0.5 / m_zoom)) {
            if (m_tool == PaintUnpainted)
                return;  // it has its colour already
            if (edited.strokes[i].swatch == m_swatch && edited.strokes[i].color == color)
                return;
            edited.strokes[i].swatch = m_swatch;
            edited.strokes[i].color = color;
            commitDrawing(edited, tr("Paint Line"));
            return;
        }
    }
    // On paint: change its colour.
    for (int i = int(edited.fills.size()) - 1; i >= 0; --i) {
        if (edited.fills[i].contains(p)) {
            if (m_tool == PaintUnpainted)
                return;  // painted already: left as it is
            if (edited.fills[i].swatch == m_swatch && edited.fills[i].color == color)
                return;
            edited.fills[i].swatch = m_swatch;
            edited.fills[i].color = color;
            commitDrawing(edited, tr("Paint"));
            return;
        }
    }
    if (m_tool == Repaint)
        return;  // only what is painted already
    // Otherwise fill the area the lines enclose around the point.
    QVector<QPolygonF> outlines;
    // In the colour art the lines over it bound the areas too: that is what it is for.
    Drawing bounded = *d;
    if (m_art != LineArt && realDrawing()) {
        bounded.strokes += realDrawing()->strokes;
        for (Fill const& shape : realDrawing()->fills)
            if (!shape.contours.isEmpty())
                bounded.fills.push_back(shape);
    }
    switch (FillRegion::compute(bounded, scene.width, scene.height, p, 2.0, outlines)) {
    case FillRegion::Ok: {
        Fill fill;
        fill.outlines = outlines;
        fill.swatch = m_swatch;
        fill.color = color;
        // Under the brush shapes that bound it: the paint reaches a little way under the
        // lines round it, and must not show over them.
        int under = int(edited.fills.size());
        for (int i = 0; i < edited.fills.size(); ++i)
            if (!edited.fills[i].contours.isEmpty()) {
                under = i;
                break;
            }
        edited.fills.insert(under, fill);
        commitDrawing(edited, tr("Paint"));
        break;
    }
    case FillRegion::NotClosed:
        emit message(tr("That area is not closed: the paint would leak out. Close the gap in the lines first."));
        break;
    default:
        break;
    }
}

void Canvas::pickColourAt(QPointF widgetPos)
{
    // What is in front is found first: lines before paint, front layers before those behind.
    Scene const& scene = m_document->scene();
    QPointF const at = toScene(widgetPos);
    QVector<int> const order = scene.drawOrder(m_frame);
    for (int place = int(order.size()) - 1; place >= 0; --place) {
        int const l = order[place];
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(m_frame);
        if (!scene.shows(l) || !layer.holdsDrawings() || index == Layer::kNoDrawing)
            continue;
        QPointF const p = scene.worldTransform(l, m_frame).inverted().map(at);
        Drawing const& drawing = layer.drawings[index];
        for (int i = int(drawing.strokes.size()) - 1; i >= 0; --i) {
            if (drawing.strokes[i].hit(p, 2.0 / m_zoom)) {
                emit colourPicked(drawing.strokes[i].swatch, scene.resolve(drawing.strokes[i].swatch, drawing.strokes[i].color));
                return;
            }
        }
        for (int i = int(drawing.fills.size()) - 1; i >= 0; --i) {
            if (drawing.fills[i].contains(p)) {
                emit colourPicked(drawing.fills[i].swatch, scene.resolve(drawing.fills[i].swatch, drawing.fills[i].color));
                return;
            }
        }
        for (int sheet : {int(Drawing::OverlaySheet), int(Drawing::UnderlaySheet)}) {
            for (int i = int(drawing.strokesOf(sheet).size()) - 1; i >= 0; --i) {
                Stroke const& stroke = drawing.strokesOf(sheet)[i];
                if (stroke.hit(p, 2.0 / m_zoom)) {
                    emit colourPicked(stroke.swatch, scene.resolve(stroke.swatch, stroke.color));
                    return;
                }
            }
            for (int i = int(drawing.fillsOf(sheet).size()) - 1; i >= 0; --i) {
                Fill const& fill = drawing.fillsOf(sheet)[i];
                if (fill.contains(p)) {
                    emit colourPicked(fill.swatch, scene.resolve(fill.swatch, fill.color));
                    return;
                }
            }
        }
        for (int i = int(drawing.colourStrokes.size()) - 1; i >= 0; --i) {
            if (drawing.colourStrokes[i].hit(p, 2.0 / m_zoom)) {
                emit colourPicked(drawing.colourStrokes[i].swatch, scene.resolve(drawing.colourStrokes[i].swatch, drawing.colourStrokes[i].color));
                return;
            }
        }
        for (int i = int(drawing.colourFills.size()) - 1; i >= 0; --i) {
            if (drawing.colourFills[i].contains(p)) {
                emit colourPicked(drawing.colourFills[i].swatch, scene.resolve(drawing.colourFills[i].swatch, drawing.colourFills[i].color));
                return;
            }
        }
    }
    emit message(tr("There is nothing there to take a colour from."));
}
