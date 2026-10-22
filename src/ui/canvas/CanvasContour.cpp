// Eclipse - the Camera view's Contour Editor: points, handles, the press, its aids, the controls box.
#include "ui/canvas/Canvas.h"
#include <QLineF>
#include <algorithm>

#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

QPointF Canvas::contourHandlePos(int handle) const
{
    if (!contourValid() || handle < 0 || handle > 1)
        return QPointF();
    Stroke const& stroke = *contourLine(*currentDrawing());
    QPointF in, out;
    stroke.handlesAt(m_cePoint, in, out);
    QPointF const reach = handle == 0 ? in : out;
    return reach.isNull() ? QPointF() : layerViewTransform().map(stroke.points[m_cePoint].pos + reach);
}

Stroke const* Canvas::contourLine(Drawing const& drawing) const
{
    if (m_ceFill >= 0) {
        if (m_ceFill >= drawing.fills.size() || m_ceContour < 0 || m_ceContour >= drawing.fills[m_ceFill].contours.size())
            return nullptr;
        return &drawing.fills[m_ceFill].contours[m_ceContour];
    }
    return m_ceStroke >= 0 && m_ceStroke < drawing.strokes.size() ? &drawing.strokes[m_ceStroke] : nullptr;
}

Stroke* Canvas::contourLine(Drawing& drawing) const
{
    if (!contourLine(std::as_const(drawing)))
        return nullptr;
    if (m_ceFill >= 0)
        return &drawing.fills[m_ceFill].contours[m_ceContour];
    return &drawing.strokes[m_ceStroke];
}

void Canvas::contourEdited(Drawing& drawing) const
{
    if (m_ceFill >= 0 && m_ceFill < drawing.fills.size())
        drawing.fills[m_ceFill].rebuild();
}

bool Canvas::contourValid() const
{
    Drawing const* const d = currentDrawing();
    Stroke const* const line = d ? contourLine(*d) : nullptr;
    return line && m_cePoint >= 0 && m_cePoint < line->points.size();
}

void Canvas::contourDown(QPointF p, Qt::KeyboardModifiers modifiers)
{
    Drawing const* const d = currentDrawing();
    QTransform const toWidget = layerViewTransform();
    QPointF const press = toWidget.map(p);
    // A control of the box round the points picked: they are scaled or turned.
    for (int which = 0; which <= 4; ++which) {
        QPointF const at = contourControlInView(which);
        if (at.isNull() || std::abs(at.x() - press.x()) > 7 || std::abs(at.y() - press.y()) > 7)
            continue;
        m_ceBoxStart = contourControlBox();
        m_ceBoxCorner = which;
        m_dragStart = m_dragNow = which < 4 ? toWidget.inverted().map(at) : p;
        m_drag = DragCeBox;
        return;
    }
    // A handle of the point picked?  (Before anything else: a handle may lie over another point.)
    if (d && contourValid()) {
        // (The handles of the points lying on the picked one too: to the eye they are one point.)
        for (auto const& [stroke, point] : stackedPoints(*d))
            for (int handle = 0; handle < 2; ++handle) {
                QPointF const knob = stackedHandleKnob(*d, stroke, point, handle);
                if (knob.isNull() || std::abs(knob.x() - press.x()) > 6 || std::abs(knob.y() - press.y()) > 6)
                    continue;
                bool const own = m_ceFill >= 0 || (stroke == m_ceStroke && point == m_cePoint);
                m_ceHandleStroke = own ? -1 : stroke;
                m_ceHandlePoint = own ? -1 : point;
                m_ceHandle = handle;
                m_ceBreak = modifiers & Qt::AltModifier;
                m_dragStart = m_dragNow = toWidget.inverted().map(knob);
                m_drag = DragHandle;
                return;
            }
    }
    m_ceHandleStroke = m_ceHandlePoint = -1;
    // The points picked so far: Shift adds to them or takes from them, and a press on one of them drags them all.
    QVector<int> picked = contourPointsPicked();
    if (m_cePoint >= 0)
        picked.push_back(m_cePoint);
    m_ceExtra.clear();
    m_cePoint = -1;
    if (!d) {
        m_ceStroke = m_ceFill = -1;
        return;
    }
    // A point of the curve being edited?
    if (Stroke const* const line = contourLine(*d)) {
        QVector<StrokePoint> const& points = line->points;
        for (int i = 0; i < points.size(); ++i) {
            QPointF const w = toWidget.map(points[i].pos);
            if (std::abs(w.x() - press.x()) <= 6 && std::abs(w.y() - press.y()) <= 6) {
                m_cePoint = i;
                if (modifiers & Qt::AltModifier) {
                    m_ceHandle = 2;
                    m_ceBreak = false;
                    m_dragStart = m_dragNow = points[i].pos;
                    m_drag = DragHandle;
                    return;
                }
                if (modifiers & Qt::ShiftModifier) {
                    if (picked.contains(i))
                        picked.removeAll(i);
                    else
                        picked.push_back(i);
                    m_cePoint = picked.isEmpty() ? -1 : picked.takeLast();
                    m_ceExtra = picked;
                    return;
                }
                if (picked.contains(i)) {
                    picked.removeAll(i);
                    m_ceExtra = picked;
                }
                m_dragNow = points[i].pos;
                m_drag = DragPoint;
                return;
            }
        }
        // On the curve itself, between two points: dragged, it bends there.
        if (startBend(*line, p, modifiers))
            return;
    }
    for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
        if (!d->strokes[i].hit(p, 4.0 / m_zoom))
            continue;
        if (i == m_ceStroke && (modifiers & Qt::ControlModifier)) {
            Drawing edited = *d;
            int const added = edited.strokes[i].insertPoint(p);
            if (added < 0)
                return;
            commitDrawing(edited, tr("Add Point"));
            m_ceStroke = i;
            m_ceFill = -1;
            m_cePoint = added;
            m_dragNow = edited.strokes[i].points[added].pos;
            m_drag = DragPoint;
            return;
        }
        m_ceStroke = i;
        m_ceFill = -1;
        startBend(d->strokes[i], p, modifiers);
        return;
    }
    // A brush shape?  Pressed on its edge or inside it: the contour nearest the press.
    for (int i = int(d->fills.size()) - 1; i >= 0; --i) {
        Fill const& shape = d->fills[i];
        if (shape.contours.isEmpty() || !shape.hit(p, 4.0 / m_zoom))
            continue;
        int nearest = 0;
        double nearestDistance = 1e300;
        for (int c = 0; c < shape.contours.size(); ++c) {
            QVector<StrokePoint> const edge = shape.contours[c].path();
            for (int k = 0; k + 1 < edge.size(); ++k) {
                QPointF const a = edge[k].pos, ab = edge[k + 1].pos - a;
                double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
                double const t = len2 > 0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
                double const distance = QLineF(p, a + ab * t).length();
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = c;
                }
            }
        }
        if (i == m_ceFill && nearest == m_ceContour && (modifiers & Qt::ControlModifier) && nearestDistance <= 6.0 / m_zoom) {
            // A new point on the contour; the shape keeps its outline.
            Drawing edited = *d;
            int const added = edited.fills[i].contours[nearest].insertPoint(p);
            if (added < 0)
                return;
            edited.fills[i].rebuild();
            commitDrawing(edited, tr("Add Point"));
            m_ceStroke = -1;
            m_ceFill = i;
            m_ceContour = nearest;
            m_cePoint = added;
            m_dragNow = edited.fills[i].contours[nearest].points[added].pos;
            m_drag = DragPoint;
            return;
        }
        m_ceStroke = -1;
        m_ceFill = i;
        m_ceContour = nearest;
        startBend(shape.contours[nearest], p, modifiers);
        return;
    }
    if (contourLine(*d)) {
        m_drag = DragPointMarquee;
        m_dragStart = m_dragNow = p;
        return;
    }
    m_ceStroke = m_ceFill = -1;
}

void Canvas::paintContourAids(QPainter& painter)
{
    Drawing const* const current = currentDrawing();
    if (!current || !contourLine(*current))
        return;
    Drawing const shown = previewDrawing();
    if (!contourLine(shown))
        return;
    Stroke const& stroke = *contourLine(shown);
    QTransform const toWidget = layerViewTransform();
    QColor const accent(64, 156, 255);
    painter.resetTransform();
    // The line itself, thin, so that its middle shows whatever its thickness.
    QPolygonF line;
    for (StrokePoint const& p : stroke.path())
        line.push_back(toWidget.map(p.pos));
    painter.setPen(QPen(accent, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(line);
    // The handles of the point picked, and of the points lying on it: a line out to a round knob each.
    if (m_cePoint >= 0 && m_cePoint < stroke.points.size()) {
        for (auto const& [index, point] : stackedPoints(shown))
            for (int handle = 0; handle < 2; ++handle) {
                QPointF const to = stackedHandleKnob(shown, index, point, handle);
                if (to.isNull())
                    continue;
                Stroke const& owner = index < 0 || m_ceFill >= 0 ? stroke : shown.strokes[index];
                painter.setPen(QPen(accent, 1.0));
                painter.drawLine(toWidget.map(owner.points[point].pos), to);
                painter.setPen(QPen(QColor(20, 20, 20), 1));
                painter.setBrush(QColor(255, 210, 70));
                painter.drawEllipse(to, 3.5, 3.5);
            }
    }
    for (int i = 0; i < stroke.points.size(); ++i) {
        QPointF const w = toWidget.map(stroke.points[i].pos);
        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(i == m_cePoint || m_ceExtra.contains(i) ? accent : QColor(Qt::white));
        // A joint (one point of several lines): a ring round it.
        if (stroke.points[i].joint != 0) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 210, 70), 1.5));
            painter.drawEllipse(w, 7.0, 7.0);
            painter.setPen(QPen(QColor(20, 20, 20), 1));
            painter.setBrush(i == m_cePoint || m_ceExtra.contains(i) ? accent : QColor(Qt::white));
        }
        // A square where the line turns sharply, a dot where it bends through.
        if (stroke.isCorner(i))
            painter.drawRect(QRectF(w.x() - 3.5, w.y() - 3.5, 7, 7));
        else
            painter.drawEllipse(w, 4.0, 4.0);
    }
    // The controls: a box round the points picked, its corners and its knob.
    QRectF const box = contourControlBox();
    if (!box.isNull()) {
        QTransform const moved = m_drag == DragCeBox ? contourBoxTransform() : QTransform();
        QPolygonF const outline = toWidget.map(moved.map(QPolygonF(box)));
        QPen dashed(accent, 1.0);
        dashed.setStyle(Qt::DashLine);
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(outline);
        painter.setPen(QPen(accent));
        painter.setBrush(Qt::white);
        for (int corner = 0; corner < 4; ++corner) {
            QPointF const c = outline[corner];
            painter.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
        }
        QPointF const top = (outline[0] + outline[1]) / 2.0;
        QPointF const knob = m_drag == DragCeBox ? toWidget.map(moved.map(toWidget.inverted().map(contourControlInView(4)))) : contourControlInView(4);
        painter.drawLine(top, knob);
        painter.drawEllipse(knob, 5, 5);
    }
}

QRectF Canvas::contourControlBox() const
{
    Drawing const* const d = currentDrawing();
    Stroke const* const line = d ? contourLine(*d) : nullptr;
    if (!m_ceControls || m_tool != ContourEditor || !line || m_cePoint < 0 || m_cePoint >= line->points.size())
        return QRectF();
    QVector<int> const picked = contourPointsPicked();
    if (picked.isEmpty())
        return QRectF();        // one point: there is nothing to scale or turn
    // (Points have no size, and a rectangle of none is left out of a union: the corners are found by hand.)
    QPointF least = line->points[m_cePoint].pos, most = least;
    for (int i : picked) {
        QPointF const p = line->points[i].pos;
        least = QPointF(std::min(least.x(), p.x()), std::min(least.y(), p.y()));
        most = QPointF(std::max(most.x(), p.x()), std::max(most.y(), p.y()));
    }
    QRectF const box(least, most);
    if (box.width() * m_zoom < 2.0 && box.height() * m_zoom < 2.0)
        return QRectF();        // all in one place
    // A little room round the points, so that the corners do not lie on them.
    double const room = 6.0 / std::max(m_zoom, 1e-6);
    return box.adjusted(-room, -room, room, room);
}

QPointF Canvas::contourControlInView(int which) const
{
    QRectF const box = contourControlBox();
    if (box.isNull() || which < 0 || which > 4)
        return QPointF();
    QTransform const view = layerViewTransform();
    if (which < 4) {
        QPointF const corners[4] = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
        return view.map(corners[which]);
    }
    // The knob: a little way out over the middle of the top, on the screen.
    QPointF const top = view.map(QPointF(box.center().x(), box.top()));
    QPointF const middle = view.map(box.center());
    QPointF way = top - middle;
    double const length = std::hypot(way.x(), way.y());
    way = length > 1e-9 ? way / length : QPointF(0.0, -1.0);
    return top + way * 22.0;
}

QTransform Canvas::contourBoxTransform() const
{
    QRectF const box = m_ceBoxStart;
    if (box.isNull())
        return QTransform();
    if (m_ceBoxCorner == 4) {
        // Turned about its middle, as far as the pen has gone round it.
        QPointF const c = box.center();
        double const from = std::atan2(m_dragStart.y() - c.y(), m_dragStart.x() - c.x());
        double const to = std::atan2(m_dragNow.y() - c.y(), m_dragNow.x() - c.x());
        QTransform turn;
        turn.rotateRadians(to - from);
        return QTransform::fromTranslate(-c.x(), -c.y()) * turn * QTransform::fromTranslate(c.x(), c.y());
    }
    double const room = 6.0 / std::max(m_zoom, 1e-6);
    QRectF const tight = box.adjusted(room, room, -room, -room);
    QPointF const corners[4] = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    QPointF const tightCorners[4] = {tight.topLeft(), tight.topRight(), tight.bottomRight(), tight.bottomLeft()};
    QPointF const anchor = tightCorners[(m_ceBoxCorner + 2) % 4];
    QPointF const from = corners[m_ceBoxCorner] - anchor;
    QPointF const to = m_dragNow - anchor;
    double const sx = std::abs(from.x()) > 1e-9 ? to.x() / from.x() : 1.0;
    double const sy = std::abs(from.y()) > 1e-9 ? to.y() / from.y() : 1.0;
    return QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(sx, sy) * QTransform::fromTranslate(anchor.x(), anchor.y());
}
