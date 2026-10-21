// Eclipse - the Camera view: paint that follows its lines.  An area painted with the paint bucket (or filled under a line
// drawn round it) is the area its lines close round: when those lines are moved, bent or reshaped, it is found again from
// them - so the paint goes wherever the lines go, as it does in Harmony.
#include "ui/canvas/Canvas.h"

#include <algorithm>

#include "model/geometry/FillRegion.h"

namespace {

bool sameShape(Stroke const& a, Stroke const& b)
{
    if (a.points.size() != b.points.size() || a.closed != b.closed || a.curve != b.curve || a.width != b.width)
        return false;
    for (int i = 0; i < a.points.size(); ++i) {
        StrokePoint const& p = a.points[i];
        StrokePoint const& q = b.points[i];
        if (p.pos != q.pos || p.handles != q.handles || p.corner != q.corner || (p.handles && (p.in != q.in || p.out != q.out)))
            return false;
    }
    return true;
}

// What bounds the paint of a sheet: its lines, the line art's lines (for the other sheets), and the brush shapes there.
QVector<Stroke> boundsOf(Drawing const& d, int sheet)
{
    QVector<Stroke> lines = d.strokesOf(sheet);
    QVector<int> sheets{sheet};
    if (sheet != Drawing::LineSheet) {
        lines += d.strokesOf(Drawing::LineSheet);
        sheets.push_back(Drawing::LineSheet);
    }
    for (int s : std::as_const(sheets))
        for (Fill const& shape : d.fillsOf(s))
            for (Stroke const& contour : shape.contours)
                lines.push_back(contour);
    return lines;
}

// Where the lines changed: what is in one and not (just the same) in the other.
QRectF changedBetween(QVector<Stroke> const& now, QVector<Stroke> const& before)
{
    QRectF changed;
    QVector<bool> matched(before.size(), false);
    for (Stroke const& line : now) {
        bool found = false;
        for (int k = 0; k < before.size() && !found; ++k)
            if (!matched[k] && sameShape(line, before[k]))
                matched[k] = found = true;
        if (!found)
            changed = changed.united(line.bounds().adjusted(-line.width, -line.width, line.width, line.width));
    }
    for (int k = 0; k < before.size(); ++k)
        if (!matched[k])
            changed = changed.united(before[k].bounds().adjusted(-before[k].width, -before[k].width, before[k].width, before[k].width));
    return changed;
}

} // namespace

QPointF Canvas::insideOf(QVector<QPolygonF> const& outlines)
{
    Fill probe;
    probe.outlines = outlines;
    QRectF box;
    for (QPolygonF const& outline : outlines)
        box = box.united(outline.boundingRect());
    if (box.isEmpty())
        return QPointF();
    if (probe.contains(box.center()))
        return box.center();
    // A point well inside: the one of a grid over it furthest from the edge (as far as the grid can tell).
    QPointF best;
    double deepest = -1.0;
    for (int i = 1; i < 16; ++i)
        for (int k = 1; k < 16; ++k) {
            QPointF const p(box.left() + box.width() * i / 16.0, box.top() + box.height() * k / 16.0);
            if (!probe.contains(p))
                continue;
            double edge = 1e300;
            for (QPolygonF const& outline : outlines)
                for (int e = 0; e < outline.size(); ++e)
                    edge = std::min(edge, QLineF(p, outline[e]).length());
            if (edge > deepest) {
                deepest = edge;
                best = p;
            }
        }
    return best;
}

Drawing Canvas::followLines(Drawing whole, Drawing const* before) const
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Fill>& fills = whole.fillsOf(sheet);
        bool any = false;
        for (Fill const& fill : std::as_const(fills))
            any = any || fill.follows;
        if (!any)
            continue;
        QVector<Stroke> const lines = boundsOf(whole, sheet);
        QRectF const changed = before ? changedBetween(lines, boundsOf(*before, sheet)) : QRectF();
        if (before && changed.isNull())
            continue;       // the lines round the paint are as they were
        Drawing bounded;
        bounded.strokes = lines;
        QRectF const area = FillRegion::paintArea(bounded);
        double const room = m_gapSize + 4.0;
        for (Fill& fill : fills) {
            if (!fill.follows || !fill.contours.isEmpty())
                continue;
            QRectF const was = fill.bounds();
            if (before && !was.adjusted(-room, -room, room, room).intersects(changed))
                continue;       // nothing near it changed
            // Found again from where it was painted (while that is still in it), or from well inside it.
            QVector<QPointF> tries;
            if (fill.contains(fill.seed))
                tries.push_back(fill.seed);
            if (QPointF const inside = insideOf(fill.outlines); !inside.isNull())
                tries.push_back(inside);
            for (QPointF const& from : std::as_const(tries)) {
                QVector<QPolygonF> outlines;
                if (FillRegion::compute(bounded, area, from, m_gapSize, outlines) != FillRegion::Ok || outlines.isEmpty())
                    continue;
                fill.outlines = outlines;
                if (!fill.contains(fill.seed))
                    fill.seed = from;
                break;
            }
        }
    }
    return whole;
}

bool Canvas::keepJoined(Stroke const& line)
{
    if ((m_tool != Line && m_tool != Polyline) || (m_tool == Polyline && m_polyBehind) || bitmapNow() || line.closed || line.points.size() < 2)
        return false;
    Drawing const* const d = currentDrawing();
    if (!d)
        return false;
    Drawing art = *d;
    Stroke mine = line;
    int next = std::max(art.nextJoint(), realDrawing() ? realDrawing()->nextJoint() : 1);
    bool joined = false;
    for (int end : {0, int(mine.points.size()) - 1}) {
        StrokePoint& here = mine.points[end];
        for (Stroke& other : art.strokes) {
            if (other.closed || other.points.isEmpty())
                continue;
            for (int k : {0, int(other.points.size()) - 1}) {
                StrokePoint& there = other.points[k];
                if (there.pos != here.pos)
                    continue;
                if (there.joint == 0 && here.joint == 0)
                    there.joint = here.joint = next++;
                else if (here.joint == 0)
                    here.joint = there.joint;
                else if (there.joint == 0)
                    there.joint = here.joint;
                joined = true;
            }
        }
    }
    if (!joined)
        return false;
    mine.order = Drawing::topOrder(art.fills, art.strokes);
    art.strokes.push_back(mine);
    commitDrawing(art, tr("Draw Stroke"));
    return true;
}
