// Eclipse - the Camera view: the Contour Editor.
#include "ui/canvas/Canvas.h"

#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

// ---- contour editor -----------------------------------------------------------------------
// Shows the points of one line.  Drag a point to reshape the line, Ctrl-click the line to
// add a point, Delete to take the picked point out.

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
    // Reached through the drawing as something that may be written to: its lists are shared
    // with the scene until then, and must become this drawing's own before a point is moved.
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
    // A handle of the point picked?  (Before anything else: a handle may lie over another point.)
    if (d && contourValid()) {
        for (int handle = 0; handle < 2; ++handle) {
            QPointF const knob = contourHandlePos(handle);
            if (knob.isNull() || std::abs(knob.x() - press.x()) > 6 || std::abs(knob.y() - press.y()) > 6)
                continue;
            m_ceHandle = handle;
            m_ceBreak = modifiers & Qt::AltModifier;
            m_dragStart = m_dragNow = toWidget.inverted().map(knob);
            m_drag = DragHandle;
            return;
        }
    }
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
                    // Alt: dragged, the point's two handles are pulled out of it (in line, as long as each other);
                    // clicked, a corner becomes smooth and a smooth point a corner (when the pen comes up).
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
    }
    for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
        if (!d->strokes[i].hit(p, 4.0 / m_zoom))
            continue;
        if (i == m_ceStroke && (modifiers & Qt::ControlModifier)) {
            // A new point where the line was pressed; it can be dragged at once.
            // The line keeps its shape: the new point lies on it, with the handles that make it so.
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
        return;
    }
    // On nothing.  With a curve being edited, a box can be dragged round some of its points
    // (a click without a drag lets the curve go).
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
    // The handles of the point picked: a line out to a round knob each.
    if (m_cePoint >= 0 && m_cePoint < stroke.points.size()) {
        QPointF in, out;
        stroke.handlesAt(m_cePoint, in, out);
        QPointF const at = stroke.points[m_cePoint].pos;
        for (QPointF const& reach : {in, out}) {
            if (reach.isNull())
                continue;
            QPointF const from = toWidget.map(at), to = toWidget.map(at + reach);
            painter.setPen(QPen(accent, 1.0));
            painter.drawLine(from, to);
            painter.setPen(QPen(QColor(20, 20, 20), 1));
            painter.setBrush(QColor(255, 210, 70));
            painter.drawEllipse(to, 3.5, 3.5);
        }
    }
    for (int i = 0; i < stroke.points.size(); ++i) {
        QPointF const w = toWidget.map(stroke.points[i].pos);
        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(i == m_cePoint || m_ceExtra.contains(i) ? accent : QColor(Qt::white));
        // A square where the line turns sharply, a dot where it bends through.
        if (stroke.isCorner(i))
            painter.drawRect(QRectF(w.x() - 3.5, w.y() - 3.5, 7, 7));
        else
            painter.drawEllipse(w, 4.0, 4.0);
    }
}
