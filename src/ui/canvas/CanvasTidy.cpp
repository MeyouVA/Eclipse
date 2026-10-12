// Eclipse - the Camera view: tidying what is drawn - smooth, reduce points, flatten, group, colour art from lines.
#include "ui/canvas/Canvas.h"

#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

void Canvas::onActionSmooth()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select lines with the Select tool to smooth them."));
        return;
    }
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selStrokes)) {
        QVector<StrokePoint>& points = edited.strokes[i].points;
        QVector<StrokePoint> const before = points;
        int const n = int(before.size());
        if (n < 3)
            continue;
        bool const closed = edited.strokes[i].closed;
        // Each point goes half way towards the middle of its neighbours; corners and the
        // ends of an open line stay where they are.
        for (int k = 0; k < n; ++k) {
            if (before[k].corner || (!closed && (k == 0 || k == n - 1)))
                continue;
            StrokePoint const& a = before[(k + n - 1) % n];
            StrokePoint const& b = before[(k + 1) % n];
            points[k].pos = before[k].pos * 0.5 + (a.pos + b.pos) * 0.25;
            points[k].pressure = before[k].pressure * 0.5f + (a.pressure + b.pressure) * 0.25f;
            changed = changed || points[k].pos != before[k].pos;
        }
    }
    if (changed)
        commitDrawing(edited, tr("Smooth"));
    update();
}

// The points of a line that it needs: the others lie within `tolerance` of the line through
// the ones kept (Douglas and Peucker's way).
static void keepNeeded(QVector<StrokePoint> const& points, int first, int last, double tolerance, QVector<bool>& keep)
{
    if (last <= first + 1)
        return;
    QLineF const chord(points[first].pos, points[last].pos);
    double worst = -1.0;
    int at = -1;
    for (int k = first + 1; k < last; ++k) {
        QPointF const p = points[k].pos;
        double distance;
        if (chord.length() < 1e-9) {
            distance = QLineF(chord.p1(), p).length();
        } else {
            double const t = std::clamp(QPointF::dotProduct(p - chord.p1(), chord.p2() - chord.p1()) / (chord.length() * chord.length()), 0.0, 1.0);
            distance = QLineF(chord.pointAt(t), p).length();
        }
        // A point where the line is much thicker or thinner than its neighbours is needed too.
        distance += std::abs(points[k].pressure - (points[first].pressure + points[last].pressure) / 2.0) * tolerance * 4.0;
        if (points[k].corner)
            distance = tolerance * 1e6;
        if (distance > worst) {
            worst = distance;
            at = k;
        }
    }
    if (worst <= tolerance)
        return;
    keep[at] = true;
    keepNeeded(points, first, at, tolerance, keep);
    keepNeeded(points, at, last, tolerance, keep);
}

void Canvas::onActionReducePoints()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select lines with the Select tool to take out the points they do not need."));
        return;
    }
    Drawing edited = *d;
    int removed = 0;
    for (int i : std::as_const(m_selStrokes)) {
        Stroke& stroke = edited.strokes[i];
        QVector<StrokePoint> const before = stroke.points;
        int const n = int(before.size());
        if (n < 3)
            continue;
        // Half a pixel of the drawing for a line of straight pieces; a curve bends between
        // its points, so it can lose more.
        QVector<bool> keep(n, false);
        keep[0] = keep[n - 1] = true;
        keepNeeded(before, 0, n - 1, stroke.curve ? 1.5 : 0.5, keep);
        QVector<StrokePoint> kept;
        for (int k = 0; k < n; ++k)
            if (keep[k])
                kept.push_back(before[k]);
        if (kept.size() < 2 || (stroke.closed && kept.size() < 3))
            continue;
        removed += n - int(kept.size());
        stroke.points = kept;
    }
    if (removed > 0)
        commitDrawing(edited, tr("Reduce Points"));
    emit message(removed > 0 ? tr("%n point(s) taken out.", nullptr, removed) : tr("Every point of the selected lines is needed."));
    update();
}

void Canvas::onActionCreateColourArt()
{
    Drawing const* const real = realDrawing();
    if (!real || (real->strokes.isEmpty() && real->fills.isEmpty())) {
        emit message(tr("There is no line art on this frame to make colour art from."));
        return;
    }
    // Each line of the line art, and each contour of its brush shapes, as a line with no
    // thickness in the colour art - unless one is there on the same points already.
    QVector<Stroke> lines;
    for (Stroke const& stroke : real->strokes)
        lines.push_back(stroke);
    for (Fill const& fill : real->fills)
        for (Stroke const& contour : fill.contours)
            lines.push_back(contour);
    Drawing whole = *real;
    int made = 0;
    for (Stroke line : std::as_const(lines)) {
        line.width = 0.0f;
        line.group = 0;
        bool there = false;
        for (Stroke const& other : std::as_const(whole.colourStrokes)) {
            if (other.width > 0.0f || other.points.size() != line.points.size())
                continue;
            bool same = true;
            for (int i = 0; same && i < line.points.size(); ++i)
                same = QLineF(other.points[i].pos, line.points[i].pos).length() < 1e-6;
            there = there || same;
        }
        if (!there) {
            whole.colourStrokes.push_back(line);
            ++made;
        }
    }
    if (made == 0) {
        emit message(tr("The colour art has these lines already."));
        return;
    }
    clearSelection();
    m_committing = true;
    m_document->setDrawing(m_layer, m_frame, whole, tr("Create Colour Art from Line Art"));
    m_committing = false;
    emit message(tr("%n line(s) made in the colour art.", nullptr, made));
}

void Canvas::onActionGroupArtwork()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.size() + m_selFills.size() < 2) {
        emit message(tr("Select two or more things with the Select tool to group them."));
        return;
    }
    // A number no group of this sheet has yet.
    int group = 1;
    for (Stroke const& stroke : d->strokes)
        group = std::max(group, stroke.group + 1);
    for (Fill const& fill : d->fills)
        group = std::max(group, fill.group + 1);
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes))
        edited.strokes[i].group = group;
    for (int i : std::as_const(m_selFills))
        edited.fills[i].group = group;
    commitDrawing(edited, tr("Group"));
}

void Canvas::onActionUngroupArtwork()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection())
        return;
    Drawing edited = *d;
    bool any = false;
    for (int i : std::as_const(m_selStrokes)) {
        any = any || edited.strokes[i].group != 0;
        edited.strokes[i].group = 0;
    }
    for (int i : std::as_const(m_selFills)) {
        any = any || edited.fills[i].group != 0;
        edited.fills[i].group = 0;
    }
    if (any)
        commitDrawing(edited, tr("Ungroup"));
    else
        emit message(tr("Nothing selected is in a group."));
}

void Canvas::onActionFlatten()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selFills.size() < 2) {
        emit message(tr("Select two or more painted areas of one colour with the Select tool to flatten them."));
        return;
    }
    // Each colour among the selected areas: its areas become one, where the last of them was.
    Drawing edited = *d;
    QVector<int> gone, kept;
    for (int a = 0; a < m_selFills.size(); ++a) {
        int const first = m_selFills[a];
        if (gone.contains(first) || kept.contains(first))
            continue;
        QPainterPath joined;
        joined.setFillRule(Qt::OddEvenFill);
        QVector<int> same;
        bool shapes = false;    // whether any of them is a contour shape: the result is one then
        for (int b = a; b < m_selFills.size(); ++b) {
            Fill const& fill = d->fills[m_selFills[b]];
            if (fill.swatch != d->fills[first].swatch || fill.color != d->fills[first].color)
                continue;
            shapes = shapes || !fill.contours.isEmpty();
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& outline : fill.outlines)
                path.addPolygon(outline);
            path.closeSubpath();
            joined = same.isEmpty() ? path : joined.united(path);
            same.push_back(m_selFills[b]);
        }
        if (same.size() < 2)
            continue;
        Fill& into = edited.fills[same.last()];
        into.outlines.clear();
        into.contours.clear();
        for (QPolygonF const& outline : joined.simplified().toSubpathPolygons())
            if (outline.size() >= 3)
                into.outlines.push_back(outline);
        if (shapes) {
            // Contour shapes stay contour shapes: the joined edge gets points and handles.
            for (QPolygonF const& outline : std::as_const(into.outlines))
                into.contours.push_back(Fill::contourOf(outline, std::max(0.25 / m_zoom, 0.05)));
            into.rebuild();
        }
        kept.push_back(same.last());
        same.removeLast();
        gone += same;
    }
    if (gone.isEmpty()) {
        emit message(tr("The selected areas are of different colours: there is nothing to flatten."));
        return;
    }
    std::sort(gone.begin(), gone.end());
    for (int i = int(gone.size()) - 1; i >= 0; --i)
        edited.fills.removeAt(gone[i]);
    clearSelection();
    commitDrawing(edited, tr("Flatten"));
}
