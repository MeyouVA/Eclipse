// Eclipse - the Camera view: Drawing > Paint and Clean Up on what is selected (or on all that is
// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <functional>

#include <QInputDialog>
#include <QSettings>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/window/MainWindowPrivate.h"

namespace {

enum What { UnpaintArt, RepaintArt, RemoveArt };

// Does `what` to the lines and paint that `chosen` says yes to; true when anything changed.
bool changeArt(QVector<Stroke>& strokes, QVector<Fill>& fills, std::function<bool(bool line, int index)> const& chosen, What what,
               std::function<bool(int swatch)> const& kept, int swatch, QColor const& colour)
{
    bool changed = false;
    for (int i = int(strokes.size()) - 1; i >= 0; --i) {
        if (!chosen(true, i))
            continue;
        Stroke& stroke = strokes[i];
        if (what == RemoveArt) {
            strokes.removeAt(i);
            changed = true;
        } else if (kept(stroke.swatch)) {
            continue;       // a protected colour is left as it is
        } else if (what == UnpaintArt && stroke.width > 0.0f) {
            stroke.width = 0.0f;        // there still, to bound paint, but not seen
            changed = true;
        } else if (what == RepaintArt && stroke.width > 0.0f && (stroke.swatch != swatch || stroke.color != colour)) {
            stroke.swatch = swatch;
            stroke.color = colour;
            changed = true;
        }
    }
    for (int i = int(fills.size()) - 1; i >= 0; --i) {
        if (!chosen(false, i))
            continue;
        Fill& fill = fills[i];
        if (what == RemoveArt) {
            fills.removeAt(i);
            changed = true;
        } else if (kept(fill.swatch)) {
            continue;
        } else if (what == UnpaintArt) {
            fills.removeAt(i);
            changed = true;
        } else if (fill.swatch != swatch || fill.color != colour) {
            fill.swatch = swatch;
            fill.color = colour;
            changed = true;
        }
    }
    return changed;
}

}  // namespace

bool Canvas::keepsColour(int swatch) const
{
    return m_respectProtected && m_document->scene().protects(swatch);
}

void Canvas::onActionRespectProtected(bool on)
{
    m_respectProtected = on;
}

// `how` is what to do - "unpaint", "repaint" or "remove" - then "Outside" when it is for all that
// is not selected, then "All" when it is for every drawing of the layer.
void Canvas::onActionSelectionArt(QString const& how)
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection() || m_layer < 0 || m_layer >= scene.layers.size()) {
        emit message(tr("Select artwork with the Select tool first."));
        return;
    }
    What const what = how.startsWith(QLatin1String("unpaint")) ? UnpaintArt : how.startsWith(QLatin1String("repaint")) ? RepaintArt : RemoveArt;
    bool const outside = how.contains(QLatin1String("Outside"));
    bool const everywhere = how.endsWith(QLatin1String("All"));
    QString const text = what == UnpaintArt ? (outside ? tr("Unpaint Outside Selection") : tr("Unpaint Selection"))
                         : what == RepaintArt ? (outside ? tr("Repaint Outside Selection") : tr("Repaint Selection"))
                         : (outside ? tr("Remove Art Outside Selection") : tr("Remove Art Inside Selection"));
    QColor const colour = scene.resolve(m_swatch, Qt::black);
    int const swatch = m_swatch;
    auto kept = [this](int id) { return keepsColour(id); };
    QVector<int> const lines = m_selStrokes, paints = m_selFills;
    // In the other drawings: whatever lies within the place the selection has here.
    QRectF const region = selectionBounds().adjusted(-1.5, -1.5, 1.5, 1.5);

    Drawing edited = *d;
    bool const here = changeArt(edited.strokes, edited.fills,
                                [&](bool line, int i) { return (line ? lines.contains(i) : paints.contains(i)) != outside; }, what, kept, swatch, colour);
    // Things may have gone: what is selected is known by its place in the drawing no longer.
    bool const fewer = edited.strokes.size() != d->strokes.size() || edited.fills.size() != d->fills.size();
    QVector<int> keptLines, keptPaints;
    if (fewer && outside && what == RemoveArt) {
        // All that is left is what was selected: it stays selected.
        for (int i = 0; i < edited.strokes.size(); ++i)
            keptLines.push_back(i);
        for (int i = 0; i < edited.fills.size(); ++i)
            keptPaints.push_back(i);
    } else if (fewer && outside) {
        // Only paint outside the selection went: the lines are where they were; the paint selected
        // has moved down by as many as went below it.
        keptLines = lines;
        for (int i : paints) {
            int below = 0;
            for (int k = 0; k < i; ++k)
                below += !paints.contains(k) && !kept(d->fills[k].swatch);
            keptPaints.push_back(i - below);
        }
    }
    if (everywhere)
        m_document->beginMacro(text);
    if (here) {
        if (fewer)
            clearSelection();
        commitDrawing(edited, text);
        if (fewer && !(keptLines.isEmpty() && keptPaints.isEmpty())) {
            m_selStrokes = keptLines;
            m_selFills = keptPaints;
            emit selectionChanged();
        }
    }
    if (everywhere) {
        // A copy: the scene changes under us with every drawing set.
        QVector<Drawing> const drawings = scene.layers[m_layer].drawings;
        QVector<int> const cells = scene.layers[m_layer].cells;
        int const sheet = sheetOf(m_art);
        QVector<bool> done(drawings.size(), false);
        if (m_frame >= 0 && m_frame < cells.size() && cells[m_frame] >= 0 && cells[m_frame] < done.size())
            done[cells[m_frame]] = true;    // the one in hand has been seen to
        m_committing = true;
        for (int f = 0; f < cells.size(); ++f) {
            int const index = cells[f];
            if (index < 0 || index >= drawings.size() || done[index])
                continue;
            done[index] = true;
            Drawing whole = drawings[index];
            QVector<Stroke>& strokes = whole.strokesOf(sheet);
            QVector<Fill>& fills = whole.fillsOf(sheet);
            auto within = [&](bool line, int i) { return region.contains(line ? strokes[i].bounds() : fills[i].bounds()) != outside; };
            if (changeArt(strokes, fills, within, what, kept, swatch, colour))
                m_document->setDrawing(m_layer, f, whole, text);
        }
        m_committing = false;
        m_document->endMacro();
    }
    update();
}

// ---- Reposition All Drawings ------------------------------------------------------------------

void Canvas::repositionOthers(QTransform const& done, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    // A copy: the scene changes under us with every drawing set.
    QVector<Drawing> const drawings = scene.layers[m_layer].drawings;
    QVector<int> const cells = scene.layers[m_layer].cells;
    int const current = m_frame >= 0 && m_frame < cells.size() ? cells[m_frame] : -1;
    int const mine = sheetOf(m_art);
    QVector<bool> seen(drawings.size(), false);
    m_committing = true;
    for (int f = 0; f < cells.size(); ++f) {
        int const index = cells[f];
        if (index < 0 || index >= drawings.size() || seen[index])
            continue;
        seen[index] = true;
        Drawing whole = drawings[index];
        bool changed = false;
        for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::LineSheet), int(Drawing::OverlaySheet)}) {
            if (index == current && sheet == mine)
                continue;       // that one has been seen to
            for (Stroke& stroke : whole.strokesOf(sheet)) {
                stroke.transform(done);
                changed = true;
            }
            for (Fill& fill : whole.fillsOf(sheet)) {
                fill.transform(done);
                changed = true;
            }
        }
        for (Picture& picture : whole.pictures) {
            picture.placement = picture.placement * done;
            changed = true;
        }
        if (changed)
            m_document->setDrawing(m_layer, f, whole, text);
    }
    m_committing = false;
}

// ---- the Stamp tool --------------------------------------------------------------------------------

Drawing Canvas::stampNow() const
{
    if (m_stampOwn)
        return m_stamp;
    // To begin with: a star, in the colour in hand.
    Drawing star;
    Fill shape;
    QPolygonF outline;
    for (int i = 0; i < 10; ++i) {
        double const angle = -3.14159265358979323846 / 2.0 + i * 3.14159265358979323846 / 5.0;
        double const reach = i % 2 == 0 ? 40.0 : 16.0;
        outline << QPointF(std::cos(angle) * reach, std::sin(angle) * reach);
    }
    shape.outlines = {outline};
    shape.swatch = m_swatch;
    shape.color = m_document->scene().resolve(m_swatch, Qt::black);
    star.fills = {shape};
    return star;
}

void Canvas::clearStamp()
{
    m_stamp = Drawing();
    m_stampOwn = false;
    update();
}

void Canvas::onActionCreateStamp()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to make a stamp of with the Select tool."));
        return;
    }
    // What is selected, kept about its own middle: that is where the pointer will be when it is stamped.
    QTransform const home = QTransform::fromTranslate(-selectionBounds().center().x(), -selectionBounds().center().y());
    Drawing stamp;
    for (int i : std::as_const(m_selStrokes)) {
        stamp.strokes.push_back(d->strokes[i]);
        stamp.strokes.last().transform(home);
    }
    for (int i : std::as_const(m_selFills)) {
        stamp.fills.push_back(d->fills[i]);
        stamp.fills.last().transform(home);
    }
    m_stamp = stamp;
    m_stampOwn = true;
    emit message(tr("The Stamp tool now stamps this artwork."));
}

void Canvas::stampAt(QPointF p)
{
    Drawing const stamp = stampNow();
    double const k = m_stampScale / 100.0;
    QTransform const place = QTransform::fromScale(k, k) * QTransform::fromTranslate(p.x(), p.y());
    Drawing edited = currentDrawing() ? *currentDrawing() : Drawing();
    // Stamped, its parts are one thing: picked together.
    int group = 0;
    if (stamp.strokes.size() + stamp.fills.size() > 1) {
        group = 1;
        for (Stroke const& stroke : std::as_const(edited.strokes))
            group = std::max(group, stroke.group + 1);
        for (Fill const& fill : std::as_const(edited.fills))
            group = std::max(group, fill.group + 1);
    }
    for (Fill fill : stamp.fills) {
        fill.transform(place);
        fill.group = group;
        edited.fills.push_back(fill);
    }
    for (Stroke stroke : stamp.strokes) {
        stroke.transform(place);
        stroke.group = group;
        edited.strokes.push_back(stroke);
    }
    commitDrawing(edited, tr("Stamp"));
    update();
}

// ---- Generate Auto-Matte ---------------------------------------------------------------------------

// A painted shape, in the colour in hand, that covers everything inside the outline of the line art:
// put at the back of the colour art, to stand behind the drawing or to be cut with.
void Canvas::onActionAutoMatte()
{
    Drawing const* const real = realDrawing();
    if (!real || (real->strokes.isEmpty() && real->fills.isEmpty())) {
        emit message(tr("There is no line art on this frame to make a matte of."));
        return;
    }
    Fill matte;
    matte.outlines = Deform::outerEdges(*real, Drawing::LineSheet);
    if (matte.outlines.isEmpty()) {
        emit message(tr("There is no line art on this frame to make a matte of."));
        return;
    }
    matte.swatch = m_swatch;
    matte.color = m_document->scene().resolve(m_swatch, Qt::black);
    Drawing whole = *real;
    whole.colourFills.push_front(matte);
    clearSelection();
    m_committing = true;
    m_document->setDrawing(m_layer, m_frame, whole, tr("Generate Auto-Matte"));
    m_committing = false;
    emit message(tr("The matte is in the colour art, behind the lines."));
    update();
}

// ---- Close Gaps ---------------------------------------------------------------------------------

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

// ---- contour strokes ------------------------------------------------------------------------------

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

// ---- Strokes to Pencil Lines ---------------------------------------------------------------------

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
        for (StrokePoint& point : stroke.points)
            point.pressure = 1.0f;
        changed = true;
    }
    if (changed)
        commitDrawing(edited, tr("Strokes to Pencil Lines"));
    update();
}
