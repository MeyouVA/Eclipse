// Eclipse - the Camera view: the Morphing tool, which places the hints of a morphing.
#include "ui/canvas/Canvas.h"

#include <QPainter>

#include "model/document/Commands.h"
#include "model/geometry/Morph.h"

QVector<QPair<int, bool>> Canvas::morphsHere() const
{
    QVector<QPair<int, bool>> found;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings())
        return found;
    Layer const& layer = scene.layers[m_layer];
    for (int i = 0; i < layer.morphs.size(); ++i) {
        if (layer.morphs[i].from == m_frame)
            found.push_back({i, false});
        else if (layer.morphEnd(i) == m_frame)
            found.push_back({i, true});
    }
    return found;
}

int Canvas::morphHere() const
{
    QVector<QPair<int, bool>> const here = morphsHere();
    return here.isEmpty() ? -1 : here.first().first;
}

void Canvas::setMorphGroup(int group)
{
    m_morphGroup = group;
    update();
}

int Canvas::morphShapeAt(QPointF drawingPos) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return -1;
    Layer const& layer = scene.layers[m_layer];
    int const index = layer.drawingAt(m_frame);
    if (index == Layer::kNoDrawing || index < 0 || index >= layer.drawings.size())
        return -1;
    Drawing const& d = layer.drawings[index];
    // From the sheet on top down: a line near the place (within its thickness, and a little more), else an area it is in.
    double const slack = 4.0 / std::max(0.05, m_zoom);
    for (int sheet = Drawing::SheetCount - 1; sheet >= 0; --sheet) {
        QVector<Stroke> const& strokes = d.strokesOf(sheet);
        for (int i = int(strokes.size()) - 1; i >= 0; --i)
            for (int k = 0; k + 1 < strokes[i].points.size(); ++k)
                if (distanceToSegment(drawingPos, strokes[i].points[k].pos, strokes[i].points[k + 1].pos) <= strokes[i].width / 2.0 + slack)
                    return Morphing::shapeKey(sheet, true, i);
        QVector<Fill> const& fills = d.fillsOf(sheet);
        for (int i = int(fills.size()) - 1; i >= 0; --i)
            if (fills[i].contains(drawingPos))
                return Morphing::shapeKey(sheet, false, i);
    }
    return -1;
}

QVector<QPointF> Canvas::morphHintsInView() const
{
    QVector<QPointF> places;
    if (m_tool != Morphing)
        return places;
    Layer const& layer = m_document->scene().layers[m_layer];
    QTransform const view = layerViewTransform();
    for (QPair<int, bool> const& here : morphsHere()) {
        QVector<QLineF> const& hints = layer.morphs[here.first].hints;
        for (int k = 0; k < hints.size(); ++k) {
            bool const held = m_drag == DragHint && m_hintMorph == here.first && m_hintIndex == k && m_hintFar == here.second;
            places.push_back(view.map(held ? m_hintNow : here.second ? hints[k].p2() : hints[k].p1()));
        }
    }
    return places;
}

void Canvas::hintDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers)
{
    QVector<QPair<int, bool>> const here = morphsHere();
    if (here.isEmpty()) {
        emit message(tr("Go to the first or the last drawing of a morphing to place its hints."));
        return;
    }
    Layer const& layer = m_document->scene().layers[m_layer];
    if (modifiers & Qt::ControlModifier) {
        // Ctrl+click: the shape put in the morphing layer chosen in Tool Properties, or taken out of it.
        int const morph = here.first().first;
        bool const far = here.first().second;
        QVector<Morph::Group> groups = layer.morphs[morph].groups;
        if (m_morphGroup < 0 || m_morphGroup >= groups.size()) {
            emit message(tr("Add a morphing layer in Tool Properties first; Ctrl+click then puts shapes in it."));
            return;
        }
        int const shape = morphShapeAt(p);
        if (shape < 0)
            return;
        QVector<int>& shapes = far ? groups[m_morphGroup].second : groups[m_morphGroup].first;
        bool const had = shapes.contains(shape);
        if (had) {
            shapes.removeAll(shape);
        } else {
            // (A shape is in one morphing layer at most.)
            for (Morph::Group& g : groups)
                (far ? g.second : g.first).removeAll(shape);
            shapes.push_back(shape);
        }
        m_document->setMorphGroups(m_layer, morph, groups, had ? tr("Take Out of Morphing Layer") : tr("Put in Morphing Layer"));
        update();
        return;
    }
    QTransform const view = layerViewTransform();
    // On a hint: it is taken hold of - or, with Alt, taken away.
    for (QPair<int, bool> const& one : here) {
        QVector<QLineF> const hints = layer.morphs[one.first].hints;
        for (int k = int(hints.size()) - 1; k >= 0; --k) {
            QPointF const at = view.map(one.second ? hints[k].p2() : hints[k].p1());
            if (QLineF(at, widgetPos).length() > 8.0)
                continue;
            if (modifiers & Qt::AltModifier) {
                QVector<QLineF> left = hints;
                left.removeAt(k);
                m_document->setMorphHints(m_layer, one.first, left, tr("Remove Morphing Hint"));
                update();
                return;
            }
            m_hintMorph = one.first;
            m_hintIndex = k;
            m_hintFar = one.second;
            m_hintNow = one.second ? hints[k].p2() : hints[k].p1();
            m_drag = DragHint;
            update();
            return;
        }
    }
    // Anywhere else on the first drawing: a new hint, which goes to the same place until it is dragged on the second.
    for (QPair<int, bool> const& one : here) {
        if (one.second)
            continue;
        QVector<QLineF> hints = layer.morphs[one.first].hints;
        hints.push_back(QLineF(p, p));
        m_document->setMorphHints(m_layer, one.first, hints, tr("Add Morphing Hint"));
        update();
        return;
    }
    emit message(tr("New hints are put on the first drawing of the morphing. Here, drag the ones it has to where they are to go."));
}

void Canvas::hintUp()
{
    Scene const& scene = m_document->scene();
    int const morph = m_hintMorph, index = m_hintIndex;
    m_hintMorph = m_hintIndex = -1;
    if (m_layer < 0 || m_layer >= scene.layers.size() || morph < 0 || morph >= scene.layers[m_layer].morphs.size())
        return;
    QVector<QLineF> hints = scene.layers[m_layer].morphs[morph].hints;
    if (index < 0 || index >= hints.size())
        return;
    if (m_hintFar)
        hints[index].setP2(m_hintNow);
    else
        hints[index].setP1(m_hintNow);
    m_document->setMorphHints(m_layer, morph, hints, tr("Move Morphing Hint"));
    update();
}

void Canvas::paintMorphHints(QPainter& painter)
{
    if (m_tool != Morphing || m_playing)
        return;
    QVector<QPair<int, bool>> const here = morphsHere();
    if (here.isEmpty())
        return;
    Layer const& layer = m_document->scene().layers[m_layer];
    QTransform const view = layerViewTransform();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont small = painter.font();
    small.setPixelSize(10);
    small.setBold(true);
    painter.setFont(small);
    // The shapes of the morphing layer chosen, outlined.
    if (int const index = layer.drawingAt(m_frame); index >= 0 && index < layer.drawings.size()) {
        Drawing const& d = layer.drawings[index];
        for (QPair<int, bool> const& one : here) {
            QVector<Morph::Group> const& groups = layer.morphs[one.first].groups;
            if (m_morphGroup < 0 || m_morphGroup >= groups.size())
                continue;
            painter.setPen(QPen(QColor(80, 220, 255), 2.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            for (int key : one.second ? groups[m_morphGroup].second : groups[m_morphGroup].first) {
                int const sheet = (key >> 24) / 2, i = key & 0xffffff;
                bool const line = (key >> 24) % 2 == 1;
                if (sheet < 0 || sheet >= Drawing::SheetCount)
                    continue;
                if (line && i < d.strokesOf(sheet).size()) {
                    QPolygonF path;
                    for (StrokePoint const& sp : d.strokesOf(sheet)[i].points)
                        path << view.map(sp.pos);
                    painter.drawPolyline(path);
                } else if (!line && i < d.fillsOf(sheet).size()) {
                    for (QPolygonF const& outline : d.fillsOf(sheet)[i].outlines)
                        painter.drawPolygon(view.map(outline));
                }
            }
        }
    }
    for (QPair<int, bool> const& one : here) {
        QVector<QLineF> const& hints = layer.morphs[one.first].hints;
        for (int k = 0; k < hints.size(); ++k) {
            bool const held = m_drag == DragHint && m_hintMorph == one.first && m_hintIndex == k && m_hintFar == one.second;
            QPointF const at = view.map(held ? m_hintNow : one.second ? hints[k].p2() : hints[k].p1());
            // Green where it starts from, orange where it goes to; numbered, so that the two ends can be told.
            painter.setPen(QPen(QColor(20, 20, 20), 1.2));
            painter.setBrush(one.second ? QColor(240, 150, 60) : QColor(90, 200, 110));
            painter.drawEllipse(at, 7.0, 7.0);
            painter.drawText(QRectF(at.x() - 7.0, at.y() - 7.0, 14.0, 14.0), Qt::AlignCenter, QString::number(k + 1));
        }
    }
    painter.restore();
}
