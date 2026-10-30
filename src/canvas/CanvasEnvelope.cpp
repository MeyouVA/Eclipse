// Eclipse - the Camera view: the envelope round a selection; gaps closed; contour strokes made and removed; strokes to pencil.
// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "canvas/Canvas.h"

#include <cmath>

#include <QRandomGenerator>
#include <functional>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QSettings>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "actions/theme/AppSettings.h"
#include "canvas/CanvasCleanUpShared.h"

using namespace canvas_clean_up_shared;

// So many points (eight to begin with) on a loop close round what is selected.
QVector<QPointF> Canvas::selectionEnvelope() const
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return {};
    Drawing chosen;
    for (int i : m_selStrokes)
        chosen.strokes.push_back(d->strokes[i]);
    for (int i : m_selFills)
        chosen.fills.push_back(d->fills[i]);
    return Deform::envelopeAround(chosen, std::clamp(m_envCount, 4, 32), 6.0 / std::max(m_zoom, 1e-6));
}

QVector<QPointF> Canvas::selectionGrid(QRectF* box) const
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return {};
    QRectF around;
    for (int i : m_selStrokes)
        around |= d->strokes[i].bounds();
    for (int i : m_selFills)
        around |= d->fills[i].bounds();
    if (around.isNull())
        return {};
    double const room = 6.0 / std::max(m_zoom, 1e-6);
    around.adjust(-room, -room, room, room);
    if (box)
        *box = around;
    QVector<QPointF> points;
    for (int row = 0; row <= m_envRows; ++row)
        for (int column = 0; column <= m_envCols; ++column)
            points.push_back(QPointF(around.left() + around.width() * column / m_envCols, around.top() + around.height() * row / m_envRows));
    return points;
}

QPointF Canvas::gridMapped(QPointF q, QRectF const& box, int rows, int columns, QVector<QPointF> const& now)
{
    if (box.width() <= 0.0 || box.height() <= 0.0 || now.size() != (rows + 1) * (columns + 1))
        return q;
    // Its cell of the grid as it stood, and where in the cell it is; the cell's corners as they are now, mixed so.
    double const u = (q.x() - box.left()) / box.width() * columns, v = (q.y() - box.top()) / box.height() * rows;
    int const column = std::clamp(int(std::floor(u)), 0, columns - 1), row = std::clamp(int(std::floor(v)), 0, rows - 1);
    double const fu = u - column, fv = v - row;
    auto at = [&](int r, int c) { return now[r * (columns + 1) + c]; };
    return at(row, column) * ((1 - fu) * (1 - fv)) + at(row, column + 1) * (fu * (1 - fv)) + at(row + 1, column) * ((1 - fu) * fv)
           + at(row + 1, column + 1) * (fu * fv);
}

// A line or an area's outline bent: each point (and the ends of its handles) to where `map` puts it.
template <class Map> static void bentLine(Stroke& line, Map const& map)
{
    for (StrokePoint& point : line.points) {
        QPointF const was = point.pos, to = map(was);
        point.in = map(was + point.in) - to;
        point.out = map(was + point.out) - to;
        point.pos = to;
    }
}

template <class Map> static void bentArt(Drawing& art, Map const& map)
{
    for (Stroke& line : art.strokes)
        bentLine(line, map);
    for (Fill& fill : art.fills) {
        if (!fill.contours.isEmpty()) {
            for (Stroke& contour : fill.contours)
                bentLine(contour, map);
            fill.rebuild();
        } else {
            for (QPolygonF& outline : fill.outlines)
                for (QPointF& q : outline)
                    q = map(q);
        }
        if (fill.gradPlaced) {
            fill.gradFrom = map(fill.gradFrom);
            fill.gradTo = map(fill.gradTo);
        }
    }
}

QPointF Canvas::envelopePointInView(int point) const
{
    QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
    return m_tool == Envelope && point >= 0 && point * 3 < points.size() ? layerViewTransform().map(points[point * 3]) : QPointF();
}

QPointF Canvas::envelopeHandleInView(bool after) const
{
    QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
    if (m_tool != Envelope || !m_envAdvanced || m_envPicked < 0 || m_envPicked + 2 >= points.size())
        return QPointF();
    return layerViewTransform().map(points[m_envPicked] + points[m_envPicked + (after ? 2 : 1)]);
}

bool Canvas::envelopeDown(QPointF widgetPos)
{
    QTransform const view = layerViewTransform();
    if (m_envGrid) {
        // The deformation grid: a point of it taken hold of.
        QVector<QPointF> const grid = selectionGrid(&m_envGridRect);
        // (The nearest point to the pen, of those near enough: on a small grid they crowd together.)
        int nearest = -1;
        double best = 8.0;
        for (int i = 0; i < grid.size(); ++i)
            if (double const d = QLineF(view.map(grid[i]), widgetPos).length(); d <= best) {
                best = d;
                nearest = i;
            }
        if (nearest < 0)
            return false;
        m_envRest = m_envNow = grid;
        m_envPoint = nearest;
        m_eraseArt = *currentDrawing();
        m_drag = DragEnvelope;
        return true;
    }
    QVector<QPointF> const points = selectionEnvelope();
    // A handle of the point pressed last: the outline is led another way through its point.
    if (m_envAdvanced && m_envPicked >= 0 && m_envPicked + 2 < points.size()) {
        for (int which : {1, 2}) {
            if (QLineF(view.map(points[m_envPicked] + points[m_envPicked + which]), widgetPos).length() > 7.0)
                continue;
            m_envRest = m_envNow = points;
            m_envPoint = m_envPicked + which;
            m_eraseArt = *currentDrawing();
            m_drag = DragEnvelope;
            return true;
        }
    }
    for (int i = 0; i < points.size(); i += 3) {
        if (QLineF(view.map(points[i]), widgetPos).length() > 8.0)
            continue;
        m_envRest = m_envNow = points;
        m_envPoint = i;
        m_envPicked = i;
        m_eraseArt = *currentDrawing();
        m_drag = DragEnvelope;
        return true;
    }
    return false;
}

void Canvas::envelopeMove(QPointF p)
{
    Drawing const* const d = currentDrawing();
    if (!d || m_envPoint < 0 || m_envPoint >= m_envNow.size())
        return;
    if (m_envGrid) {
        // The grid point goes with the pen; what is selected follows the cells it is in.
        m_envNow = m_envRest;
        m_envNow[m_envPoint] = p;
        Drawing chosen;
        for (int i : std::as_const(m_selStrokes))
            chosen.strokes.push_back(d->strokes[i]);
        for (int i : std::as_const(m_selFills))
            chosen.fills.push_back(d->fills[i]);
        QRectF const box = m_envGridRect;
        int const rows = m_envRows, columns = m_envCols;
        QVector<QPointF> const now = m_envNow;
        bentArt(chosen, [&](QPointF q) { return gridMapped(q, box, rows, columns, now); });
        m_eraseArt = *d;
        for (int k = 0; k < m_selStrokes.size(); ++k)
            m_eraseArt.strokes[m_selStrokes[k]] = chosen.strokes[k];
        for (int k = 0; k < m_selFills.size(); ++k)
            m_eraseArt.fills[m_selFills[k]] = chosen.fills[k];
        update();
        return;
    }
    // The point goes with the pen; the curve is led smoothly through it and its two neighbours again.
    m_envNow = m_envRest;
    if (m_envPoint % 3 != 0) {
        // A handle: it goes with the pen (it is kept as how far it stands from its point); nothing else is led anew.
        m_envNow[m_envPoint] = p - m_envRest[m_envPoint - m_envPoint % 3];
    } else {
        m_envNow[m_envPoint] = p;
        int const count = int(m_envNow.size()) / 3, at = m_envPoint / 3;
        for (int k : {(at + count - 1) % count, at, (at + 1) % count})
            Deform::smoothWarpHandles(m_envNow, true, k);
    }
    QVector<QPointF> moved(m_envRest.size());
    for (int i = 0; i < moved.size(); ++i)
        moved[i] = m_envNow[i] - m_envRest[i];
    // What is selected, reshaped by that; the rest of the drawing as it is.
    Drawing chosen;
    for (int i : std::as_const(m_selStrokes))
        chosen.strokes.push_back(d->strokes[i]);
    for (int i : std::as_const(m_selFills))
        chosen.fills.push_back(d->fills[i]);
    Drawing const pulled = Deform::warp(chosen, m_envRest, true, moved);
    m_eraseArt = *d;
    if (pulled.strokes.size() == m_selStrokes.size() && pulled.fills.size() == m_selFills.size()) {
        for (int k = 0; k < m_selStrokes.size(); ++k)
            m_eraseArt.strokes[m_selStrokes[k]] = pulled.strokes[k];
        for (int k = 0; k < m_selFills.size(); ++k)
            m_eraseArt.fills[m_selFills[k]] = pulled.fills[k];
    }
    update();
}

int Canvas::closeGaps(double gap)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return 0;
    bool const some = selecting() && hasSelection();
    // Every line drawn out, to look for the nearest place on it.
    QVector<QVector<QPointF>> paths;
    for (Stroke const& stroke : d->strokes) {
        QVector<QPointF> path;
        for (StrokePoint const& point : stroke.path())
            path.push_back(point.pos);
        if (stroke.closed && !path.isEmpty())
            path.push_back(path.first());
        paths.push_back(path);
    }
    auto nearestOn = [](QPointF a, QPointF b, QPointF p) {
        QPointF const ab = b - a;
        double const along = ab.x() * ab.x() + ab.y() * ab.y();
        double const t = along < 1e-12 ? 0.0 : std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / along, 0.0, 1.0);
        return a + ab * t;
    };
    Drawing edited = *d;
    QVector<QLineF> made;
    for (int i = 0; i < d->strokes.size(); ++i) {
        Stroke const& stroke = d->strokes[i];
        if (stroke.closed || paths[i].size() < 2 || (some && !m_selStrokes.contains(i)))
            continue;
        for (int end = 0; end < 2; ++end) {
            QPointF const from = end == 0 ? paths[i].first() : paths[i].last();
            QPointF best;
            double nearest = gap;
            bool found = false;
            auto look = [&](QPointF q) {
                double const distance = QLineF(from, q).length();
                if (distance <= nearest) {
                    nearest = distance;
                    best = q;
                    found = true;
                }
            };
            for (int j = 0; j < paths.size(); ++j) {
                if (j == i) {
                    look(end == 0 ? paths[i].last() : paths[i].first());     // of its own line, only the other end
                    continue;
                }
                for (int k = 1; k < paths[j].size(); ++k)
                    look(nearestOn(paths[j][k - 1], paths[j][k], from));
            }
            if (!found || nearest < 1e-6)
                continue;       // nothing near, or it touches already
            bool twice = false;
            for (QLineF const& line : std::as_const(made))
                twice = twice || (QLineF(line.p1(), best).length() < 1e-6 && QLineF(line.p2(), from).length() < 1e-6);
            if (twice)
                continue;       // the two ends found each other: one line between them is enough
            made.push_back(QLineF(from, best));
            // A line with no thickness: not seen, but paint stops at it.
            Stroke bridge;
            bridge.width = 0.0f;
            bridge.swatch = stroke.swatch;
            bridge.color = stroke.color;
            bridge.points = {{from, 1.0f}, {best, 1.0f}};
            edited.strokes.push_back(bridge);
        }
    }
    if (!made.isEmpty())
        commitDrawing(edited, tr("Close Gaps"));
    return int(made.size());
}

void Canvas::onActionCloseGaps()
{
    if (!currentDrawing()) {
        emit message(tr("There is no drawing on this frame to clean up."));
        return;
    }
    bool ok = false;
    int const gap = QInputDialog::getInt(this, tr("Close Gaps"), tr("Close gaps no wider than (pixels):"),
                                         settings().value(QStringLiteral("tools/gapSize"), 10).toInt(), 1, 500, 1, &ok);
    if (!ok)
        return;
    settings().setValue(QStringLiteral("tools/gapSize"), gap);
    emit message(tr("%n gap(s) closed.", nullptr, closeGaps(gap)));
}

void Canvas::onActionCreateContourStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selFills.isEmpty()) {
        emit message(tr("Select painted areas with the Select tool to put strokes round them."));
        return;
    }
    // Round each outline of each area, a closed line with no thickness: not seen, but paint stops at it.
    Drawing edited = *d;
    for (int i : std::as_const(m_selFills)) {
        Fill const& fill = d->fills[i];
        bool const curves = fill.contours.size() == fill.outlines.size();
        for (int k = 0; k < fill.outlines.size(); ++k) {
            Stroke round;
            if (curves) {
                round = fill.contours[k];
            } else {
                QPolygonF outline = fill.outlines[k];
                if (outline.size() > 1 && outline.first() == outline.last())
                    outline.removeLast();
                for (QPointF const& point : std::as_const(outline))
                    round.points.push_back({point, 1.0f});
            }
            if (round.points.size() < 3)
                continue;
            round.closed = true;
            round.width = 0.0f;
            round.swatch = fill.swatch;
            round.color = fill.color;
            round.group = fill.group;
            edited.strokes.push_back(round);
        }
    }
    if (edited.strokes.size() != d->strokes.size())
        commitDrawing(edited, tr("Create Contour Strokes"));
    update();
}

void Canvas::onActionRemoveContourStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to take the strokes with no thickness out of."));
        return;
    }
    Drawing edited = *d;
    edited.strokes.clear();
    QVector<int> lines;
    for (int i = 0; i < d->strokes.size(); ++i) {
        if (m_selStrokes.contains(i) && d->strokes[i].width <= 0.0f)
            continue;
        if (m_selStrokes.contains(i))
            lines.push_back(int(edited.strokes.size()));
        edited.strokes.push_back(d->strokes[i]);
    }
    if (edited.strokes.size() == d->strokes.size())
        return;
    QVector<int> const paints = m_selFills;
    clearSelection();
    commitDrawing(edited, tr("Remove Contour Strokes"));
    m_selStrokes = lines;
    m_selFills = paints;
    emit selectionChanged();
    update();
}

void Canvas::onActionRemoveExtraStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    bool const chosen = selecting() && hasSelection();
    // The paint on top at a place: its number, or -1 where there is none.
    auto paintAt = [&](QPointF q) {
        for (int i = int(d->fills.size()) - 1; i >= 0; --i)
            if (d->fills[i].contains(q))
                return i;
        return -1;
    };
    auto extra = [&](Stroke const& stroke) {
        if (stroke.width > 0.0f || stroke.points.size() < 2)
            return false;
        int swatch = Scene::kNoSwatch;
        QColor colour;
        bool first = true;
        auto same = [&](QPointF q) {
            // Not only at the place itself: a little to every side of it too, so that a stroke along an edge stays.
            for (QPointF const off : {QPointF(0, 0), QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
                int const paint = paintAt(q + off);
                if (paint < 0)
                    return false;
                Fill const& fill = d->fills[paint];
                if (first) {
                    swatch = fill.swatch;
                    colour = fill.color;
                    first = false;
                } else if (fill.swatch != swatch || (swatch == Scene::kNoSwatch && fill.color != colour)) {
                    return false;
                }
            }
            return true;
        };
        for (int i = 0; i < stroke.points.size(); ++i) {
            if (!same(stroke.points[i].pos))
                return false;
            if (i + 1 < stroke.points.size() && !same((stroke.points[i].pos + stroke.points[i + 1].pos) / 2.0))
                return false;
        }
        return true;
    };
    Drawing edited = *d;
    edited.strokes.clear();
    QVector<int> lines;
    for (int i = 0; i < d->strokes.size(); ++i) {
        bool const mine = !chosen || m_selStrokes.contains(i);
        if (mine && extra(d->strokes[i]))
            continue;
        if (chosen && m_selStrokes.contains(i))
            lines.push_back(int(edited.strokes.size()));
        edited.strokes.push_back(d->strokes[i]);
    }
    if (edited.strokes.size() == d->strokes.size()) {
        emit message(tr("There are no extra strokes: none with no thickness lies wholly inside paint of one colour."));
        return;
    }
    int const gone = int(d->strokes.size() - edited.strokes.size());
    QVector<int> const paints = chosen ? m_selFills : QVector<int>();
    clearSelection();
    commitDrawing(edited, tr("Remove Extra Strokes"));
    m_selStrokes = lines;
    m_selFills = paints;
    emit selectionChanged();
    emit message(tr("%n extra stroke(s) removed.", nullptr, gone));
    update();
}

void Canvas::onActionStrokesToPencil()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select the strokes to convert with the Select tool."));
        return;
    }
    // A stroke with no thickness gets the thickness the pencil has now.
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selStrokes)) {
        Stroke& stroke = edited.strokes[i];
        if (stroke.width > 0.0f)
            continue;
        stroke.width = float(m_brushWidth);
        stroke.thickness.clear();
        for (StrokePoint& point : stroke.points)
            point.pressure = 1.0f;
        changed = true;
    }
    if (changed)
        commitDrawing(edited, tr("Strokes to Pencil Lines"));
    update();
}
