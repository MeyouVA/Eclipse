// Eclipse - the Camera view: tidying what is drawn - smooth, reduce points, flatten, group, colour art from lines.
#include "ui/canvas/Canvas.h"
#include <QSet>
#include <QRandomGenerator>
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
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>

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
        for (int k = 0; k < n; ++k) {
            if (before[k].corner || (!closed && (k == 0 || k == n - 1)))
                continue;
            StrokePoint const& a = before[(k + n - 1) % n];
            StrokePoint const& b = before[(k + 1) % n];
            points[k].pos = before[k].pos * keep + (a.pos + b.pos) * side;
            if (edited.strokes[i].thickness.isEmpty())
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
        // (A thickness of its own stays where it was along the line, whatever points go.)
        QVector<QPair<double, ThicknessPoint>> const thick = stroke.thickness.isEmpty() ? QVector<QPair<double, ThicknessPoint>>() : stroke.thicknessByLength();
        if (std::any_of(before.begin(), before.end(), [](StrokePoint const& point) { return point.handles; })) {
            int const gone = reduceCurve(stroke, 2.0);     // (two curves drawn out never lie closer than a pixel or so)
            removed += gone;
            if (gone > 0 && !thick.isEmpty())
                stroke.setThicknessByLength(thick);
            continue;
        }
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
        if (!thick.isEmpty())
            stroke.setThicknessByLength(thick);
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
        if (!m_quietFlatten)
            emit message(tr("Select two or more painted areas of one colour, or lines that meet or cross, with the Select tool to flatten them."));
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
    QVector<int> goneLines;
    // Where the selected lines cross: a point on each, lying on one another.
    int const crossings = addCrossingPoints(edited.strokes, m_selStrokes);
    {
        double const near = std::max(1.0 / std::max(m_zoom, 1e-6), 0.5);
        auto alike = [](Stroke const& a, Stroke const& b) {
            return a.swatch == b.swatch && a.color == b.color && std::abs(a.width - b.width) < 1e-3f && a.curve == b.curve && a.join == b.join;
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
                    Stroke mine = a, theirs = b;
                    if (meet(mine.points.last(), theirs.points.first())) {
                        // as they are
                    } else if (meet(mine.points.last(), theirs.points.last())) {
                        theirs.reverse();
                    } else if (meet(mine.points.first(), theirs.points.first())) {
                        mine.reverse();
                    } else if (meet(mine.points.first(), theirs.points.last())) {
                        mine.reverse();
                        theirs.reverse();
                    } else {
                        continue;
                    }
                    // Where they meet is one point: it arrives as the first line did and leaves as the second does.
                    int const meeting = int(mine.points.size()) - 1;
                    mine.append(theirs, true);
                    mine.points[meeting].corner = true;
                    a = mine;
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
    if (gone.isEmpty() && goneLines.isEmpty() && crossings == 0) {
        if (!m_quietFlatten)
            emit message(tr("There is nothing to flatten: the areas selected are of different colours, and no lines of one colour and thickness meet or cross."));
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
    case StencilCurvedAbove:
    case StencilCurvedBelow: share = std::pow(std::sin(3.14159265358979323846 * t), 0.7); break;     // a bow, to one side (applyStencil)
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
    for (int i = 0; i < stroke.points.size(); ++i) {
        stroke.points[i].pressure *= stencilThickness(stencil, along[i] / total);
        // Curved Above / Below: all of the thickness on the one side of the line (left as it goes: above a line drawn to the right).
        if (stencil == StencilCurvedAbove || stencil == StencilCurvedBelow)
            stroke.points[i].lean = stencil == StencilCurvedAbove ? 1.0f : -1.0f;
    }
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
        // The thickness along the line turned end for end (the line itself as it is; each side stays its side).
        QVector<ThicknessPoint> const was = line.thickness;
        QVector<float> const pressures = [&] {
            QVector<float> all;
            for (StrokePoint const& p : line.points)
                all.push_back(p.pressure);
            return all;
        }();
        QVector<QPair<double, ThicknessPoint>> byLength = line.thicknessByLength();
        for (auto& entry : byLength)
            entry.first = 1.0 - entry.first;
        std::reverse(byLength.begin(), byLength.end());
        line.setThicknessByLength(byLength);
        // (A line as thick all along is as it was.)
        Stroke plain = line;
        if (plain.uniform() && !plain.leans() && was.isEmpty()) {
            line.thickness.clear();
            for (int i = 0; i < line.points.size(); ++i)
                line.points[i].pressure = pressures[i];
        }
        changed = changed || line.thickness != was;
    }
    if (changed)
        commitDrawing(edited, tr("Reverse Pencil Thickness"));
    update();
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
            makeCurve(line);
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
        joined.append(second, QLineF(first.points.last().pos, second.points.first().pos).length() < 1e-6);
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
        edited.strokes[m_ceStroke] = line.section(point, point + int(line.points.size()));
    } else {
        if (point == 0 || point == line.points.size() - 1) {
            emit message(tr("That is an end of the line already: pick a point along it."));
            return;
        }
        Stroke a = line.section(0, point), b = line.section(point, int(line.points.size()) - 1);
        a.capEnd = line.capStart;
        b.capStart = line.capEnd;
        edited.strokes[m_ceStroke] = a;
        edited.strokes.insert(m_ceStroke + 1, b);
    }
    commitDrawing(edited, tr("Split Pencil Line"));
    update();
}

// ---- how a line drawn with the pen is finished ---------------------------------------------------------------------------

void Canvas::applyTaperAndSpeed(Stroke& stroke, LineOptions const& options, double zoom)
{
    int const n = int(stroke.points.size());
    if (n < 2)
        return;
    QVector<double> const along = distancesAlong(stroke.points);
    double const total = along.last();
    if (total <= 0.0)
        return;
    // Speed: how far the pen went between two samples, on the screen (the samples come at a steady rate), smoothed a little.
    if (options.speed > 0) {
        QVector<double> step(n, 0.0);
        for (int i = 1; i < n; ++i)
            step[i] = (along[i] - along[i - 1]) * zoom;
        step[0] = n > 1 ? step[1] : 0.0;
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            int count = 0;
            for (int k = std::max(0, i - 2); k <= std::min(n - 1, i + 2); ++k) {
                sum += step[k];
                ++count;
            }
            double const fast = std::clamp(sum / count / 30.0, 0.0, 1.0);
            double const thinner = options.speed / 100.0 * 0.75 * (options.speedInvert ? 1.0 - fast : fast);
            stroke.points[i].pressure = float(stroke.points[i].pressure * (1.0 - thinner));
        }
    }
    // Taper: smoothly to a point over that share of the line at its start and at its end.
    auto ramp = [](double x) {
        x = std::clamp(x, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    };
    // Fade Distance: thinning away to nothing so far from the start; past it there is no more line.
    if (options.fade > 0) {
        int keep = n;
        for (int i = 0; i < n; ++i) {
            double const left = 1.0 - along[i] / options.fade;
            if (left <= 0.0) {
                keep = std::max(2, i);
                break;
            }
            stroke.points[i].pressure = float(stroke.points[i].pressure * left);
        }
        stroke.points.resize(keep);
        if (keep < n)
            stroke.points.last().pressure = 0.05f;
    }
    // (Tapered over the line as it is left.)
    double const length = along[stroke.points.size() - 1];
    for (int i = 0; i < stroke.points.size(); ++i) {
        double share = 1.0;
        if (options.startTaper > 0 && length > 0.0)
            share *= ramp(along[i] / (length * options.startTaper / 100.0));
        if (options.endTaper > 0 && length > 0.0)
            share *= ramp((length - along[i]) / (length * options.endTaper / 100.0));
        stroke.points[i].pressure = float(std::max(0.05, stroke.points[i].pressure * share));
    }
}

// Where the piece a-b crosses the piece c-d: how far along a-b (0 to 1), or -1.
static double crossing(QPointF a, QPointF b, QPointF c, QPointF d)
{
    QPointF const r = b - a, s = d - c;
    double const across = r.x() * s.y() - r.y() * s.x();
    if (std::abs(across) < 1e-12)
        return -1.0;
    QPointF const ac = c - a;
    double const t = (ac.x() * s.y() - ac.y() * s.x()) / across, u = (ac.x() * r.y() - ac.y() * r.x()) / across;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0 ? t : -1.0;
}

bool Canvas::trimExtra(Stroke& stroke, Drawing const& art, double reach)
{
    // Every place the new line crosses a line of the drawing, as a distance along it.
    int const n = int(stroke.points.size());
    if (n < 2)
        return false;
    QVector<double> const along = distancesAlong(stroke.points);
    double const total = along.last();
    double first = -1.0, last = -1.0;
    QPointF firstAt, lastAt;
    int firstPiece = -1, lastPiece = -1;
    for (Stroke const& other : art.strokes) {
        QVector<StrokePoint> const run = other.path();
        int const pieces = int(run.size()) - 1 + (other.closed && run.size() > 2 ? 1 : 0);
        for (int k = 0; k < pieces; ++k) {
            QPointF const c = run[k].pos, d = run[(k + 1) % run.size()].pos;
            for (int i = 0; i + 1 < n; ++i) {
                double const t = crossing(stroke.points[i].pos, stroke.points[i + 1].pos, c, d);
                if (t < 0.0)
                    continue;
                double const at = along[i] + (along[i + 1] - along[i]) * t;
                QPointF const where = stroke.points[i].pos + (stroke.points[i + 1].pos - stroke.points[i].pos) * t;
                if (first < 0.0 || at < first) {
                    first = at;
                    firstAt = where;
                    firstPiece = i;
                }
                if (last < 0.0 || at > last) {
                    last = at;
                    lastAt = where;
                    lastPiece = i;
                }
            }
        }
    }
    if (first < 0.0)
        return false;
    // The ends that run only a little past the first and last crossings (and not most of the line) are cut off there.
    bool const cutEnd = total - last > 1e-9 && total - last <= reach && total - last < total * 0.4;
    bool const cutStart = first > 1e-9 && first <= reach && first < total * 0.4 && (!cutEnd || lastPiece >= firstPiece);
    if (!cutEnd && !cutStart)
        return false;
    QVector<StrokePoint> kept;
    int const from = cutStart ? firstPiece + 1 : 0, to = cutEnd ? lastPiece : n - 1;
    if (cutStart)
        kept.push_back({firstAt, stroke.points[firstPiece].pressure});
    for (int i = from; i <= to; ++i)
        kept.push_back(stroke.points[i]);
    if (cutEnd)
        kept.push_back({lastAt, stroke.points[std::min(lastPiece + 1, n - 1)].pressure});
    if (kept.size() < 2)
        return false;
    stroke.points = kept;
    return true;
}

bool Canvas::connectEnds(Stroke& stroke, Drawing const& art, double reach)
{
    // An end near a line of the drawing, but not on it: carried on to the nearest place on that line.
    if (stroke.points.size() < 2)
        return false;
    auto nearest = [&](QPointF p, QPointF& on) {
        double best = 1e300;
        for (Stroke const& other : art.strokes) {
            QVector<StrokePoint> const run = other.path();
            int const pieces = int(run.size()) - 1 + (other.closed && run.size() > 2 ? 1 : 0);
            for (int k = 0; k < pieces; ++k) {
                QPointF const a = run[k].pos, ab = run[(k + 1) % run.size()].pos - a;
                double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
                double const t = len2 > 0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
                QPointF const there = a + ab * t;
                double const distance = QLineF(p, there).length();
                if (distance < best) {
                    best = distance;
                    on = there;
                }
            }
        }
        return best;
    };
    bool changed = false;
    QPointF on;
    double const atStart = nearest(stroke.points.first().pos, on);
    if (atStart > reach * 0.02 && atStart <= reach) {
        stroke.points.prepend({on, stroke.points.first().pressure});
        changed = true;
    }
    double const atEnd = nearest(stroke.points.last().pos, on);
    if (atEnd > reach * 0.02 && atEnd <= reach) {
        stroke.points.push_back({on, stroke.points.last().pressure});
        changed = true;
    }
    return changed;
}

void Canvas::finishDrawnLine(double reach)
{
    Drawing const* const d = currentDrawing();
    if (!d || d->strokes.isEmpty())
        return;
    Drawing edited = *d;
    int const mine = int(edited.strokes.size()) - 1;        // the line just drawn is the last one
    Stroke line = edited.strokes[mine];
    if (line.points.size() < 2 || line.closed || line.width <= 0.0f)
        return;
    bool changed = false;
    // Line Building: begun or ended at an end of a line of its colour - one line with it.
    if (m_lineOptions.building) {
        double const near = std::max(reach * 0.5, double(line.width));
        for (int k = mine - 1; k >= 0 && !changed; --k) {
            Stroke const& other = edited.strokes[k];
            if (other.closed || other.points.size() < 2 || other.width <= 0.0f || other.swatch != line.swatch)
                continue;
            QPointF const oa = other.points.first().pos, ob = other.points.last().pos, na = line.points.first().pos, nb = line.points.last().pos;
            Stroke before, after;
            if (QLineF(ob, na).length() <= near) {
                before = other;
                after = line;
            } else if (QLineF(oa, nb).length() <= near) {
                before = line;
                after = other;
            } else if (QLineF(ob, nb).length() <= near) {
                before = other;
                after = reversedLine(line);
            } else if (QLineF(oa, na).length() <= near) {
                before = reversedLine(line);
                after = other;
            } else {
                continue;
            }
            if (before.curve || after.curve) {
                makeCurve(before);
                makeCurve(after);
            }
            Stroke joined = startedFrom(other, before);
            joined.append(after, false);
            edited.strokes[k] = joined;
            edited.strokes.removeAt(mine);
            line = joined;
            changed = true;
        }
    }
    // Auto Close Gap: ends that nearly meet - an invisible stroke across.
    if (m_lineOptions.closeGap && !line.closed) {
        QPointF const a = line.points.first().pos, b = line.points.last().pos;
        double const gap = QLineF(a, b).length();
        QRectF const box = line.bounds();
        if (gap > 1e-6 && gap <= reach && std::max(box.width(), box.height()) > reach * 2.0) {
            Stroke across;
            across.width = 0.0f;
            across.swatch = line.swatch;
            across.color = line.color;
            across.points = {{b, 1.0f}, {a, 1.0f}};
            across.order = line.order;
            edited.strokes.push_back(across);
            changed = true;
        }
    }
    if (changed)
        commitDrawing(edited, tr("Draw Stroke"));
}

// ---- thickness stencils of your own, the Pencil Retouch tool, Pencil Line Wiggle ---------------------------------------

float Canvas::profileThickness(QVector<float> const& profile, double along)
{
    if (profile.isEmpty())
        return 1.0f;
    if (profile.size() == 1)
        return profile.first();
    double const at = std::clamp(along, 0.0, 1.0) * (profile.size() - 1);
    int const k = std::min(int(at), int(profile.size()) - 2);
    double const t = at - k;
    return float(std::max(0.05, profile[k] + (profile[k + 1] - profile[k]) * t));
}

void Canvas::applyProfile(Stroke& stroke, QVector<float> const& profile)
{
    QVector<double> const along = distancesAlong(stroke.points);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (total <= 0.0)
        return;
    for (int i = 0; i < stroke.points.size(); ++i)
        stroke.points[i].pressure *= profileThickness(profile, along[i] / total);
}

QVector<float> Canvas::selectedLineProfile() const
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty() || m_selStrokes.first() < 0 || m_selStrokes.first() >= d->strokes.size())
        return {};
    // Along the line as drawn (its curve), the thickness at 32 evenly spaced places, as shares of the thickest.
    QVector<StrokePoint> const run = d->strokes[m_selStrokes.first()].path();
    QVector<double> const along = distancesAlong(run);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (run.size() < 2 || total <= 0.0)
        return {};
    QVector<float> profile;
    int k = 0;
    for (int i = 0; i < 32; ++i) {
        double const at = total * i / 31.0;
        while (k + 2 < run.size() && along[k + 1] < at)
            ++k;
        double const span = along[k + 1] - along[k];
        double const t = span > 0.0 ? std::clamp((at - along[k]) / span, 0.0, 1.0) : 0.0;
        profile.push_back(float(run[k].pressure + (run[k + 1].pressure - run[k].pressure) * t));
    }
    float const most = *std::max_element(profile.begin(), profile.end());
    if (most <= 0.0f)
        return {};
    for (float& share : profile)
        share /= most;
    return profile;
}

void Canvas::retouchAt(QPointF p, int mode)
{
    bool const thinner = mode == 1 || mode == 3, opacity = mode >= 2;
    double const reach = std::max(m_brushWidth / 2.0, 4.0 / std::max(m_zoom, 1e-6));
    for (int index = 0; index < m_eraseArt.strokes.size(); ++index) {
        Stroke& line = m_eraseArt.strokes[index];
        if (line.width <= 0.0f || line.points.size() < 2 || !line.hit(p, reach))
            continue;
        // Apply to Single Stroke: the first line rubbed, and that one only.
        if (m_singleNow && m_onlyLine == -1)
            m_onlyLine = index;
        if (m_singleNow && m_onlyLine != index)
            continue;
        if (!opacity) {
            // Thicker or thinner: the thickness points under the pen (one is put there when none is near), most at its middle.
            line.ownThickness();
            bool keyNear = false;
            for (ThicknessPoint const& key : std::as_const(line.thickness))
                keyNear = keyNear || QLineF(line.pointAt(key.at), p).length() <= reach * 0.5;
            if (!keyNear)
                line.thicknessPointAt(line.placeAt(p));
            for (ThicknessPoint& key : line.thickness) {
                double const distance = QLineF(line.pointAt(key.at), p).length();
                if (distance > reach)
                    continue;
                double const fall = 1.0 - distance / reach;
                double const step = 1.0 + 0.15 * fall * (thinner ? -1.0 : 1.0);
                key.left = float(std::clamp(key.left * step, 0.02, 24.0));
                key.right = float(std::clamp(key.right * step, 0.02, 24.0));
                m_inked = true;
            }
            continue;
        }
        bool pointNear = false;
        for (StrokePoint const& point : std::as_const(line.points))
            pointNear = pointNear || QLineF(point.pos, p).length() <= reach * 0.5;
        if (!pointNear)
            line.insertPoint(p);
        for (StrokePoint& point : line.points) {
            double const distance = QLineF(point.pos, p).length();
            if (distance > reach)
                continue;
            double const fall = 1.0 - distance / reach;
            double const step = 1.0 + 0.15 * fall * (thinner ? -1.0 : 1.0);
            if (opacity)
                point.opacity = float(std::clamp(point.opacity * step, 0.02, 1.0));
            else
                point.pressure = float(std::clamp(point.pressure * step, 0.05, 4.0));
            m_inked = true;
        }
    }
    update();
}

void Canvas::wiggleSelectedLines(double amount, double every, quint32 seed)
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty() || amount <= 0.0 || every <= 0.0) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    QRandomGenerator random(seed);
    Drawing edited = *d;
    for (int index : std::as_const(m_selStrokes)) {
        if (index < 0 || index >= edited.strokes.size())
            continue;
        Stroke& line = edited.strokes[index];
        QVector<StrokePoint> const run = line.path();
        QVector<double> const along = distancesAlong(run);
        double const total = along.isEmpty() ? 0.0 : along.last();
        if (run.size() < 2 || total <= 0.0)
            continue;
        // Places every so far along the line, each pushed to one side or the other of it (the ends stay put).
        int const count = std::max(2, int(std::ceil(total / every)));
        QVector<StrokePoint> wiggled;
        int k = 0;
        for (int i = 0; i <= count; ++i) {
            double const at = total * i / count;
            while (k + 2 < run.size() && along[k + 1] < at)
                ++k;
            double const span = along[k + 1] - along[k];
            double const t = span > 0.0 ? std::clamp((at - along[k]) / span, 0.0, 1.0) : 0.0;
            QPointF const pos = run[k].pos + (run[k + 1].pos - run[k].pos) * t;
            QPointF const way = run[k + 1].pos - run[k].pos;
            double const length = std::hypot(way.x(), way.y());
            QPointF const side = length > 1e-9 ? QPointF(-way.y(), way.x()) / length : QPointF();
            double const push = (i == 0 || i == count) ? 0.0 : (random.generateDouble() * 2.0 - 1.0) * amount;
            wiggled.push_back({pos + side * push, float(run[k].pressure + (run[k + 1].pressure - run[k].pressure) * t)});
        }
        line.points = wiggled;
        line.thickness.clear();     // (its places carry the thickness the line had there)
        line.curve = false;
        line.fit(std::max(amount * 0.05, 0.01), 180.0);
    }
    commitDrawing(edited, tr("Pencil Line Wiggle"));
    update();
}

void Canvas::onActionPencilLineWiggle()
{
    if (m_selStrokes.isEmpty()) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    bool ok = false;
    double const amount = QInputDialog::getDouble(this, tr("Pencil Line Wiggle"), tr("How far the line bends to each side:"), 4.0, 0.1, 500.0, 1, &ok);
    if (!ok)
        return;
    double const every = QInputDialog::getDouble(this, tr("Pencil Line Wiggle"), tr("A bend every:"), 20.0, 1.0, 2000.0, 1, &ok);
    if (ok)
        wiggleSelectedLines(amount, every, QRandomGenerator::global()->generate());
}

void Canvas::softenTips(Stroke& line, double share)
{
    // Points this share of the way in from each end (where the line has none near), then the ends made clear.
    QVector<StrokePoint> const run = line.path();
    QVector<double> const along = distancesAlong(run);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (line.points.size() < 2 || total <= 0.0)
        return;
    for (double at : {total * share, total * (1.0 - share)}) {
        int k = 0;
        while (k + 2 < run.size() && along[k + 1] < at)
            ++k;
        double const span = along[k + 1] - along[k];
        QPointF const place = run[k].pos + (run[k + 1].pos - run[k].pos) * (span > 0.0 ? (at - along[k]) / span : 0.0);
        bool near = false;
        for (StrokePoint const& point : std::as_const(line.points))
            near = near || QLineF(point.pos, place).length() < total * 0.02;
        if (!near)
            line.insertPoint(place);
    }
    // The opacity along the line: clear at the ends, solid from that share in.
    QVector<double> const placed = distancesAlong(line.points);
    double const length = placed.last();
    for (int i = 0; i < line.points.size(); ++i) {
        double const from = std::min(placed[i], length - placed[i]) / std::max(length * share, 1e-9);
        line.points[i].opacity = float(std::clamp(from, 0.0, 1.0));
    }
}

void Canvas::onActionSetPencilSoftTip()
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty()) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    Drawing edited = *d;
    for (int index : std::as_const(m_selStrokes))
        if (index >= 0 && index < edited.strokes.size() && edited.strokes[index].width > 0.0f && !edited.strokes[index].closed)
            softenTips(edited.strokes[index], 0.15);
    commitDrawing(edited, tr("Set Pencil Opacity Soft Tip"));
    update();
}

void Canvas::onActionSetPencilSoftTipAllDrawings()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[m_layer];
    m_document->beginMacro(tr("Set Pencil Opacity Soft Tip On All Drawings"));
    // Every drawing the layer shows, at the first frame that shows it.
    QSet<int> done;
    for (int frame = 0; frame < layer.cells.size(); ++frame) {
        int const index = layer.cells[frame];
        if (index < 0 || index >= layer.drawings.size() || done.contains(index))
            continue;
        done.insert(index);
        Drawing edited = layer.drawings[index];
        bool any = false;
        for (Stroke& line : edited.strokes)
            if (line.width > 0.0f && !line.closed && line.points.size() >= 2) {
                softenTips(line, 0.15);
                any = true;
            }
        if (any)
            m_document->setDrawing(m_layer, frame, edited, tr("Set Pencil Opacity Soft Tip On All Drawings"));
    }
    m_document->endMacro();
    update();
}

QVector<QPolygonF> Canvas::eraserArea(Stroke const& path) const
{
    if (!m_eraserSquare)
        return path.outlinePolygons();
    double const r = std::max(path.width / 2.0, 0.25);
    auto square = [r](QPointF c) { return QPolygonF({c + QPointF(-r, -r), c + QPointF(r, -r), c + QPointF(r, r), c + QPointF(-r, r)}); };
    auto hull = [](QVector<QPointF> points) {
        std::sort(points.begin(), points.end(), [](QPointF a, QPointF b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
        auto cross = [](QPointF o, QPointF a, QPointF b) { return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
        QVector<QPointF> lower, upper;
        for (QPointF const& p : points) {
            while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.last(), p) <= 0)
                lower.removeLast();
            lower.push_back(p);
        }
        for (int i = int(points.size()) - 1; i >= 0; --i) {
            while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper.last(), points[i]) <= 0)
                upper.removeLast();
            upper.push_back(points[i]);
        }
        lower.removeLast();
        upper.removeLast();
        return QPolygonF(lower + upper);
    };
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    if (path.points.size() == 1)
        area.addPolygon(square(path.points[0].pos));
    for (int i = 0; i + 1 < path.points.size(); ++i) {
        QPolygonF const a = square(path.points[i].pos), b = square(path.points[i + 1].pos);
        QPolygonF swept = hull(QVector<QPointF>(a.begin(), a.end()) + QVector<QPointF>(b.begin(), b.end()));
        area.addPolygon(swept);
        area.closeSubpath();
    }
    QVector<QPolygonF> polygons;
    for (QPolygonF polygon : area.simplified().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        if (polygon.size() >= 3)
            polygons.push_back(polygon);
    }
    return polygons;
}

// ---- Flatten's crossings and its other ways; invisible lines; copying art --------------------------------------------------

int Canvas::addCrossingPoints(QVector<Stroke>& strokes, QVector<int> const& which)
{
    double const same = 0.05;       // (scene units) a point already there
    auto pointAt = [&](Stroke const& line, QPointF at) {
        for (int k = 0; k < line.points.size(); ++k)
            if (QLineF(line.points[k].pos, at).length() < same)
                return k;
        return -1;
    };
    int added = 0;
    int next = 1;
    for (Stroke const& line : std::as_const(strokes))
        for (StrokePoint const& point : line.points)
            next = std::max(next, point.joint + 1);
    // One point of both lines: a joint (two joints that meet become one).
    auto joint = [&](StrokePoint& a, StrokePoint& b) {
        if (a.joint != 0 && b.joint != 0 && a.joint != b.joint) {
            int const from = b.joint;
            for (Stroke& line : strokes)
                for (StrokePoint& point : line.points)
                    if (point.joint == from)
                        point.joint = a.joint;
            return;
        }
        int const j = a.joint != 0 ? a.joint : b.joint != 0 ? b.joint : next++;
        a.joint = b.joint = j;
    };
    // One crossing at a time: each new point changes the lines, so they are looked over again (a few hundred at most).
    for (int round = 0; round < 400; ++round) {
        bool found = false;
        for (int x = 0; x < which.size() && !found; ++x) {
            for (int y = x + 1; y < which.size() && !found; ++y) {
                if (which[x] < 0 || which[y] < 0 || which[x] >= strokes.size() || which[y] >= strokes.size())
                    continue;
                Stroke& a = strokes[which[x]];
                Stroke& b = strokes[which[y]];
                if (a.points.size() < 2 || b.points.size() < 2)
                    continue;
                QVector<StrokePoint> const ra = a.path(), rb = b.path();
                int const pa = int(ra.size()) - 1 + (a.closed && ra.size() > 2 ? 1 : 0), pb = int(rb.size()) - 1 + (b.closed && rb.size() > 2 ? 1 : 0);
                for (int i = 0; i < pa && !found; ++i) {
                    QPointF const p0 = ra[i].pos, p1 = ra[(i + 1) % ra.size()].pos;
                    for (int k = 0; k < pb && !found; ++k) {
                        double const t = crossing(p0, p1, rb[k].pos, rb[(k + 1) % rb.size()].pos);
                        if (t < 0.0)
                            continue;
                        QPointF const at = p0 + (p1 - p0) * t;
                        int ia = pointAt(a, at), ib = pointAt(b, at);
                        if (ia >= 0 && ib >= 0 && a.points[ia].pos == b.points[ib].pos) {
                            if (a.points[ia].joint != 0 && a.points[ia].joint == b.points[ib].joint)
                                continue;       // one point of both already
                            joint(a.points[ia], b.points[ib]);
                            ++added;
                            found = true;
                            continue;
                        }
                        if (ia < 0)
                            ia = a.insertPoint(at);
                        if (ib < 0)
                            ib = b.insertPoint(at);
                        if (ia < 0 || ib < 0)
                            continue;
                        // Exactly one place: where the one that was there already is, or between the two new ones.
                        QPointF const place = pointAt(a, at) >= 0 && pointAt(b, at) < 0 ? a.points[ia].pos
                                              : (a.points[ia].pos + b.points[ib].pos) / 2.0;
                        a.points[ia].pos = place;
                        b.points[ib].pos = place;
                        joint(a.points[ia], b.points[ib]);
                        ++added;
                        found = true;
                    }
                }
            }
        }
        if (!found)
            break;
    }
    return added;
}

void Canvas::onActionFlattenDrawing()
{
    if (m_tool != Select)
        setTool(Select);
    selectAll();
    onActionFlatten();
}

void Canvas::onActionFlattenAllDrawings()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    if (m_tool != Select)
        setTool(Select);
    int const was = m_frame;
    QVector<int> const cells = scene.layers[m_layer].cells;
    m_document->beginMacro(tr("Flatten On All Drawings"));
    m_quietFlatten = true;
    QSet<int> done;
    for (int frame = 0; frame < cells.size(); ++frame) {
        if (cells[frame] < 0 || done.contains(cells[frame]))
            continue;
        done.insert(cells[frame]);
        m_frame = frame;        // (each drawing at the first frame that shows it)
        selectAll();
        onActionFlatten();
    }
    m_quietFlatten = false;
    m_frame = was;
    m_document->endMacro();
    clearSelection();
    update();
}

void Canvas::onActionFlattenRemoveExtra()
{
    m_document->beginMacro(tr("Flatten and Remove Extra Strokes"));
    onActionFlatten();
    onActionRemoveExtraStrokes();
    m_document->endMacro();
}

void Canvas::onActionRemoveInvisibleLines()
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    bool const chosen = selecting() && !m_selStrokes.isEmpty();
    Drawing edited = *d;
    edited.strokes.clear();
    for (int i = 0; i < d->strokes.size(); ++i)
        if (d->strokes[i].width > 0.0f || (chosen && !m_selStrokes.contains(i)))
            edited.strokes.push_back(d->strokes[i]);
    if (edited.strokes.size() == d->strokes.size()) {
        emit message(tr("There are no invisible lines here."));
        return;
    }
    clearSelection();
    commitDrawing(edited, tr("Remove Invisible Lines"));
}

void Canvas::onActionCopyArt()
{
    // From one sheet of the drawing into another: the lines (with no thickness, into the colour art) and the areas.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_copyArt.from == m_copyArt.to) {
        emit message(tr("Unable to set art layer to itself."));
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    QVector<int> frames;
    if (m_copyArt.allDrawings) {
        QSet<int> done;
        for (int frame = 0; frame < layer.cells.size(); ++frame)
            if (layer.cells[frame] >= 0 && !done.contains(layer.cells[frame])) {
                done.insert(layer.cells[frame]);
                frames.push_back(frame);
            }
    } else {
        frames = {m_frame};
    }
    int made = 0;
    m_document->beginMacro(tr("Copy Art"));
    for (int frame : std::as_const(frames)) {
        int const index = frame < layer.cells.size() ? layer.cells[frame] : -1;
        if (index < 0 || index >= layer.drawings.size())
            continue;
        Drawing whole = layer.drawings[index];
        QVector<Stroke> lines = whole.strokesOf(m_copyArt.from);
        QVector<Fill> areas = whole.fillsOf(m_copyArt.from);
        if (lines.isEmpty() && areas.isEmpty())
            continue;
        if (m_copyArt.clearFirst) {
            whole.strokesOf(m_copyArt.to).clear();
            whole.fillsOf(m_copyArt.to).clear();
        }
        bool const toColour = m_copyArt.to == Drawing::ColourSheet;
        for (Stroke line : std::as_const(lines)) {
            if (toColour)
                line.width = 0.0f;
            line.group = 0;
            whole.strokesOf(m_copyArt.to).push_back(line);
            ++made;
        }
        for (Fill const& area : std::as_const(areas)) {
            if (toColour && !area.contours.isEmpty()) {
                // A brush shape's edge bounds the paint in the colour art: its contours as lines with no thickness.
                for (Stroke contour : area.contours) {
                    contour.width = 0.0f;
                    whole.strokesOf(m_copyArt.to).push_back(contour);
                    ++made;
                }
                continue;
            }
            whole.fillsOf(m_copyArt.to).push_back(area);
            ++made;
        }
        m_committing = true;
        m_document->setDrawing(m_layer, frame, whole, tr("Copy Art"));
        m_committing = false;
    }
    m_document->endMacro();
    clearSelection();
    update();
    emit message(tr("%n thing(s) copied.", nullptr, made));
}

void Canvas::onActionCopyArtSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Copy Art Settings"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QStringList const names = {tr("Underlay Art"), tr("Colour Art"), tr("Line Art"), tr("Overlay Art")};
    QComboBox* const from = new QComboBox(&dialog);
    QComboBox* const to = new QComboBox(&dialog);
    from->addItems(names);
    to->addItems(names);
    from->setCurrentIndex(m_copyArt.from);
    to->setCurrentIndex(m_copyArt.to);
    QCheckBox* const clear = new QCheckBox(tr("Clear Destination Art First"), &dialog);
    clear->setChecked(m_copyArt.clearFirst);
    QCheckBox* const all = new QCheckBox(tr("Apply to All Drawings"), &dialog);
    all->setChecked(m_copyArt.allDrawings);
    form->addRow(tr("Source Art"), from);
    form->addRow(tr("Destination Art"), to);
    form->addRow(clear);
    form->addRow(all);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_copyArt = {from->currentIndex(), to->currentIndex(), clear->isChecked(), all->isChecked()};
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    s.setValue(QStringLiteral("tools/copyArt"), QStringLiteral("%1,%2,%3,%4").arg(m_copyArt.from).arg(m_copyArt.to).arg(int(m_copyArt.clearFirst)).arg(int(m_copyArt.allDrawings)));
}

void Canvas::smoothOpacity(Stroke& line, int percent)
{
    int const n = int(line.points.size());
    int const reach = int(std::lround(percent / 10.0));     // up to ten points on each side
    if (n < 3 || reach <= 0)
        return;
    QVector<float> was(n);
    for (int i = 0; i < n; ++i)
        was[i] = line.points[i].opacity;
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        int count = 0;
        for (int k = std::max(0, i - reach); k <= std::min(n - 1, i + reach); ++k) {
            sum += was[k];
            ++count;
        }
        line.points[i].opacity = float(sum / count);
    }
}

QVector<StrokePoint> Canvas::evenlySampled(QVector<StrokePoint> const& samples, double step)
{
    if (samples.size() < 2 || step <= 0.0)
        return samples;
    QVector<StrokePoint> out;
    out.push_back(samples.first());
    for (int i = 1; i < samples.size(); ++i) {
        StrokePoint const& a = samples[i - 1];
        StrokePoint const& b = samples[i];
        int const pieces = std::clamp(int(std::ceil(QLineF(a.pos, b.pos).length() / step)), 1, 2000);
        for (int k = 1; k < pieces; ++k) {
            double const t = double(k) / pieces;
            StrokePoint between = a;
            between.pos = a.pos + (b.pos - a.pos) * t;
            between.pressure = float(a.pressure + (b.pressure - a.pressure) * t);
            between.lean = float(a.lean + (b.lean - a.lean) * t);
            between.opacity = float(a.opacity + (b.opacity - a.opacity) * t);
            between.handles = between.corner = false;
            out.push_back(between);
        }
        out.push_back(b);
    }
    return out;
}
