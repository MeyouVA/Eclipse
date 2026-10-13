// Eclipse - the Camera view: the Select tool - what is selected, its box and handles.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

void Canvas::setSelection(QVector<int> const& strokes, QVector<int> const& fills, QVector<int> const& pictures)
{
    QVector<int> allStrokes = strokes, allFills = fills;
    QVector<int> allPictures = pictures;
    std::sort(allPictures.begin(), allPictures.end());
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
    if (allStrokes == m_selStrokes && allFills == m_selFills && allPictures == m_selPictures)
        return;
    m_selStrokes = allStrokes;
    m_selFills = allFills;
    m_selPictures = allPictures;
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
    if (m_art == LineArt)
        for (int i : m_selPictures)
            if (i >= 0 && i < d->pictures.size())
                add(d->pictures[i].bounds());
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
        if (h == Rotate && m_tool == Perspective)
            continue;       // that tool pulls corners only
        QPointF const p = view.map(handlePos(h, bounds));
        if (std::abs(p.x() - widgetPos.x()) <= 7 && std::abs(p.y() - widgetPos.y()) <= 7)
            return h;
    }
    return NoHandle;
}

// Snap and Align: how far the selection is to be moved, when `by` would bring an edge or the middle
// of it within a few pixels of an edge or the middle of something else in the drawing.
QPointF Canvas::aligned(QPointF by)
{
    m_alignLines.clear();
    Drawing const* const d = currentDrawing();
    if (!m_snapAlign || m_tool != Select || !d)
        return by;
    QRectF const box = m_dragBounds.translated(by);
    double const near = 6.0 / std::max(m_zoom, 1e-6);
    double bestX = near, bestY = near, moveX = 0.0, moveY = 0.0;
    QRectF withX, withY;
    bool foundX = false, foundY = false;
    double atX = 0.0, atY = 0.0;
    auto look = [&](QRectF const& other) {
        double const mineX[3] = {box.left(), box.center().x(), box.right()}, theirsX[3] = {other.left(), other.center().x(), other.right()};
        double const mineY[3] = {box.top(), box.center().y(), box.bottom()}, theirsY[3] = {other.top(), other.center().y(), other.bottom()};
        for (int i = 0; i < 3; ++i) {
            for (int k = 0; k < 3; ++k) {
                if (std::abs(theirsX[k] - mineX[i]) < bestX) {
                    bestX = std::abs(theirsX[k] - mineX[i]);
                    moveX = theirsX[k] - mineX[i];
                    atX = theirsX[k];
                    withX = other;
                    foundX = true;
                }
                if (std::abs(theirsY[k] - mineY[i]) < bestY) {
                    bestY = std::abs(theirsY[k] - mineY[i]);
                    moveY = theirsY[k] - mineY[i];
                    atY = theirsY[k];
                    withY = other;
                    foundY = true;
                }
            }
        }
    };
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            look(d->strokes[i].bounds());
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            look(d->fills[i].bounds());
    QRectF const moved = box.translated(moveX, moveY);
    if (foundX)
        m_alignLines.push_back(QLineF(atX, std::min(moved.top(), withX.top()), atX, std::max(moved.bottom(), withX.bottom())));
    if (foundY)
        m_alignLines.push_back(QLineF(std::min(moved.left(), withY.left()), atY, std::max(moved.right(), withY.right()), atY));
    return by + QPointF(moveX, moveY);
}

// Snap and Align while scaling: the corner in hand goes onto an edge or the middle of something
// else in the drawing when it comes within a few pixels of it.
QPointF Canvas::alignedCorner(QPointF corner)
{
    m_alignLines.clear();
    Drawing const* const d = currentDrawing();
    if (!m_snapAlign || m_tool != Select || !d)
        return corner;
    double const near = 6.0 / std::max(m_zoom, 1e-6);
    double bestX = near, bestY = near;
    QPointF to = corner;
    QRectF withX, withY;
    bool foundX = false, foundY = false;
    auto look = [&](QRectF const& other) {
        double const theirsX[3] = {other.left(), other.center().x(), other.right()};
        double const theirsY[3] = {other.top(), other.center().y(), other.bottom()};
        for (int k = 0; k < 3; ++k) {
            if (std::abs(theirsX[k] - corner.x()) < bestX) {
                bestX = std::abs(theirsX[k] - corner.x());
                to.setX(theirsX[k]);
                withX = other;
                foundX = true;
            }
            if (std::abs(theirsY[k] - corner.y()) < bestY) {
                bestY = std::abs(theirsY[k] - corner.y());
                to.setY(theirsY[k]);
                withY = other;
                foundY = true;
            }
        }
    };
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            look(d->strokes[i].bounds());
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            look(d->fills[i].bounds());
    if (foundX)
        m_alignLines.push_back(QLineF(to.x(), std::min(to.y(), withX.top()), to.x(), std::max(to.y(), withX.bottom())));
    if (foundY)
        m_alignLines.push_back(QLineF(std::min(to.x(), withY.left()), to.y(), std::max(to.x(), withY.right()), to.y()));
    return to;
}

// Adds every line and painted area of that colour to the lists.
static void sameColour(Drawing const& d, int swatch, QColor const& colour, QVector<int>& strokes, QVector<int>& fills)
{
    for (int i = 0; i < d.strokes.size(); ++i)
        if (d.strokes[i].swatch == swatch && d.strokes[i].color == colour && !strokes.contains(i))
            strokes.push_back(i);
    for (int i = 0; i < d.fills.size(); ++i)
        if (d.fills[i].swatch == swatch && d.fills[i].color == colour && !fills.contains(i))
            fills.push_back(i);
}

bool Canvas::selectAt(QPointF p, bool toggle)
{
    Drawing const* const d = currentDrawing();
    QVector<int> strokes = toggle ? m_selStrokes : QVector<int>();
    QVector<int> fills = toggle ? m_selFills : QVector<int>();
    QVector<int> pictures = toggle ? m_selPictures : QVector<int>();
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
                if (m_selectByColour) {
                    sameColour(*d, d->strokes[i].swatch, d->strokes[i].color, strokes, fills);
                    setSelection(strokes, fills);
                    return true;
                }
                flip(strokes, i);
                setSelection(strokes, fills, pictures);
                return true;
            }
        }
        for (int i = int(d->fills.size()) - 1; i >= 0; --i) {
            if (d->fills[i].contains(p)) {
                if (m_selectByColour) {
                    sameColour(*d, d->fills[i].swatch, d->fills[i].color, strokes, fills);
                    setSelection(strokes, fills);
                    return true;
                }
                flip(fills, i);
                setSelection(strokes, fills, pictures);
                return true;
            }
        }
        // A picture, under the lines and the paint: pressed anywhere on it.  (They are picked on the line art, which they lie with.)
        for (int i = m_art == LineArt ? int(d->pictures.size()) - 1 : -1; i >= 0; --i) {
            Picture const& picture = d->pictures[i];
            if (!picture.placement.isInvertible() || !QRectF(QPointF(0, 0), QSizeF(picture.size)).contains(picture.placement.inverted().map(p)))
                continue;
            flip(pictures, i);
            setSelection(strokes, fills, pictures);
            return true;
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
    QVector<int> pictures = add ? m_selPictures : QVector<int>();
    if (d) {
        // A picture is taken when all of it is inside the shape.
        for (int i = 0; m_art == LineArt && i < d->pictures.size(); ++i) {
            if (pictures.contains(i))
                continue;
            QPolygonF const corners = d->pictures[i].placement.map(QPolygonF(QRectF(QPointF(0, 0), QSizeF(d->pictures[i].size))));
            bool inside = !corners.isEmpty();
            for (QPointF const& corner : corners)
                inside = inside && within(corner);
            if (inside)
                pictures.push_back(i);
        }
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
    setSelection(strokes, fills, pictures);
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
