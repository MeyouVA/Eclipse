// Eclipse - the Camera view: pencil lines - stencils, reversing, merging and splitting, taper and speed, trimming and joining ends.
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
#include "render/effects/Effects.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "ui/canvas/CanvasTidyShared.h"

using namespace canvas_tidy_shared;
#include "ui/canvas/CanvasPencilLinesShared.h"

using namespace canvas_pencil_lines_shared;

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
