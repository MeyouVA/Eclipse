// Eclipse - the Camera view: the Cutter and painting inside a lasso; the line style of what is selected.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/FillRegion.h"

// The selected lines - or the one the contour editor shows - restyled as one undo step.
template <class Change> void Canvas::restyleSelection(QString const& text, Change change)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    QVector<int> which = selecting() ? m_selStrokes : QVector<int>();
    if (m_tool == ContourEditor && m_ceStroke >= 0 && m_ceStroke < d->strokes.size())
        which.push_back(m_ceStroke);
    if (which.isEmpty())
        return;
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(which)) {
        Stroke const before = edited.strokes[i];
        change(edited.strokes[i]);
        changed = changed || before.join != edited.strokes[i].join || before.capStart != edited.strokes[i].capStart
                  || before.capEnd != edited.strokes[i].capEnd;
    }
    if (changed)
        commitDrawing(edited, text);
    update();
}

void Canvas::setJoinStyle(int join)
{
    m_join = Stroke::Join(std::clamp(join, 0, 2));
    restyleSelection(tr("Change Line Join"), [this](Stroke& s) { s.join = m_join; });
}

void Canvas::setStartCap(int cap)
{
    m_capStart = Stroke::Cap(std::clamp(cap, 0, 1));
    restyleSelection(tr("Change Line Start"), [this](Stroke& s) { s.capStart = m_capStart; });
}

void Canvas::setEndCap(int cap)
{
    m_capEnd = Stroke::Cap(std::clamp(cap, 0, 1));
    restyleSelection(tr("Change Line End"), [this](Stroke& s) { s.capEnd = m_capEnd; });
}

QVector<int> Canvas::contourPointsPicked() const
{
    QVector<int> picked;
    Drawing const* const d = currentDrawing();
    if (!d || !contourValid())
        return picked;
    int const count = int(contourLine(*d)->points.size());
    for (int i : m_ceExtra)
        if (i >= 0 && i < count && i != m_cePoint && !picked.contains(i))
            picked.push_back(i);
    return picked;
}

void Canvas::cutInside(QPolygonF const& shape)
{
    Drawing const* const d = currentDrawing();
    if (!d || shape.size() < 3) {
        clearSelection();
        return;
    }
    double const fine = std::max(0.25 / m_zoom, 0.05);
    // Everything but the shape: a sheet far larger than the drawing, with the shape a hole in it.
    QRectF all = shape.boundingRect();
    for (Fill const& fill : d->fills)
        all |= fill.bounds();
    for (Stroke const& stroke : d->strokes)
        all |= stroke.bounds();
    all.adjust(-1000, -1000, 1000, 1000);
    QVector<QPolygonF> const within{shape};
    QVector<QPolygonF> const without{QPolygonF(all), shape};
    Drawing art = *d;
    art.fills.clear();
    art.strokes.clear();
    QVector<int> fills, strokes;
    bool cut = false;
    for (Fill const& fill : d->fills) {
        bool lost = false;
        QVector<Fill> const inside = fill.minus(without, fine, &lost);
        if (!lost) {                    // none of it is outside: taken whole
            fills.push_back(int(art.fills.size()));
            art.fills.push_back(fill);
        } else if (inside.isEmpty()) {  // all of it is outside
            art.fills.push_back(fill);
        } else {
            art.fills += fill.minus(within, fine);
            for (Fill const& piece : inside) {
                fills.push_back(int(art.fills.size()));
                art.fills.push_back(piece);
            }
            cut = true;
        }
    }
    for (Stroke const& stroke : d->strokes) {
        bool lost = false;
        QVector<Stroke> const inside = stroke.minus(without, fine, &lost);
        if (!lost) {
            strokes.push_back(int(art.strokes.size()));
            art.strokes.push_back(stroke);
        } else if (inside.isEmpty()) {
            art.strokes.push_back(stroke);
        } else {
            art.strokes += stroke.minus(within, fine);
            for (Stroke const& piece : inside) {
                strokes.push_back(int(art.strokes.size()));
                art.strokes.push_back(piece);
            }
            cut = true;
        }
    }
    if (cut)
        commitDrawing(art, tr("Cut Artwork"));
    setSelection(strokes, fills);
}

void Canvas::paintInside(std::function<bool(QPointF)> const& within, QRectF const& reach)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    QColor const color = m_document->scene().resolve(m_swatch, Qt::black);
    Drawing edited;
    edited.pictures = d->pictures;
    edited.pinned = d->pinned;
    bool any = false;
    for (Stroke stroke : d->strokes) {
        bool inside = false;
        for (StrokePoint const& point : stroke.points)
            inside = inside || within(point.pos);
        // Lines take the colour from Paint and Repaint; they have one already, and are never unpainted.
        inside = inside && !keepsColour(stroke.swatch);      // a protected colour is left as it is
        if (inside && (m_tool == Paint || m_tool == Repaint) && (stroke.swatch != m_swatch || stroke.color != color)) {
            stroke.swatch = m_swatch;
            stroke.color = color;
            any = true;
        }
        edited.strokes.push_back(stroke);
    }
    for (Fill fill : d->fills) {
        bool inside = false;
        for (QPolygonF const& outline : fill.outlines)
            for (QPointF const& q : outline)
                inside = inside || within(q);
        inside = inside && !keepsColour(fill.swatch);
        if (inside && m_tool == Unpaint) {
            any = true;
            continue;       // the paint goes
        }
        if (inside && (m_tool == Paint || m_tool == Repaint) && (fill.swatch != m_swatch || fill.color != color)) {
            fill.swatch = m_swatch;
            fill.color = color;
            any = true;
        }
        edited.fills.push_back(fill);
    }
    if (m_tool == Paint || m_tool == PaintUnpainted) {
        // The closed areas with no paint that lie wholly inside what was drawn round: found
        // from a few places across it, each filled as a click there would fill it.
        int const across = 6;
        for (int row = 0; row < across; ++row) {
            for (int column = 0; column < across; ++column) {
                QPointF const at(reach.left() + reach.width() * (column + 0.5) / across, reach.top() + reach.height() * (row + 0.5) / across);
                if (!within(at))
                    continue;
                bool painted = false;
                for (Fill const& fill : std::as_const(edited.fills))
                    painted = painted || fill.contains(at);
                if (painted)
                    continue;
                Drawing bounded = edited;
                if (m_art != LineArt && realDrawing()) {
                    bounded.strokes += realDrawing()->strokes;
                    for (Fill const& shape : realDrawing()->fills)
                        if (!shape.contours.isEmpty())
                            bounded.fills.push_back(shape);
                }
                QVector<QPolygonF> outlines;
                if (FillRegion::compute(bounded, FillRegion::paintArea(bounded), at, 2.0, outlines) != FillRegion::Ok || outlines.isEmpty())
                    continue;
                QRectF area;
                for (QPolygonF const& outline : std::as_const(outlines))
                    area |= outline.boundingRect();
                if (!reach.adjusted(-1, -1, 1, 1).contains(area))
                    continue;       // it runs on outside what was drawn round: left alone
                Fill fill;
                fill.outlines = outlines;
                fill.swatch = m_swatch;
                fill.color = color;
                int under = int(edited.fills.size());
                for (int i = 0; i < edited.fills.size(); ++i)
                    if (!edited.fills[i].contours.isEmpty()) {
                        under = i;
                        break;
                    }
                edited.fills.insert(under, fill);
                any = true;
            }
        }
    }
    if (any)
        commitDrawing(edited, m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
}

void Canvas::setLasso(bool on)
{
    if (on == m_lasso)
        return;
    m_lasso = on;
    emit lassoChanged(on);
}
