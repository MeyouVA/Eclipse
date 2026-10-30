// Eclipse - the Camera view: brush shapes broken where they branch (breaking triangles), and a selection's gradient kept.
#include "canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <numbers>

#include "model/document/Commands.h"
#include "model/geometry/Vectorize.h"

int Canvas::breakAtJunctions(Drawing& drawing, QVector<int> const& which, double pixelsPerUnit)
{
    int junctions = 0;
    QVector<Fill> made;
    for (int i = 0; i < drawing.fills.size(); ++i) {
        Fill const& fill = drawing.fills[i];
        if (!which.contains(i) || fill.outlines.isEmpty()) {
            made.push_back(fill);
            continue;
        }
        QPainterPath shape;
        shape.setFillRule(Qt::WindingFill);
        for (QPolygonF const& outline : fill.outlines)
            shape.addPolygon(outline);
        shape = shape.simplified();
        QRectF const box = shape.boundingRect();
        double const scale = std::clamp(pixelsPerUnit, 0.05, 1600.0 / std::max({box.width(), box.height(), 1.0}));
        QImage picture(QSize(int(std::ceil(box.width() * scale)) + 8, int(std::ceil(box.height() * scale)) + 8), QImage::Format_RGB32);
        picture.fill(Qt::white);
        QTransform const toPicture = QTransform::fromTranslate(4, 4).scale(scale, scale).translate(-box.left(), -box.top());
        {
            QPainter p(&picture);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setTransform(toPicture);
            p.fillPath(shape, Qt::black);
        }
        // Where its middle lines meet three or more at once: the branchings.
        QVector<Vectorize::CentreLine> const lines = Vectorize::centreLines(picture, 128, 3);
        struct End {
            QPointF at, toward;
        };
        QVector<End> ends;
        for (Vectorize::CentreLine const& line : lines) {
            if (line.closed || line.points.size() < 2)
                continue;
            int const step = std::min<int>(4, int(line.points.size()) - 1);
            ends.push_back({line.points.first(), line.points[step]});
            ends.push_back({line.points.last(), line.points[line.points.size() - 1 - step]});
        }
        double const near = 3.0;
        QVector<bool> used(ends.size(), false);
        QPainterPath triangles;
        QVector<QPolygonF> corners;
        for (int a = 0; a < ends.size(); ++a) {
            if (used[a])
                continue;
            QVector<int> meeting{a};
            for (int b = a + 1; b < ends.size(); ++b)
                if (!used[b] && QLineF(ends[a].at, ends[b].at).length() <= near)
                    meeting.push_back(b);
            if (meeting.size() < 3)
                continue;
            QPointF middle;
            QVector<double> ways;
            for (int k : meeting) {
                used[k] = true;
                middle += ends[k].at;
            }
            middle /= double(meeting.size());
            for (int k : meeting)
                ways.push_back(std::atan2(ends[k].toward.y() - middle.y(), ends[k].toward.x() - middle.x()));
            std::sort(ways.begin(), ways.end());
            // Between each two branches, straight out from the branching to the shape's edge: a corner of its triangle.
            QPolygonF corner;
            for (int k = 0; k < ways.size(); ++k) {
                double next = k + 1 < ways.size() ? ways[k + 1] : ways[0] + 2.0 * std::numbers::pi;
                double const between = (ways[k] + next) / 2.0;
                QPointF const way(std::cos(between), std::sin(between));
                QPointF reach = middle;
                for (double d = 0.5; d < 400.0; d += 0.5) {
                    QPoint const q = (middle + way * d).toPoint();
                    if (!picture.rect().contains(q) || qGray(picture.pixel(q)) > 128)
                        break;
                    reach = middle + way * d;
                }
                corner << toPicture.inverted().map(reach + way * 0.75);
            }
            if (corner.size() >= 3) {
                corners.push_back(corner);
                QPainterPath one;
                one.addPolygon(corner);
                one.closeSubpath();
                triangles = triangles.united(one);
                ++junctions;
            }
        }
        if (corners.isEmpty()) {
            made.push_back(fill);
            continue;
        }
        // The branches, cut apart by the triangles; and the triangles, kept inside the shape.
        auto add = [&](QPainterPath const& path) {
            for (QPolygonF const& piece : path.toFillPolygons()) {
                if (piece.size() < 3)
                    continue;
                Fill part = fill;
                part.outlines = {piece};
                part.contours.clear();
                part.giveContours(0.5 / std::max(scale, 1e-6));
                made.push_back(part);
            }
        };
        add(shape.subtracted(triangles));
        add(shape.intersected(triangles));
    }
    drawing.fills = made;
    return junctions;
}

void Canvas::onActionCreateBreakingTriangles()
{
    Drawing const* const d = currentDrawing();
    if (!d || !canEditDrawing())
        return;
    QVector<int> which = hasSelection() ? m_selFills : QVector<int>();
    if (which.isEmpty())
        for (int i = 0; i < d->fills.size(); ++i)
            which.push_back(i);
    Drawing edited = *d;
    int const found = breakAtJunctions(edited, which, std::max(m_zoom, 1.0) * 2.0);
    if (found == 0) {
        emit message(tr("No brush shape here branches: there is nothing to break."));
        return;
    }
    clearSelection();
    commitDrawing(edited, tr("Create Breaking Triangles"));
    emit message(tr("%n brush shape branching(s) broken.", nullptr, found));
}

void Canvas::onActionStoreColourGradient()
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection()) {
        emit message(tr("Select an area (or a line) painted with a gradient first."));
        return;
    }
    // The first selected with a gradient: where it lies (one not placed by hand lies across its area, left to right).
    auto keep = [this](bool placed, QPointF from, QPointF to, QRectF box) {
        m_storedGradFrom = placed ? from : QPointF(box.left(), box.center().y());
        m_storedGradTo = placed ? to : QPointF(box.right(), box.center().y());
        m_hasStoredGradient = true;
        emit message(tr("Colour gradient stored: paint with Use Stored Colour Gradient lies the same way."));
    };
    for (int i : std::as_const(m_selFills))
        if (Swatch const* const s = m_document->scene().findSwatch(d->fills[i].swatch); s && s->isGradient())
            return keep(d->fills[i].gradPlaced, d->fills[i].gradFrom, d->fills[i].gradTo, d->fills[i].bounds());
    for (int i : std::as_const(m_selStrokes))
        if (Swatch const* const s = m_document->scene().findSwatch(d->strokes[i].swatch); s && s->isGradient())
            return keep(d->strokes[i].gradPlaced, d->strokes[i].gradFrom, d->strokes[i].gradTo, d->strokes[i].bounds());
    emit message(tr("Nothing selected is painted with a gradient."));
}
