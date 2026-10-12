// Eclipse - the Camera view: the Select tool - what is selected, its box and handles.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

void Canvas::setSelection(QVector<int> const& strokes, QVector<int> const& fills)
{
    QVector<int> allStrokes = strokes, allFills = fills;
    // What is grouped with something picked is picked too.
    if (Drawing const* const d = currentDrawing()) {
        QVector<int> groups;
        for (int i : strokes)
            if (i >= 0 && i < d->strokes.size() && d->strokes[i].group != 0 && !groups.contains(d->strokes[i].group))
                groups.push_back(d->strokes[i].group);
        for (int i : fills)
            if (i >= 0 && i < d->fills.size() && d->fills[i].group != 0 && !groups.contains(d->fills[i].group))
                groups.push_back(d->fills[i].group);
        if (!groups.isEmpty()) {
            for (int i = 0; i < d->strokes.size(); ++i)
                if (groups.contains(d->strokes[i].group) && !allStrokes.contains(i))
                    allStrokes.push_back(i);
            for (int i = 0; i < d->fills.size(); ++i)
                if (groups.contains(d->fills[i].group) && !allFills.contains(i))
                    allFills.push_back(i);
        }
    }
    std::sort(allStrokes.begin(), allStrokes.end());
    std::sort(allFills.begin(), allFills.end());
    if (allStrokes == m_selStrokes && allFills == m_selFills)
        return;
    m_selStrokes = allStrokes;
    m_selFills = allFills;
    std::sort(m_selStrokes.begin(), m_selStrokes.end());
    std::sort(m_selFills.begin(), m_selFills.end());
    update();
    emit selectionChanged();
}

void Canvas::clearSelection()
{
    setSelection({}, {});
}

void Canvas::selectAll()
{
    Drawing const* const d = currentDrawing();
    if (!d || !selecting())
        return;
    QVector<int> strokes, fills;
    for (int i = 0; i < d->strokes.size(); ++i)
        strokes.push_back(i);
    for (int i = 0; i < d->fills.size(); ++i)
        fills.push_back(i);
    setSelection(strokes, fills);
}

QRectF Canvas::selectionBounds() const
{
    Drawing const* const d = currentDrawing();
    QRectF r;
    if (!d)
        return r;
    auto add = [&r](QRectF const& b) { r = r.isNull() ? b : r.united(b); };
    for (int i : m_selStrokes)
        add(d->strokes[i].bounds());
    for (int i : m_selFills)
        add(d->fills[i].bounds());
    return r;
}

QPointF Canvas::handlePos(Handle handle, QRectF const& b) const
{
    switch (handle) {
    case TopLeft:
        return b.topLeft();
    case TopRight:
        return b.topRight();
    case BottomRight:
        return b.bottomRight();
    case BottomLeft:
        return b.bottomLeft();
    case Rotate:
        return QPointF(b.center().x(), b.top() - 28.0 / m_zoom);  // a fixed distance on screen
    default:
        return b.center();
    }
}

Canvas::Handle Canvas::handleAt(QPointF widgetPos) const
{
    if (!hasSelection())
        return NoHandle;
    QRectF const bounds = selectionBounds();
    QTransform const view = layerViewTransform();
    for (Handle h : {Rotate, TopLeft, TopRight, BottomRight, BottomLeft}) {
        QPointF const p = view.map(handlePos(h, bounds));
        if (std::abs(p.x() - widgetPos.x()) <= 7 && std::abs(p.y() - widgetPos.y()) <= 7)
            return h;
    }
    return NoHandle;
}

bool Canvas::selectAt(QPointF p, bool toggle)
{
    Drawing const* const d = currentDrawing();
    QVector<int> strokes = toggle ? m_selStrokes : QVector<int>();
    QVector<int> fills = toggle ? m_selFills : QVector<int>();
    auto flip = [](QVector<int>& list, int i) {
        if (list.contains(i))
            list.removeAll(i);
        else
            list.push_back(i);
    };
    if (d) {
        // Lines are on top of paint, and later items on top of earlier ones.
        double const reach = 4.0 / m_zoom;
        for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
            if (d->strokes[i].hit(p, reach)) {
                flip(strokes, i);
                setSelection(strokes, fills);
                return true;
            }
        }
        for (int i = int(d->fills.size()) - 1; i >= 0; --i) {
            if (d->fills[i].contains(p)) {
                flip(fills, i);
                setSelection(strokes, fills);
                return true;
            }
        }
    }
    if (!toggle)
        setSelection({}, {});
    return false;
}

void Canvas::selectInRect(QRectF const& rect, bool add)
{
    selectInside([&rect](QPointF q) { return rect.contains(q); }, add);
}

void Canvas::selectInside(std::function<bool(QPointF)> const& within, bool add)
{
    Drawing const* const d = currentDrawing();
    QVector<int> strokes = add ? m_selStrokes : QVector<int>();
    QVector<int> fills = add ? m_selFills : QVector<int>();
    if (d) {
        // An item is taken when any of its points is inside the shape.
        for (int i = 0; i < d->strokes.size(); ++i) {
            if (strokes.contains(i))
                continue;
            for (StrokePoint const& sp : d->strokes[i].points) {
                if (within(sp.pos)) {
                    strokes.push_back(i);
                    break;
                }
            }
        }
        for (int i = 0; i < d->fills.size(); ++i) {
            if (fills.contains(i))
                continue;
            bool inside = false;
            for (QPolygonF const& outline : d->fills[i].outlines)
                for (QPointF const& q : outline)
                    inside = inside || within(q);
            if (inside)
                fills.push_back(i);
        }
    }
    setSelection(strokes, fills);
}

void Canvas::onActionSelectWithCurrentColour()
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    if (!selecting())
        setTool(Select);
    QColor const colour = m_document->scene().resolve(m_swatch, Qt::black);
    auto matches = [&](int swatch, QColor const& own) {
        return m_swatch != Scene::kNoSwatch ? swatch == m_swatch : (swatch == Scene::kNoSwatch && own == colour);
    };
    QVector<int> strokes, fills;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (matches(d->strokes[i].swatch, d->strokes[i].color))
            strokes.push_back(i);
    for (int i = 0; i < d->fills.size(); ++i)
        if (matches(d->fills[i].swatch, d->fills[i].color))
            fills.push_back(i);
    setSelection(strokes, fills);
}

void Canvas::onActionInvertSelection()
{
    Drawing const* const d = currentDrawing();
    if (!d || !selecting())
        return;
    QVector<int> strokes, fills;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            strokes.push_back(i);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            fills.push_back(i);
    setSelection(strokes, fills);
}
