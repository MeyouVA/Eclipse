// Eclipse - the Camera view: tidying what is drawn - smooth, reduce points, flatten, group, colour art from lines.
#include "ui/canvas/Canvas.h"
#include <QLineF>
#include <algorithm>

#include <QInputDialog>
#include <QPainterPath>
#include <QSettings>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"
#include "ui/window/MainWindowPrivate.h"

void Canvas::onActionSmoothFast()
{
    // Each point a quarter of the way towards the middle of its neighbours, instead of half.
    m_smoothWeight = 0.25;
    onActionSmooth();
    m_smoothWeight = 0.5;
}

void Canvas::onActionSmooth()
{
    double const w = m_smoothWeight, keep = 1.0 - w, side = w / 2.0;
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || (m_selStrokes.isEmpty() && m_selFills.isEmpty())) {
        emit message(tr("Select lines or painted areas with the Select tool to smooth them."));
        return;
    }
    Drawing edited = *d;
    bool changed = false;
    // Painted areas: the points of their edges go half way towards the middle of their neighbours too.
    for (int i : std::as_const(m_selFills)) {
        Fill& fill = edited.fills[i];
        if (!fill.text.isEmpty())
            continue;       // words keep the shapes of their letters
        if (!fill.contours.isEmpty() && fill.contours.size() == fill.outlines.size()) {
            bool moved = false;
            for (Stroke& contour : fill.contours) {
                QVector<StrokePoint> const before = contour.points;
                int const n = int(before.size());
                for (int k = 0; n >= 4 && k < n; ++k) {
                    if (before[k].corner)
                        continue;
                    contour.points[k].pos = before[k].pos * keep + (before[(k + n - 1) % n].pos + before[(k + 1) % n].pos) * side;
                    moved = moved || contour.points[k].pos != before[k].pos;
                }
            }
            if (moved) {
                fill.rebuild();
                changed = true;
            }
            continue;
        }
        for (QPolygonF& outline : fill.outlines) {
            QPolygonF const before = outline;
            // (An outline that ends where it began has that point twice: it is one point.)
            int const n = int(before.size()) - (before.size() > 1 && before.first() == before.last() ? 1 : 0);
            if (n < 4)
                continue;
            for (int k = 0; k < n; ++k) {
                outline[k] = before[k] * keep + (before[(k + n - 1) % n] + before[(k + 1) % n]) * side;
                changed = changed || outline[k] != before[k];
            }
            if (n < before.size())
                outline[n] = outline[0];
        }
    }
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
            points[k].pos = before[k].pos * keep + (a.pos + b.pos) * side;
            points[k].pressure = before[k].pressure * float(keep) + (a.pressure + b.pressure) * float(side);
            changed = changed || points[k].pos != before[k].pos;
            // A point with handles: they are put in one line, so the curve passes through it without a kink.
            if (before[k].handles) {
                QPointF const across = before[k].out - before[k].in;
                double const length = std::hypot(across.x(), across.y());
                if (length > 1e-9) {
                    QPointF const way = across / length;
                    double const in = std::hypot(before[k].in.x(), before[k].in.y()), out = std::hypot(before[k].out.x(), before[k].out.y());
                    points[k].in = -way * in;
                    points[k].out = way * out;
                    changed = changed || points[k].in != before[k].in || points[k].out != before[k].out;
                }
            }
        }
    }
    if (changed)
        commitDrawing(edited, w < 0.5 ? tr("Smooth Fast") : tr("Smooth"));
    update();
}

void Canvas::scaleSelectionThickness(double factor, double set, QString const& text)
{
    // The lines selected (or the line in the Contour Editor) made thicker, thinner, or so thick.
    Drawing const* const d = currentDrawing();
    QVector<int> lines = selecting() ? m_selStrokes : QVector<int>();
    if (m_tool == ContourEditor && m_ceStroke >= 0)
        lines = {m_ceStroke};
    if (!d || lines.isEmpty()) {
        emit message(tr("Select lines first (the Select tool, or one in the Contour Editor)."));
        return;
    }
    Drawing edited = *d;
    for (int i : std::as_const(lines))
        if (i >= 0 && i < edited.strokes.size())
            edited.strokes[i].width = float(std::clamp(set > 0.0 ? set : edited.strokes[i].width * factor, 0.05, 2000.0));
    commitDrawing(edited, text);
    update();
}

void Canvas::onActionPumpThickness()
{
    scaleSelectionThickness(1.25, 0.0, tr("Pump Selection Thickness"));
}

void Canvas::onActionDeflateThickness()
{
    scaleSelectionThickness(0.8, 0.0, tr("Deflate Selection Thickness"));
}

void Canvas::onActionSetThickness()
{
    Drawing const* const d = currentDrawing();
    int const first = m_tool == ContourEditor && m_ceStroke >= 0 ? m_ceStroke : (!m_selStrokes.isEmpty() ? m_selStrokes.first() : -1);
    if (!d || first < 0 || first >= d->strokes.size()) {
        emit message(tr("Select lines first (the Select tool, or one in the Contour Editor)."));
        return;
    }
    bool ok = false;
    double const width = QInputDialog::getDouble(this, tr("Set Selection Thickness"), tr("Thickness:"), d->strokes[first].width, 0.05, 2000.0, 2, &ok);
    if (ok)
        scaleSelectionThickness(1.0, width, tr("Set Selection Thickness"));
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

// How far the line `a` strays from the line `b` at most, measured along `a` as it is drawn.
static double strayFrom(Stroke const& a, Stroke const& b)
{
    QVector<StrokePoint> const from = a.path(), to = b.path();
    if (to.isEmpty())
        return 1e9;
    double worst = 0.0;
    for (StrokePoint const& p : from) {
        double nearest = QLineF(p.pos, to[0].pos).length();
        for (int k = 1; k < to.size(); ++k) {
            QLineF const piece(to[k - 1].pos, to[k].pos);
            double const len2 = piece.length() * piece.length();
            double const t = len2 < 1e-12 ? 0.0 : std::clamp(QPointF::dotProduct(p.pos - piece.p1(), piece.p2() - piece.p1()) / len2, 0.0, 1.0);
            nearest = std::min(nearest, QLineF(piece.pointAt(t), p.pos).length());
        }
        worst = std::max(worst, nearest);
    }
    return worst;
}

// A curve with handles loses a point only where the curve, its neighbours' handles stretched over the gap, stays
// where it was (within `tolerance` both ways); a corner stays.  How many points it lost.
static int reduceCurve(Stroke& stroke, double tolerance)
{
    Stroke const original = stroke;
    int removed = 0;
    for (int k = 0; k < stroke.points.size();) {
        int const n = int(stroke.points.size());
        if (n <= (stroke.closed ? 3 : 2) || stroke.points[k].corner || (!stroke.closed && (k == 0 || k == n - 1))) {
            ++k;
            continue;
        }
        int const p = (k + n - 1) % n, q = (k + 1) % n;
        // The neighbours' handles stretched over the gap: by how much is tried, and the best kept.
        double const gap = QLineF(stroke.points[p].pos, stroke.points[q].pos).length();
        double const outBy = gap / std::max(QLineF(stroke.points[p].pos, stroke.points[k].pos).length(), 1e-9);
        double const inBy = gap / std::max(QLineF(stroke.points[k].pos, stroke.points[q].pos).length(), 1e-9);
        Stroke best;
        double bestStray = 1e18;
        for (double const f : {0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.45, 1.6}) {
            Stroke trial = stroke;
            if (trial.points[p].handles)
                trial.points[p].out *= std::min(3.0, outBy * f);
            if (trial.points[q].handles)
                trial.points[q].in *= std::min(3.0, inBy * f);
            trial.points.removeAt(k);
            double const stray = std::max(strayFrom(original, trial), strayFrom(trial, original));
            if (stray < bestStray) {
                bestStray = stray;
                best = trial;
            }
        }
        if (bestStray <= tolerance) {
            stroke = best;
            ++removed;
        } else {
            ++k;
        }
    }
    return removed;
}

// ---- Clean Up ---------------------------------------------------------------------------------

int Canvas::removeDirt(double size)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return 0;
    // Within what is selected, when something is; otherwise the whole of the drawing.
    bool const some = selecting() && hasSelection();
    auto small = [size](QRectF const& box) { return std::max(box.width(), box.height()) <= size; };
    Drawing edited = *d;
    edited.strokes.clear();
    edited.fills.clear();
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!((!some || m_selStrokes.contains(i)) && small(d->strokes[i].bounds())))
            edited.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!((!some || m_selFills.contains(i)) && small(d->fills[i].bounds())))
            edited.fills.push_back(d->fills[i]);
    int const gone = int(d->strokes.size() - edited.strokes.size() + d->fills.size() - edited.fills.size());
    if (gone > 0) {
        clearSelection();
        commitDrawing(edited, tr("Remove Dirt"));
    }
    return gone;
}

int Canvas::removeHair(double length)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return 0;
    bool const some = selecting() && hasSelection();
    Drawing edited = *d;
    edited.strokes.clear();
    for (int i = 0; i < d->strokes.size(); ++i) {
        Stroke const& stroke = d->strokes[i];
        double along = 0.0;
        QVector<StrokePoint> const path = stroke.path();
        for (int k = 1; k < path.size(); ++k)
            along += QLineF(path[k - 1].pos, path[k].pos).length();
        if (!((!some || m_selStrokes.contains(i)) && !stroke.closed && along < length))
            edited.strokes.push_back(stroke);
    }
    int const gone = int(d->strokes.size() - edited.strokes.size());
    if (gone > 0) {
        clearSelection();
        commitDrawing(edited, tr("Remove Hair"));
    }
    return gone;
}

void Canvas::onActionRemoveDirt()
{
    if (!currentDrawing()) {
        emit message(tr("There is no drawing on this frame to clean up."));
        return;
    }
    bool ok = false;
    int const size = QInputDialog::getInt(this, tr("Remove Dirt"), tr("Take out specks no bigger than (pixels):"),
                                          settings().value(QStringLiteral("tools/dirtSize"), 6).toInt(), 1, 500, 1, &ok);
    if (!ok)
        return;
    settings().setValue(QStringLiteral("tools/dirtSize"), size);
    emit message(tr("%n speck(s) taken out.", nullptr, removeDirt(size)));
}

void Canvas::onActionRemoveHair()
{
    if (!currentDrawing()) {
        emit message(tr("There is no drawing on this frame to clean up."));
        return;
    }
    bool ok = false;
    int const length = QInputDialog::getInt(this, tr("Remove Hair"), tr("Take out lines shorter than (pixels):"),
                                            settings().value(QStringLiteral("tools/hairLength"), 20).toInt(), 1, 1000, 1, &ok);
    if (!ok)
        return;
    settings().setValue(QStringLiteral("tools/hairLength"), length);
    emit message(tr("%n short line(s) taken out.", nullptr, removeHair(length)));
}

void Canvas::onActionRemoveArtInside()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to take out with the Select tool."));
        return;
    }
    Drawing edited = *d;
    edited.strokes.clear();
    edited.fills.clear();
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            edited.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            edited.fills.push_back(d->fills[i]);
    clearSelection();
    commitDrawing(edited, tr("Remove Art Inside Selection"));
}

void Canvas::onActionRemoveArtOutside()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to keep with the Select tool."));
        return;
    }
    Drawing edited = *d;
    edited.strokes.clear();
    edited.fills.clear();
    for (int i = 0; i < d->strokes.size(); ++i)
        if (m_selStrokes.contains(i))
            edited.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (m_selFills.contains(i))
            edited.fills.push_back(d->fills[i]);
    if (edited.strokes.size() == d->strokes.size() && edited.fills.size() == d->fills.size())
        return;     // there is nothing outside it
    int const lines = int(edited.strokes.size()), paints = int(edited.fills.size());
    clearSelection();
    commitDrawing(edited, tr("Remove Art Outside Selection"));
    // What was kept is still what is selected: it is all there is now.
    for (int i = 0; i < lines; ++i)
        m_selStrokes.push_back(i);
    for (int i = 0; i < paints; ++i)
        m_selFills.push_back(i);
    emit selectionChanged();
    update();
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
        if (std::any_of(before.begin(), before.end(), [](StrokePoint const& point) { return point.handles; })) {
            removed += reduceCurve(stroke, 2.0);     // (two curves drawn out never lie closer than a pixel or so)
            continue;
        }
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
    if (!selecting() || !d || (m_selFills.size() < 2 && m_selStrokes.size() < 2)) {
        emit message(tr("Select two or more painted areas of one colour, or lines that meet end to end, with the Select tool to flatten them."));
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
    // Lines: two of the same colour and thickness whose ends meet become one line, and so on along
    // the run; a run that comes back to where it began is closed.
    QVector<int> goneLines;
    {
        double const near = std::max(1.0 / std::max(m_zoom, 1e-6), 0.5);
        auto alike = [](Stroke const& a, Stroke const& b) {
            return a.swatch == b.swatch && a.color == b.color && std::abs(a.width - b.width) < 1e-3f && a.curve == b.curve && a.join == b.join;
        };
        auto turned = [](QVector<StrokePoint> points) {
            std::reverse(points.begin(), points.end());
            for (StrokePoint& point : points)
                std::swap(point.in, point.out);
            return points;
        };
        auto meet = [&](StrokePoint const& a, StrokePoint const& b) { return QLineF(a.pos, b.pos).length() <= near; };
        bool joinedAny = true;
        while (joinedAny) {
            joinedAny = false;
            for (int x = 0; x < m_selStrokes.size() && !joinedAny; ++x) {
                Stroke& a = edited.strokes[m_selStrokes[x]];
                if (a.closed || a.points.size() < 2 || goneLines.contains(m_selStrokes[x]))
                    continue;
                for (int y = 0; y < m_selStrokes.size() && !joinedAny; ++y) {
                    if (y == x || goneLines.contains(m_selStrokes[y]))
                        continue;
                    Stroke const& b = edited.strokes[m_selStrokes[y]];
                    if (b.closed || b.points.size() < 2 || !alike(a, b))
                        continue;
                    QVector<StrokePoint> mine = a.points, theirs = b.points;
                    if (meet(mine.last(), theirs.first())) {
                        // as they are
                    } else if (meet(mine.last(), theirs.last())) {
                        theirs = turned(theirs);
                    } else if (meet(mine.first(), theirs.first())) {
                        mine = turned(mine);
                    } else if (meet(mine.first(), theirs.last())) {
                        mine = turned(mine);
                        theirs = turned(theirs);
                    } else {
                        continue;
                    }
                    // Where they meet is one point: it arrives as the first line did and leaves as the second does.
                    mine.last().out = theirs.first().out;
                    mine.last().handles = mine.last().handles || theirs.first().handles;
                    mine.last().corner = true;
                    theirs.removeFirst();
                    a.points = mine + theirs;
                    goneLines.push_back(m_selStrokes[y]);
                    joinedAny = true;
                }
            }
        }
        for (int index : std::as_const(m_selStrokes)) {
            Stroke& line = edited.strokes[index];
            if (goneLines.contains(index) || line.closed || line.points.size() < 4 || !meet(line.points.first(), line.points.last()))
                continue;
            if (goneLines.isEmpty())
                continue;       // only lines that were joined here are closed: one drawn that way is left as it is
            line.points.first().in = line.points.last().in;
            line.points.removeLast();
            line.closed = true;
        }
    }
    if (gone.isEmpty() && goneLines.isEmpty()) {
        emit message(tr("There is nothing to flatten: the areas selected are of different colours, and no lines of one colour and thickness meet end to end."));
        return;
    }
    std::sort(gone.begin(), gone.end());
    for (int i = int(gone.size()) - 1; i >= 0; --i)
        edited.fills.removeAt(gone[i]);
    std::sort(goneLines.begin(), goneLines.end());
    for (int i = int(goneLines.size()) - 1; i >= 0; --i)
        edited.strokes.removeAt(goneLines[i]);
    clearSelection();
    commitDrawing(edited, tr("Flatten"));
}

float Canvas::stencilThickness(int stencil, double along)
{
    // Thinning to a point over a quarter of the line, smoothly.
    auto taper = [](double x) {
        x = std::clamp(x / 0.25, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    };
    double const t = std::clamp(along, 0.0, 1.0);
    double share = 1.0;
    switch (stencil) {
    case StencilTapered: share = taper(t) * taper(1.0 - t); break;
    case StencilTaperedStart: share = taper(t); break;
    case StencilTaperedEnd: share = taper(1.0 - t); break;
    case StencilMediumTapered: share = 0.4 + 0.6 * taper(t) * taper(1.0 - t); break;
    case StencilPointy: share = 1.0 - std::abs(2.0 * t - 1.0); break;
    case StencilFatEnds: share = 0.35 + 0.65 * (2.0 * t - 1.0) * (2.0 * t - 1.0); break;
    default: break;
    }
    return float(std::max(0.05, share));
}

// How far along a line each of its points lies, from the first (straight from point to point).
static QVector<double> distancesAlong(QVector<StrokePoint> const& points)
{
    QVector<double> along(points.size(), 0.0);
    for (int i = 1; i < points.size(); ++i)
        along[i] = along[i - 1] + QLineF(points[i - 1].pos, points[i].pos).length();
    return along;
}

void Canvas::applyStencil(Stroke& stroke, int stencil)
{
    QVector<double> const along = distancesAlong(stroke.points);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (total <= 0.0)
        return;
    for (int i = 0; i < stroke.points.size(); ++i)
        stroke.points[i].pressure *= stencilThickness(stencil, along[i] / total);
}

void Canvas::onActionReversePencilThickness()
{
    Drawing const* const d = currentDrawing();
    QVector<int> lines = selecting() ? m_selStrokes : QVector<int>();
    if (m_tool == ContourEditor && m_ceStroke >= 0)
        lines = {m_ceStroke};
    if (!d || lines.isEmpty()) {
        emit message(tr("Select lines first (the Select tool, or one in the Contour Editor)."));
        return;
    }
    Drawing edited = *d;
    bool changed = false;
    for (int index : std::as_const(lines)) {
        if (index < 0 || index >= edited.strokes.size())
            continue;
        Stroke& line = edited.strokes[index];
        QVector<StrokePoint> const before = line.points;
        QVector<double> const along = distancesAlong(before);
        double const total = along.isEmpty() ? 0.0 : along.last();
        if (total <= 0.0)
            continue;
        // Each point takes the thickness the line had as far from the other end.
        for (int i = 0; i < before.size(); ++i) {
            double const at = total - along[i];
            int k = 0;
            while (k + 1 < along.size() - 1 && along[k + 1] < at)
                ++k;
            double const span = along[k + 1] - along[k];
            double const t = span > 0.0 ? std::clamp((at - along[k]) / span, 0.0, 1.0) : 0.0;
            line.points[i].pressure = float(before[k].pressure + (before[k + 1].pressure - before[k].pressure) * t);
        }
        for (int i = 0; i < before.size(); ++i)
            changed = changed || line.points[i].pressure != before[i].pressure;
    }
    if (changed)
        commitDrawing(edited, tr("Reverse Pencil Thickness"));
    update();
}

// A line turned end for end: its points the other way round, and the handles of each with them.
static Stroke reversedLine(Stroke line)
{
    std::reverse(line.points.begin(), line.points.end());
    for (StrokePoint& point : line.points)
        std::swap(point.in, point.out);
    std::swap(line.capStart, line.capEnd);
    return line;
}

void Canvas::mergeSelectedLines(bool bridge, QString const& text)
{
    Drawing const* const d = currentDrawing();
    QVector<int> lines;
    if (d && selecting())
        for (int index : std::as_const(m_selStrokes))
            if (index >= 0 && index < d->strokes.size() && !d->strokes[index].closed && d->strokes[index].points.size() >= 2 && !lines.contains(index))
                lines.push_back(index);
    std::sort(lines.begin(), lines.end());
    if (!d || lines.size() < 2) {
        emit message(tr("Select two pencil lines or more first (the Select tool)."));
        return;
    }
    QVector<Stroke> pool;
    bool anyCurve = false;
    for (int index : std::as_const(lines)) {
        pool.push_back(d->strokes[index]);
        anyCurve = anyCurve || d->strokes[index].curve;
    }
    // A line of straight pieces joined to a curved one: its pieces become straight runs of a curve.
    if (anyCurve)
        for (Stroke& line : pool)
            if (!line.curve) {
                line.curve = true;
                for (StrokePoint& point : line.points) {
                    point.handles = true;
                    point.corner = true;
                    point.in = point.out = QPointF();
                }
            }
    // Join: only ends this near one another (the width of the lines, and a few pixels of the screen) are joined.
    double reach = 0.0;
    for (Stroke const& line : std::as_const(pool))
        reach = std::max(reach, double(line.width) / 2.0);
    reach += 6.0 / std::max(m_zoom, 1e-6);
    Stroke chain = pool.takeFirst();
    QVector<int> used{lines.first()};
    QVector<int> left = lines.mid(1);
    while (!pool.isEmpty()) {
        int best = -1, how = 0;
        double nearest = 0.0;
        for (int k = 0; k < pool.size(); ++k) {
            QPointF const ends[4][2] = {{chain.points.last().pos, pool[k].points.first().pos}, {chain.points.last().pos, pool[k].points.last().pos},
                                        {chain.points.first().pos, pool[k].points.last().pos}, {chain.points.first().pos, pool[k].points.first().pos}};
            for (int way = 0; way < 4; ++way) {
                double const gap = QLineF(ends[way][0], ends[way][1]).length();
                if (best < 0 || gap < nearest) {
                    best = k;
                    how = way;
                    nearest = gap;
                }
            }
        }
        if (!bridge && nearest > reach)
            break;
        Stroke next = pool.takeAt(best);
        used.push_back(left.takeAt(best));
        // 0: on after the end; 1: the same, turned round; 2: on before the start; 3: before the start, turned round.
        if (how == 1 || how == 3)
            next = reversedLine(next);
        Stroke& first = how <= 1 ? chain : next;
        Stroke const& second = how <= 1 ? next : chain;
        Stroke joined = first;
        int from = 0;
        if (QLineF(first.points.last().pos, second.points.first().pos).length() < 1e-6) {
            joined.points.last().out = second.points.first().out;
            from = 1;
        }
        for (int i = from; i < second.points.size(); ++i)
            joined.points.push_back(second.points[i]);
        joined.capEnd = second.capEnd;
        chain = joined;
    }
    if (used.size() < 2) {
        emit message(tr("The ends of the lines selected do not meet: Merge Pencil Lines joins them across the gap."));
        return;
    }
    Drawing edited = *d;
    std::sort(used.begin(), used.end());
    int const at = used.first();
    for (int i = int(used.size()) - 1; i >= 0; --i)
        edited.strokes.removeAt(used[i]);
    edited.strokes.insert(at, chain);
    commitDrawing(edited, text);
    setSelection({at}, {});
    update();
}

void Canvas::onActionMergePencilLines()
{
    mergeSelectedLines(true, tr("Merge Pencil Lines"));
}

void Canvas::onActionJoinPencilLines()
{
    mergeSelectedLines(false, tr("Join Pencil Lines"));
}

void Canvas::onActionSplitPencilLine()
{
    Drawing const* const d = currentDrawing();
    if (!d || m_tool != ContourEditor || m_ceStroke < 0 || m_ceFill >= 0 || m_ceStroke >= d->strokes.size() || m_cePoint < 0
        || m_cePoint >= d->strokes[m_ceStroke].points.size()) {
        emit message(tr("Pick a point of a pencil line with the Contour Editor first: the line is cut there."));
        return;
    }
    Drawing edited = *d;
    Stroke const line = d->strokes[m_ceStroke];
    int const point = m_cePoint;
    if (line.closed) {
        // A closed line opens there: it starts and ends at that point.
        Stroke opened = line;
        opened.closed = false;
        opened.points.clear();
        for (int i = 0; i <= line.points.size(); ++i)
            opened.points.push_back(line.points[(point + i) % line.points.size()]);
        edited.strokes[m_ceStroke] = opened;
    } else {
        if (point == 0 || point == line.points.size() - 1) {
            emit message(tr("That is an end of the line already: pick a point along it."));
            return;
        }
        Stroke a = line, b = line;
        a.points = line.points.mid(0, point + 1);
        b.points = line.points.mid(point);
        a.capEnd = line.capStart;
        b.capStart = line.capEnd;
        edited.strokes[m_ceStroke] = a;
        edited.strokes.insert(m_ceStroke + 1, b);
    }
    commitDrawing(edited, tr("Split Pencil Line"));
    update();
}
