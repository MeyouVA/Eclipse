// Eclipse - the Camera view: the grid and the safe area; snapping to guides, grid and contours.
#include "ui/canvas/Canvas.h"

#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "ui/window/InterfaceColours.h"

void Canvas::onActionShowGrid(bool on)
{
    m_grid = on;
    update();
}

void Canvas::onActionGridKind(int kind)
{
    m_gridKind = std::clamp(kind, 0, 3);
    update();
}

void Canvas::onActionGridOutlineOnly(bool on)
{
    m_gridOutline = on;
    update();
}

void Canvas::onActionGridUnderlay(bool on)
{
    m_gridUnder = on;
    update();
}

void Canvas::paintGrid(QPainter& painter, QRectF const& frameRect) const
{
    QColor const grid = InterfaceColours::get("canvasGrid");
    QPen thin(QColor(grid.red(), grid.green(), grid.blue(), grid.alpha() * 90 / 190));
    thin.setCosmetic(true);
    QPen strong(grid);
    strong.setCosmetic(true);
    double const w = frameRect.width(), h = frameRect.height();
    if (m_gridOutline) {
        // Only its edge, its middle lines and its diagonals.
        painter.setPen(strong);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frameRect);
        painter.drawLine(QPointF(w / 2, 0), QPointF(w / 2, h));
        painter.drawLine(QPointF(0, h / 2), QPointF(w, h / 2));
        painter.setPen(thin);
        painter.drawLine(frameRect.topLeft(), frameRect.bottomRight());
        painter.drawLine(frameRect.topRight(), frameRect.bottomLeft());
        return;
    }
    if (m_gridKind == 3) {
        // Squares, a twelfth of half the height each, counted out from the middle.
        double const cell = h / 24.0;
        for (int i = 0; w / 2 + i * cell <= w + 1e-6; ++i) {
            painter.setPen(i == 0 ? strong : thin);
            painter.drawLine(QPointF(w / 2 + i * cell, 0), QPointF(w / 2 + i * cell, h));
            if (i > 0)
                painter.drawLine(QPointF(w / 2 - i * cell, 0), QPointF(w / 2 - i * cell, h));
        }
        for (int i = 1; i < 24; ++i) {
            painter.setPen(i == 12 ? strong : thin);
            painter.drawLine(QPointF(0, h * i / 24.0), QPointF(w, h * i / 24.0));
        }
        return;
    }
    int const across = m_gridKind == 0 ? 2 * m_fieldsAcross : 32, down = m_gridKind == 1 ? 32 : m_gridKind == 0 ? 2 * m_fieldsDown : 24;
    for (int i = 1; i < across; ++i) {
        painter.setPen(i == across / 2 ? strong : thin);
        painter.drawLine(QPointF(w * i / across, 0), QPointF(w * i / across, h));
    }
    for (int i = 1; i < down; ++i) {
        painter.setPen(i == down / 2 ? strong : thin);
        painter.drawLine(QPointF(0, h * i / down), QPointF(w, h * i / down));
    }
    painter.setPen(thin);
    painter.drawLine(frameRect.topLeft(), frameRect.bottomRight());
    painter.drawLine(frameRect.topRight(), frameRect.bottomLeft());
}

void Canvas::onActionShowSafeArea(bool on)
{
    m_safeArea = on;
    update();
}

void Canvas::onActionProportionGrid(bool on)
{
    m_proportionGrid = on;
    update();
}

QPointF Canvas::snapped(QPointF p) const
{
    if (!m_snapping)
        return p;       // the master switch is off
    Drawing const* const d = currentDrawing();
    if (m_showGuides && !m_document->scene().guides.isEmpty() && m_layer >= 0 && m_layer < m_document->scene().layers.size()) {
        // Onto a guide that is near: across to one that runs down, up or down to one that runs across - or both.
        Scene const& scene = m_document->scene();
        QTransform const toPicture = m_drawingView ? QTransform() : worldOf(m_layer, m_frame) * scene.cameraTransform(m_frame);
        QPointF q = toPicture.map(p);
        double nearX = 8.0 / std::max(m_zoom, 1e-6), nearY = nearX;
        bool moved = false;
        QPointF const from = q;
        for (Scene::Guide const& guide : scene.guides) {
            if (guide.kind != Scene::Guide::Line)
                continue;       // one to draw along, not to snap to
            double const off = std::abs((guide.vertical ? from.x() : from.y()) - guide.at);
            if (guide.vertical && off < nearX) {
                nearX = off;
                q.setX(guide.at);
                moved = true;
            } else if (!guide.vertical && off < nearY) {
                nearY = off;
                q.setY(guide.at);
                moved = true;
            }
        }
        if (moved && toPicture.isInvertible())
            return toPicture.inverted().map(q);
    }
    if (m_snapGrid && m_layer >= 0 && m_layer < m_document->scene().layers.size()) {
        // The crossings of the grid lie on the picture; the point is on the layer.
        Scene const& scene = m_document->scene();
        QTransform const toPicture = m_drawingView ? QTransform() : worldOf(m_layer, m_frame) * scene.cameraTransform(m_frame);
        QPointF const q = toPicture.map(p);
        double const w = scene.width, h = scene.height;
        double stepX = w / 24.0, stepY = h / 24.0, originX = 0.0;
        if (m_gridOutline) {
            stepX = w / 2.0;
            stepY = h / 2.0;
        } else if (m_gridKind == 3) {
            stepX = stepY = h / 24.0;       // squares, counted out from the middle
            originX = w / 2.0;
        } else {
            stepX = w / (m_gridKind == 0 ? 2.0 * m_fieldsAcross : 32.0);
            stepY = h / (m_gridKind == 1 ? 32.0 : m_gridKind == 0 ? 2.0 * m_fieldsDown : 24.0);
        }
        QPointF const crossing(originX + std::round((q.x() - originX) / stepX) * stepX, std::round(q.y() / stepY) * stepY);
        if (QLineF(crossing, q).length() * m_zoom <= 8.0 && toPicture.isInvertible())
            return toPicture.inverted().map(crossing);
    }
    if (!m_snapContour || !d)
        return p;
    Stroke const* const mine = contourLine(*d);
    QPointF best = p;
    double nearest = 8.0 / m_zoom;
    auto look = [&](Stroke const& line) {
        if (&line == mine)
            return;     // not onto the curve it is a point of
        for (StrokePoint const& point : line.points) {
            double const distance = QLineF(point.pos, p).length();
            if (distance < nearest) {
                nearest = distance;
                best = point.pos;
            }
        }
    };
    for (Stroke const& stroke : d->strokes)
        look(stroke);
    for (Fill const& fill : d->fills)
        for (Stroke const& contour : fill.contours)
            look(contour);
    if (best != p)
        return best;
    // No point near: the nearest place along another line, a little closer in.
    nearest = 6.0 / m_zoom;
    auto along = [&](Stroke const& line) {
        if (&line == mine)
            return;
        QVector<StrokePoint> const run = line.path();
        int const pieces = int(run.size()) - 1 + (line.closed && run.size() > 2 ? 1 : 0);
        for (int i = 0; i < pieces; ++i) {
            QPointF const a = run[i].pos, ab = run[(i + 1) % run.size()].pos - a;
            double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double const t = len2 > 0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
            QPointF const on = a + ab * t;
            double const distance = QLineF(on, p).length();
            if (distance < nearest) {
                nearest = distance;
                best = on;
            }
        }
    };
    for (Stroke const& stroke : d->strokes)
        along(stroke);
    for (Fill const& fill : d->fills)
        for (Stroke const& contour : fill.contours)
            along(contour);
    return best;
}
