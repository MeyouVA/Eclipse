// Eclipse - the Camera view: tidying lines - smoothing, thickness, dirt and hair, art inside or outside, fewer points.
#include "canvas/Canvas.h"
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
#include "render/effects/Effects.h"
#include "actions/theme/AppSettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "canvas/CanvasTidyShared.h"

using namespace canvas_tidy_shared;

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
