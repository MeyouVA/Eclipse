// Eclipse - the Camera view: the Contour Editor.
#include "ui/canvas/Canvas.h"
#include <QLineF>
#include <algorithm>

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

// ---- the controls: a box round the points picked, to scale and turn them ------------------------------------------------

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

// ---- points dropped on points ------------------------------------------------------------------------------------------

QVector<QPair<int, int>> Canvas::sharedPoints(Drawing const& d) const
{
    // The other points of the joint of the point in hand (one point with it): not those picked with it.
    QVector<QPair<int, int>> shared;
    Stroke const* const mine = contourLine(d);
    if (!mine || m_ceFill >= 0 || m_cePoint < 0 || m_cePoint >= mine->points.size())
        return shared;
    int const joint = mine->points[m_cePoint].joint;
    if (joint == 0)
        return shared;
    QVector<int> const picked = contourPointsPicked();
    for (int s = 0; s < d.strokes.size(); ++s)
        for (int k = 0; k < d.strokes[s].points.size(); ++k) {
            bool const own = &d.strokes[s] == mine;
            if (own && (k == m_cePoint || picked.contains(k)))
                continue;
            if (d.strokes[s].points[k].joint == joint)
                shared.push_back({s, k});
        }
    return shared;
}

QPointF Canvas::pointMagnet(QPointF p) const
{
    // Near enough another point - a few pixels of the screen - the point in hand goes onto it.
    Drawing const* const d = currentDrawing();
    Stroke const* const mine = d ? contourLine(*d) : nullptr;
    if (!m_snapping || !mine || m_cePoint < 0 || m_cePoint >= mine->points.size())
        return p;
    int const joint = m_ceFill < 0 ? mine->points[m_cePoint].joint : 0;
    QVector<int> const picked = contourPointsPicked();
    double nearest = 6.0 / std::max(m_zoom, 1e-6);
    QPointF best = p;
    auto look = [&](Stroke const& line) {
        for (int k = 0; k < line.points.size(); ++k) {
            QPointF const at = line.points[k].pos;
            if (&line == mine && (k == m_cePoint || picked.contains(k)))
                continue;
            if (joint != 0 && line.points[k].joint == joint)
                continue;       // one point with the point in hand: it goes along with it
            double const distance = QLineF(at, p).length();
            if (distance < nearest) {
                nearest = distance;
                best = at;
            }
        }
    };
    for (Stroke const& line : d->strokes)
        look(line);
    for (Fill const& fill : d->fills)
        for (Stroke const& contour : fill.contours)
            look(contour);
    return best;
}

Stroke Canvas::reversedLine(Stroke line)
{
    line.reverse();
    return line;
}

Stroke Canvas::startedFrom(Stroke const& look, Stroke const& before)
{
    // The look (colour, width, texture...) of the one, the course and the thickness of the other.
    Stroke joined = look;
    joined.curve = before.curve;
    joined.closed = false;
    joined.points = before.points;
    joined.thickness = before.thickness;
    joined.capStart = before.capStart;
    joined.capEnd = before.capEnd;
    if (before.width > 0.0f && joined.width > 0.0f && std::abs(before.width - joined.width) > 1e-6f) {
        // Its thickness against the width it now has.
        joined.ownThickness();
        double const scale = double(before.width) / joined.width;
        for (ThicknessPoint& key : joined.thickness) {
            key.left = float(key.left * scale);
            key.right = float(key.right * scale);
        }
    }
    return joined;
}

void Canvas::makeCurve(Stroke& line)
{
    if (line.curve)
        return;
    line.curve = true;
    for (StrokePoint& point : line.points) {
        point.handles = true;
        point.corner = true;
        point.in = point.out = QPointF();
    }
}

bool Canvas::joinDroppedEnd(Drawing& edited)
{
    // Only a line's end, dropped exactly on a point (the magnet put it there).
    if (m_ceStroke < 0 || m_ceFill >= 0 || m_ceStroke >= edited.strokes.size() || !contourPointsPicked().isEmpty())
        return false;
    Stroke const line = edited.strokes[m_ceStroke];
    int const n = int(line.points.size());
    bool const atEnd = m_cePoint == n - 1, atStart = m_cePoint == 0;
    if (line.closed || n < 2 || (!atEnd && !atStart))
        return false;
    QPointF const at = line.points[m_cePoint].pos;
    // Onto its own other end: the line closes there.
    int const otherEnd = atEnd ? 0 : n - 1;
    if (n >= 3 && QLineF(line.points[otherEnd].pos, at).length() < 1e-6) {
        Stroke closed = line;
        closed.freezeHandles();     // (the curve as it is: closed, its points' neighbours go round)
        StrokePoint const dropped = closed.points.takeAt(m_cePoint);
        if (atEnd)
            closed.points.first().in = dropped.in;
        else
            closed.points.last().out = dropped.out;
        closed.closed = true;
        // (Its pieces are the pieces it had: dropped at its start, the first of them is now the last, the one that closes it.)
        if (!atEnd && !closed.thickness.isEmpty()) {
            double const span = closed.pieces();
            for (ThicknessPoint& key : closed.thickness)
                key.at = key.at < 1.0 ? key.at + span - 1.0 : key.at - 1.0;
            std::stable_sort(closed.thickness.begin(), closed.thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
        }
        edited.strokes[m_ceStroke] = closed;
        m_cePoint = atEnd ? 0 : int(closed.points.size()) - 1;
        return true;
    }
    // Onto an end of another line: one line, through that point.
    for (int s = 0; s < edited.strokes.size(); ++s) {
        Stroke other = edited.strokes[s];
        if (s == m_ceStroke || other.closed || other.points.size() < 2)
            continue;
        // (A point joined to the one dropped went along with it: it was not dropped on.)
        int const joint = line.points[m_cePoint].joint;
        bool const toFirst = QLineF(other.points.first().pos, at).length() < 1e-6 && (joint == 0 || other.points.first().joint != joint);
        bool const toLast = !toFirst && QLineF(other.points.last().pos, at).length() < 1e-6 && (joint == 0 || other.points.last().joint != joint);
        if (!toFirst && !toLast)
            continue;
        // First the part that comes before the meeting point, then the part after it.
        Stroke before = atEnd ? line : (toLast ? other : reversedLine(other));
        Stroke after = atEnd ? (toFirst ? other : reversedLine(other)) : line;
        if (before.curve || after.curve) {
            makeCurve(before);
            makeCurve(after);
        }
        Stroke joined = startedFrom(line, before);
        joined.append(after, true);
        int const meeting = int(before.points.size()) - 1;
        int const keep = std::min(s, m_ceStroke), drop = std::max(s, m_ceStroke);
        edited.strokes[keep] = joined;
        edited.strokes.removeAt(drop);
        m_ceStroke = keep;
        m_cePoint = meeting;
        return true;
    }
    return false;
}

QVector<QPair<int, int>> Canvas::stackedPoints(Drawing const& d) const
{
    if (!contourLine(d) || m_cePoint < 0 || m_cePoint >= contourLine(d)->points.size())
        return {};
    if (m_ceFill >= 0)
        return {{-1, m_cePoint}};
    return QVector<QPair<int, int>>{{m_ceStroke, m_cePoint}} + sharedPoints(d);
}

QPointF Canvas::stackedHandleKnob(Drawing const& d, int stroke, int point, int handle) const
{
    Stroke const* const line = stroke < 0 || m_ceFill >= 0 ? contourLine(d) : (stroke < d.strokes.size() ? &d.strokes[stroke] : nullptr);
    if (!line || point < 0 || point >= line->points.size())
        return QPointF();
    QPointF in, out;
    line->handlesAt(point, in, out);
    QPointF const reach = handle == 0 ? in : out;
    return reach.isNull() ? QPointF() : layerViewTransform().map(line->points[point].pos + reach);
}

Stroke* Canvas::handleLine(Drawing& d, int& point) const
{
    if (m_ceHandleStroke >= 0 && m_ceFill < 0 && m_ceHandleStroke < d.strokes.size()
        && m_ceHandlePoint >= 0 && m_ceHandlePoint < d.strokes[m_ceHandleStroke].points.size()) {
        point = m_ceHandlePoint;
        return &d.strokes[m_ceHandleStroke];
    }
    point = m_cePoint;
    return contourLine(d);
}

void Canvas::mergeJoints(Drawing& drawing, int into, int from)
{
    if (into == from || from == 0)
        return;
    for (Stroke& line : drawing.strokes)
        for (StrokePoint& point : line.points)
            if (point.joint == from)
                point.joint = into;
}

bool Canvas::jointDroppedPoint(Drawing& edited)
{
    if (m_ceStroke < 0 || m_ceFill >= 0 || m_ceStroke >= edited.strokes.size() || !contourPointsPicked().isEmpty()
        || m_cePoint < 0 || m_cePoint >= edited.strokes[m_ceStroke].points.size())
        return false;
    QPointF const at = edited.strokes[m_ceStroke].points[m_cePoint].pos;
    auto join = [&](int stroke, int point) {
        int const mine = edited.strokes[m_ceStroke].points[m_cePoint].joint, theirs = edited.strokes[stroke].points[point].joint;
        if (mine != 0 && theirs != 0) {
            mergeJoints(edited, mine, theirs);
        } else {
            int const joint = mine != 0 ? mine : theirs != 0 ? theirs : edited.nextJoint();
            edited.strokes[m_ceStroke].points[m_cePoint].joint = joint;
            edited.strokes[stroke].points[point].joint = joint;
        }
    };
    // Onto a point (of another line, or of this one away from its neighbours).
    int const mineJoint = edited.strokes[m_ceStroke].points[m_cePoint].joint;
    for (int s = 0; s < edited.strokes.size(); ++s)
        for (int k = 0; k < edited.strokes[s].points.size(); ++k) {
            if (s == m_ceStroke && std::abs(k - m_cePoint) <= 1)
                continue;
            StrokePoint const& other = edited.strokes[s].points[k];
            if ((mineJoint != 0 && other.joint == mineJoint) || QLineF(other.pos, at).length() > 1e-6)
                continue;
            join(s, k);
            return true;
        }
    // Onto a line (where Snap to Contour put it): a point there, joined.
    for (int s = 0; s < edited.strokes.size(); ++s) {
        if (s == m_ceStroke)
            continue;
        // (A line with a point there already - one joined to this one, or lying on it - needs no new point.)
        bool already = false;
        for (StrokePoint const& point : std::as_const(edited.strokes[s].points))
            already = already || (mineJoint != 0 && point.joint == mineJoint) || QLineF(point.pos, at).length() < 1e-6;
        if (already)
            continue;
        QVector<StrokePoint> const run = edited.strokes[s].path();
        int const pieces = int(run.size()) - 1 + (edited.strokes[s].closed && run.size() > 2 ? 1 : 0);
        double nearest = 1e300;
        for (int i = 0; i < pieces; ++i) {
            QPointF const a = run[i].pos, ab = run[(i + 1) % run.size()].pos - a;
            double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double const t = len2 > 0 ? std::clamp(((at.x() - a.x()) * ab.x() + (at.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
            nearest = std::min(nearest, QLineF(at, a + ab * t).length());
        }
        if (nearest > 1e-3)
            continue;
        int const made = edited.strokes[s].insertPoint(at);
        if (made < 0)
            continue;
        edited.strokes[s].points[made].pos = at;
        join(s, made);
        return true;
    }
    return false;
}

void Canvas::onActionBreakJoint()
{
    Drawing const* const d = currentDrawing();
    if (!d || m_tool != ContourEditor || m_ceStroke < 0 || m_ceFill >= 0 || m_ceStroke >= d->strokes.size() || m_cePoint < 0
        || m_cePoint >= d->strokes[m_ceStroke].points.size() || d->strokes[m_ceStroke].points[m_cePoint].joint == 0) {
        emit message(tr("Pick a joint with the Contour Editor first (a point with a ring round it)."));
        return;
    }
    Drawing edited = *d;
    edited.strokes[m_ceStroke].points[m_cePoint].joint = 0;
    int const stroke = m_ceStroke, point = m_cePoint;
    commitDrawing(edited, tr("Break Joint"));
    m_ceStroke = stroke;
    m_cePoint = point;
    update();
}

// ---- a line bent by dragging it between two of its points ---------------------------------------------------------------

namespace {

// The four points of the curve from point `segment` to the next (its two ends and the two handles between).
bool pieceOf(Stroke const& line, int segment, QPointF& p0, QPointF& p1, QPointF& p2, QPointF& p3)
{
    int const n = int(line.points.size());
    bool const loop = line.closed && n >= 2;
    if (segment < 0 || n < 2 || segment >= (loop ? n : n - 1))
        return false;
    int const next = (segment + 1) % n;
    QPointF in, out, unused;
    line.handlesAt(segment, unused, out);
    line.handlesAt(next, in, unused);
    p0 = line.points[segment].pos;
    p3 = line.points[next].pos;
    p1 = p0 + out;
    p2 = p3 + in;
    return true;
}

QPointF cubicAt(QPointF p0, QPointF p1, QPointF p2, QPointF p3, double t)
{
    double const u = 1.0 - t;
    return p0 * (u * u * u) + p1 * (3.0 * u * u * t) + p2 * (3.0 * u * t * t) + p3 * (t * t * t);
}

} // namespace

bool Canvas::segmentAt(Stroke const& line, QPointF p, double reach, int& segment, double& t)
{
    int const n = int(line.points.size());
    int const pieces = line.closed && n >= 2 ? n : n - 1;
    double best = reach;
    bool found = false;
    for (int s = 0; s < pieces; ++s) {
        QPointF p0, p1, p2, p3;
        if (!pieceOf(line, s, p0, p1, p2, p3))
            continue;
        for (int k = 1; k < 64; ++k) {
            double const at = k / 64.0;
            double const d = QLineF(cubicAt(p0, p1, p2, p3, at), p) .length();
            if (d <= best) {
                best = d;
                segment = s;
                t = at;
                found = true;
            }
        }
    }
    return found;
}

Stroke Canvas::bentSegment(Stroke line, int segment, double t, QPointF by)
{
    int const n = int(line.points.size());
    QPointF p0, p1, p2, p3;
    if (!pieceOf(line, segment, p0, p1, p2, p3) || t <= 0.0 || t >= 1.0)
        return line;
    // Every point keeps the curve it has now (its handles made its own), so that only this piece changes.
    QVector<QPair<QPointF, QPointF>> kept;
    for (int i = 0; i < n; ++i) {
        QPointF in, out;
        line.handlesAt(i, in, out);
        kept.push_back({in, out});
    }
    bool const wasCurve = line.curve;
    for (int i = 0; i < n; ++i) {
        bool const corner = !wasCurve || line.points[i].corner;
        line.setHandles(i, kept[i].first, kept[i].second);
        line.points[i].corner = corner;
    }
    double weight;
    if (t <= 1.0 / 6.0)
        weight = 0.0;
    else if (t <= 0.5)
        weight = std::pow((6.0 * t - 1.0) / 2.0, 3.0) / 2.0;
    else if (t <= 5.0 / 6.0)
        weight = (1.0 - std::pow((6.0 * (1.0 - t) - 1.0) / 2.0, 3.0)) / 2.0 + 0.5;
    else
        weight = 1.0;
    QPointF const first = by * ((1.0 - weight) / (3.0 * t * (1.0 - t) * (1.0 - t)));
    QPointF const second = by * (weight / (3.0 * t * t * (1.0 - t)));
    int const next = (segment + 1) % n;
    StrokePoint& a = line.points[segment];
    StrokePoint& b = line.points[next];
    a.out += first;
    b.in += second;
    a.corner = true;
    b.corner = true;
    return line;
}

bool Canvas::startBend(Stroke const& line, QPointF p, Qt::KeyboardModifiers modifiers)
{
    int segment = -1;
    double t = 0.0;
    if ((modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) || !segmentAt(line, p, 5.0 / std::max(m_zoom, 1e-6), segment, t))
        return false;
    m_segIndex = segment;
    m_segT = t;
    m_dragStart = m_dragNow = p;
    m_drag = DragSegment;
    return true;
}
