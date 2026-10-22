// Eclipse - the Camera view: the paint bucket and the dropper.
#include "canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include <QPainterPath>
#include <QPainterPathStroker>

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

    QString const kept = tr("That colour is protected: the paint tools leave it as it is. (Colour view: right-click the colour.)");
    if (m_tool == Unpaint) {
        // On a line: it loses its paint - it is there still, to bound areas, but is not seen.
        for (int i = int(edited.strokes.size()) - 1; i >= 0; --i) {
            if (edited.strokes[i].width > 0.0f && edited.strokes[i].hit(p, 0.5 / m_zoom)) {
                if (keepsColour(edited.strokes[i].swatch)) {
                    emit message(kept);
                    return;
                }
                edited.strokes[i].width = 0.0f;
                commitDrawing(edited, tr("Unpaint Line"));
                return;
            }
        }
        // Otherwise the paint under the point goes.
        for (int i = int(edited.fills.size()) - 1; i >= 0; --i) {
            if (edited.fills[i].contains(p)) {
                if (keepsColour(edited.fills[i].swatch)) {
                    emit message(kept);
                    return;
                }
                edited.fills.removeAt(i);
                commitDrawing(edited, tr("Unpaint"));
                return;
            }
        }
        return;
    }
    bool paintUnder = false;
    for (Fill const& fill : std::as_const(edited.fills))
        paintUnder = paintUnder || fill.contains(p);
    for (int i = int(edited.strokes.size()) - 1; i >= 0 && !paintUnder; --i) {
        if (edited.strokes[i].hit(p, 0.5 / m_zoom)) {
            if (m_tool == PaintUnpainted)
                return;  // it has its colour already
            if (keepsColour(edited.strokes[i].swatch)) {
                emit message(kept);
                return;
            }
            if (edited.strokes[i].swatch == m_swatch && edited.strokes[i].color == color)
                return;
            edited.strokes[i].swatch = m_swatch;
            edited.strokes[i].color = color;
            commitDrawing(edited, tr("Paint Line"));
            return;
        }
    }
    {
        int shape = -1;
        QPolygonF round;
        for (int i = 0; i < edited.strokes.size(); ++i) {
            Stroke const& line = edited.strokes[i];
            if (line.points.size() < 2)
                continue;
            // (Ends meeting: within half the line's thickness, and any gap the Close Gap option closes.)
            double const reach = line.width / 2.0 + gapNow();
            bool painted = false;
            for (Fill const& f : std::as_const(edited.fills))
                painted = painted || (line.link != 0 && f.link == line.link);
            bool const closed = line.closed || painted || (line.points.size() >= 3 && QLineF(line.points.first().pos, line.points.last().pos).length() <= reach);
            if (!closed)
                continue;
            QPolygonF outline;
            for (StrokePoint const& q : line.path())
                outline << q.pos;
            if (outline.size() < 3 || !outline.containsPoint(p, Qt::WindingFill))
                continue;
            // The one on top: the highest in the order, the later of those.
            if (shape < 0 || line.order >= edited.strokes[shape].order) {
                shape = i;
                round = outline;
            }
        }
        if (shape >= 0) {
            Stroke& line = edited.strokes[shape];
            // Painted already: its paint takes the colour.
            int painted = -1;
            for (int i = 0; i < edited.fills.size() && line.link != 0; ++i)
                if (edited.fills[i].link == line.link)
                    painted = i;
            if (painted >= 0) {
                if (m_tool == PaintUnpainted)
                    return;
                if (keepsColour(edited.fills[painted].swatch)) {
                    emit message(kept);
                    return;
                }
                if (edited.fills[painted].swatch == m_swatch && edited.fills[painted].color == color)
                    return;
                edited.fills[painted].swatch = m_swatch;
                edited.fills[painted].color = color;
                commitDrawing(edited, tr("Paint"));
                return;
            }
            if (m_tool != Repaint) {
                int next = 1;
                for (Stroke const& s : std::as_const(edited.strokes))
                    next = std::max(next, s.link + 1);
                for (Fill const& f : std::as_const(edited.fills))
                    next = std::max(next, f.link + 1);
                if (Drawing const* const whole = realDrawing())
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        for (Stroke const& s : whole->strokesOf(sheet))
                            next = std::max(next, s.link + 1);
                        for (Fill const& f : whole->fillsOf(sheet))
                            next = std::max(next, f.link + 1);
                    }
                line.link = next;
                // Paint made before inside this shape, in pieces cut by the lines crossing it: the shape's paint takes over.
                QPainterPath reachOf;
                reachOf.addPolygon(round);
                reachOf.closeSubpath();
                QPainterPathStroker widen;
                widen.setWidth(line.width + 2.0 * (gapNow() + 2.0));
                reachOf = reachOf.united(widen.createStroke(reachOf));
                reachOf.setFillRule(Qt::WindingFill);
                for (int i = int(edited.fills.size()) - 1; i >= 0; --i) {
                    Fill const& old = edited.fills[i];
                    if (old.link != 0 || !old.contours.isEmpty() || keepsColour(old.swatch))
                        continue;
                    bool within = !old.outlines.isEmpty();
                    for (QPolygonF const& o : old.outlines)
                        for (QPointF const& q : o)
                            within = within && reachOf.contains(q);
                    if (within)
                        edited.fills.removeAt(i);
                }
                Fill area;
                area.outlines = {round};
                area.follows = true;
                area.link = next;
                area.seed = p;
                area.swatch = m_swatch;
                area.color = color;
                area.order = line.order;
                // Among the shapes' paint in the order of their lines: a later shape's paint over an earlier one's.
                int at = int(edited.fills.size());
                for (int i = 0; i < edited.fills.size(); ++i) {
                    int const k = edited.fills[i].link;
                    if (k == 0)
                        continue;
                    int lineOf = -1;
                    for (int j = 0; j < edited.strokes.size(); ++j)
                        if (edited.strokes[j].link == k)
                            lineOf = j;
                    if (lineOf > shape) {
                        at = i;
                        break;
                    }
                }
                edited.fills.insert(at, area);
                commitDrawing(edited, tr("Paint"));
                return;
            }
        }
    }
    // On paint: change its colour.
    for (int i = int(edited.fills.size()) - 1; i >= 0; --i) {
        if (edited.fills[i].contains(p)) {
            if (m_tool == PaintUnpainted)
                return;  // painted already: left as it is
            if (keepsColour(edited.fills[i].swatch)) {
                emit message(kept);
                return;
            }
            bool const textured = m_paintOverTexture && edited.fills[i].texture != 0;
            if (edited.fills[i].swatch == m_swatch && edited.fills[i].color == color && !textured)
                return;
            edited.fills[i].swatch = m_swatch;
            edited.fills[i].color = color;
            if (textured)
                edited.fills[i].texture = 0;    // Paint and Remove Texture: one plain colour now
            commitDrawing(edited, tr("Paint"));
            if (m_paintSelectNew)
                setSelection({}, {i});
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
    // (Wherever the artwork is - beside the frame too, or on a layer the camera sees moved.)
    switch (FillRegion::compute(bounded, FillRegion::paintArea(bounded), p, gapNow(), outlines)) {
    case FillRegion::Ok: {
        underShapes(outlines, bounded);
        Fill fill;
        fill.outlines = outlines;
        fill.follows = true;        // it goes where its lines go
        fill.seed = p;
        fill.swatch = m_swatch;
        fill.color = color;
        if (Swatch const* const sw = scene.findSwatch(m_swatch); m_useStoredGradient && m_hasStoredGradient && sw && sw->isPlaced()) {
            fill.gradPlaced = true;
            fill.gradFrom = m_storedGradFrom;
            fill.gradTo = m_storedGradTo;
        }
        int const at = underIndex(edited);
        edited.fills.insert(at, fill);
        commitDrawing(edited, tr("Paint"));
        if (m_paintSelectNew)
            setSelection({}, {at});
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
    // Do Not Pick Transparency: lines unpainted and colours that cannot be seen are passed over.
    auto seen = [&](int swatch, QColor const& colour, float width) {
        return !m_dropperNoTransparency || (width > 0.0f && scene.resolve(swatch, colour).alpha() > 0);
    };
    for (int place = int(order.size()) - 1; place >= 0; --place) {
        int const l = order[place];
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(m_frame);
        if (!scene.shows(l) || !layer.holdsDrawings() || index == Layer::kNoDrawing || (!m_dropperAllLayers && l != m_layer))
            continue;
        QPointF const p = scene.worldTransform(l, m_frame).inverted().map(at);
        Drawing const& drawing = layer.drawings[index];
        for (int i = int(drawing.strokes.size()) - 1; i >= 0; --i) {
            if (drawing.strokes[i].hit(p, 2.0 / m_zoom) && seen(drawing.strokes[i].swatch, drawing.strokes[i].color, drawing.strokes[i].width)) {
                tookColour(drawing.strokes[i].swatch, scene.resolve(drawing.strokes[i].swatch, drawing.strokes[i].color));
                return;
            }
        }
        for (int i = int(drawing.fills.size()) - 1; i >= 0; --i) {
            if (drawing.fills[i].contains(p) && seen(drawing.fills[i].swatch, drawing.fills[i].color, 1.0f)) {
                tookColour(drawing.fills[i].swatch, scene.resolve(drawing.fills[i].swatch, drawing.fills[i].color));
                return;
            }
        }
        for (int sheet : {int(Drawing::OverlaySheet), int(Drawing::UnderlaySheet)}) {
            for (int i = int(drawing.strokesOf(sheet).size()) - 1; i >= 0; --i) {
                Stroke const& stroke = drawing.strokesOf(sheet)[i];
                if (stroke.hit(p, 2.0 / m_zoom) && seen(stroke.swatch, stroke.color, stroke.width)) {
                    tookColour(stroke.swatch, scene.resolve(stroke.swatch, stroke.color));
                    return;
                }
            }
            for (int i = int(drawing.fillsOf(sheet).size()) - 1; i >= 0; --i) {
                Fill const& fill = drawing.fillsOf(sheet)[i];
                if (fill.contains(p) && seen(fill.swatch, fill.color, 1.0f)) {
                    tookColour(fill.swatch, scene.resolve(fill.swatch, fill.color));
                    return;
                }
            }
        }
        for (int i = int(drawing.colourStrokes.size()) - 1; i >= 0; --i) {
            if (drawing.colourStrokes[i].hit(p, 2.0 / m_zoom) && seen(drawing.colourStrokes[i].swatch, drawing.colourStrokes[i].color, drawing.colourStrokes[i].width)) {
                tookColour(drawing.colourStrokes[i].swatch, scene.resolve(drawing.colourStrokes[i].swatch, drawing.colourStrokes[i].color));
                return;
            }
        }
        for (int i = int(drawing.colourFills.size()) - 1; i >= 0; --i) {
            if (drawing.colourFills[i].contains(p) && seen(drawing.colourFills[i].swatch, drawing.colourFills[i].color, 1.0f)) {
                tookColour(drawing.colourFills[i].swatch, scene.resolve(drawing.colourFills[i].swatch, drawing.colourFills[i].color));
                return;
            }
        }
    }
    emit message(tr("There is nothing there to take a colour from."));
}

void Canvas::tookColour(int swatch, QColor const& colour)
{
    emit colourPicked(swatch, colour);
    // Not sticky: the tool before the Dropper comes back.
    if (!m_stickyDropper && m_tool == Dropper && m_toolBeforeDropper >= 0 && m_toolBeforeDropper != int(Dropper))
        QMetaObject::invokeMethod(this, [this] { setTool(Tool(m_toolBeforeDropper)); }, Qt::QueuedConnection);
}
