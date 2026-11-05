// Eclipse - the Camera view: the Morphing tool, which places the hints of a morphing.
#include "canvas/Canvas.h"

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

void Canvas::setBrushOutline(bool on)
{
    m_brushOutline = on;
    update();
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

void Canvas::setHintMode(int mode)
{
    m_hintMode = std::clamp(mode, int(kDeleteHints), int(Morph::Vanishing));
    update();
}

void Canvas::setHintsHidden(bool hidden)
{
    m_hintsHidden = hidden;
    update();
}

void Canvas::setOtherKeyShown(bool shown)
{
    m_otherKeyShown = shown;
    update();
}

void Canvas::setMorphLayerInPlace(bool inPlace)
{
    m_morphLayerInPlace = inPlace;
    update();
}

void Canvas::hintDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers)
{
    QVector<QPair<int, bool>> const here = morphsHere();
    if (here.isEmpty()) {
        emit message(tr("Go to the first or the last drawing of a morphing to place its hints."));
        return;
    }
    Layer const& layer = m_document->scene().layers[m_layer];
    QTransform const view = layerViewTransform();
    // A point of a deformation path: taken hold of.
    for (QPair<int, bool> const& one : here) {
        QVector<Morph::Path> const& paths = layer.morphs[one.first].paths;
        for (int i = 0; i < paths.size(); ++i)
            for (int k = 0; k < paths[i].points.size(); ++k)
                if (QLineF(view.map(paths[i].points[k]), widgetPos).length() <= 8.0) {
                    m_hintMorph = one.first;
                    m_pathIndex = i;
                    m_pathPoint = k;
                    m_hintIndex = -1;
                    m_hintNow = paths[i].points[k];
                    m_drag = DragHint;
                    update();
                    return;
                }
    }
    if (m_hintsHidden) {
        emit message(tr("The hints are hidden (Hide Hints): show them to place or move them."));
        return;
    }
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
    // On a hint: it is taken hold of - or, with Alt (or in Delete Morphing Hint Mode), taken away.
    for (QPair<int, bool> const& one : here) {
        Morph const& morph = layer.morphs[one.first];
        for (int k = int(morph.hints.size()) - 1; k >= 0; --k) {
            QPointF const at = view.map(one.second ? morph.hints[k].p2() : morph.hints[k].p1());
            if (QLineF(at, widgetPos).length() > 8.0)
                continue;
            if ((modifiers & Qt::AltModifier) || m_hintMode == kDeleteHints) {
                Morph left = morph;
                left.hints.removeAt(k);
                if (k < left.hintTypes.size())
                    left.hintTypes.removeAt(k);
                m_document->setMorph(m_layer, one.first, left, tr("Remove Morphing Hint"));
                update();
                return;
            }
            m_hintMorph = one.first;
            m_hintIndex = k;
            m_pathIndex = -1;
            m_hintFar = one.second;
            m_hintNow = one.second ? morph.hints[k].p2() : morph.hints[k].p1();
            m_drag = DragHint;
            update();
            return;
        }
    }
    if (m_hintMode == kDeleteHints)
        return;
    // Anywhere else on the first drawing: a new hint of the type chosen, which goes to the same place until it is dragged on
    // the second.
    for (QPair<int, bool> const& one : here) {
        if (one.second)
            continue;
        Morph morph = layer.morphs[one.first];
        while (morph.hintTypes.size() < morph.hints.size())
            morph.hintTypes.push_back(Morph::Contour);
        morph.hints.push_back(QLineF(p, p));
        morph.hintTypes.push_back(m_hintMode);
        static char const* const names[] = {"Add Contour Hint", "Add Zone Hint", "Add Pencil Hint", "Add Appearing Point Hint", "Add Vanishing Point Hint"};
        m_document->setMorph(m_layer, one.first, morph, tr(names[std::clamp(m_hintMode, 0, 4)]));
        update();
        return;
    }
    emit message(tr("New hints are put on the first drawing of the morphing. Here, drag the ones it has to where they are to go."));
}

void Canvas::hintUp()
{
    Scene const& scene = m_document->scene();
    int const morph = m_hintMorph, index = m_hintIndex, path = m_pathIndex, point = m_pathPoint;
    m_hintMorph = m_hintIndex = m_pathIndex = m_pathPoint = -1;
    if (m_layer < 0 || m_layer >= scene.layers.size() || morph < 0 || morph >= scene.layers[m_layer].morphs.size())
        return;
    Morph changed = scene.layers[m_layer].morphs[morph];
    if (path >= 0) {
        if (path >= changed.paths.size() || point < 0 || point >= changed.paths[path].points.size())
            return;
        changed.paths[path].points[point] = m_hintNow;
        m_document->setMorph(m_layer, morph, changed, tr("Modify Deformation Path"));
        update();
        return;
    }
    if (index < 0 || index >= changed.hints.size())
        return;
    if (m_hintFar)
        changed.hints[index].setP2(m_hintNow);
    else
        changed.hints[index].setP1(m_hintNow);
    m_document->setMorph(m_layer, morph, changed, tr("Changed Morphing Hint(s)"));
    update();
}

void Canvas::paintMorphHints(QPainter& painter)
{
    if (m_tool != Morphing || m_playing)
        return;
    QVector<QPair<int, bool>> const here = morphsHere();
    if (here.isEmpty())
        return;
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[m_layer];
    QTransform const view = layerViewTransform();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont small = painter.font();
    small.setPixelSize(10);
    small.setBold(true);
    painter.setFont(small);
    // The shapes of the morphing layer chosen: drawn again on top of the rest (unless it is to be shown in place), outlined.
    if (int const index = layer.drawingAt(m_frame); index >= 0 && index < layer.drawings.size()) {
        Drawing const& d = layer.drawings[index];
        for (QPair<int, bool> const& one : here) {
            QVector<Morph::Group> const& groups = layer.morphs[one.first].groups;
            if (m_morphGroup < 0 || m_morphGroup >= groups.size())
                continue;
            QVector<int> const& keys = one.second ? groups[m_morphGroup].second : groups[m_morphGroup].first;
            if (!m_morphLayerInPlace) {
                Drawing top;
                for (int key : keys) {
                    int const sheet = (key >> 24) / 2, i = key & 0xffffff;
                    if (sheet < 0 || sheet >= Drawing::SheetCount)
                        continue;
                    if ((key >> 24) % 2 == 1 && i < d.strokesOf(sheet).size())
                        top.strokesOf(sheet).push_back(d.strokesOf(sheet)[i]);
                    else if ((key >> 24) % 2 == 0 && i < d.fillsOf(sheet).size())
                        top.fillsOf(sheet).push_back(d.fillsOf(sheet)[i]);
                }
                painter.save();
                painter.setTransform(view);
                paintDrawing(painter, scene, top, 1.0);
                painter.restore();
            }
            painter.setPen(QPen(QColor(80, 220, 255), 2.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            for (int key : keys) {
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
    // The deformation paths, with their points to drag.
    for (QPair<int, bool> const& one : here) {
        QVector<Morph::Path> const& paths = layer.morphs[one.first].paths;
        for (int i = 0; i < paths.size(); ++i) {
            QPolygonF shown = paths[i].points;
            if (m_drag == DragHint && m_hintMorph == one.first && m_pathIndex == i && m_pathPoint >= 0 && m_pathPoint < shown.size())
                shown[m_pathPoint] = m_hintNow;
            painter.setPen(QPen(paths[i].used ? QColor(230, 80, 200) : QColor(150, 150, 150), 1.6, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(view.map(shown));
            painter.setPen(QPen(QColor(20, 20, 20), 1.0));
            painter.setBrush(QColor(230, 80, 200));
            for (QPointF const& p : shown)
                painter.drawRect(QRectF(view.map(p) - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    if (!m_hintsHidden)
        for (QPair<int, bool> const& one : here) {
            Morph const& morph = layer.morphs[one.first];
            for (int k = 0; k < morph.hints.size(); ++k) {
                bool const held = m_drag == DragHint && m_pathIndex < 0 && m_hintMorph == one.first && m_hintIndex == k && m_hintFar == one.second;
                QPointF const at = view.map(held ? m_hintNow : one.second ? morph.hints[k].p2() : morph.hints[k].p1());
                // Green where it starts from, orange where it goes to; numbered, so that the two ends can be told; its shape
                // says its type (a circle: contour, a square: zone, a diamond: pencil, a triangle up: appearing, down: vanishing).
                painter.setPen(QPen(QColor(20, 20, 20), 1.2));
                painter.setBrush(one.second ? QColor(240, 150, 60) : QColor(90, 200, 110));
                switch (morph.hintType(k)) {
                case Morph::Zone:
                    painter.drawRect(QRectF(at - QPointF(7, 7), QSizeF(14, 14)));
                    break;
                case Morph::Pencil:
                    painter.drawPolygon(QPolygonF({at + QPointF(0, -8), at + QPointF(8, 0), at + QPointF(0, 8), at + QPointF(-8, 0)}));
                    break;
                case Morph::Appearing:
                    painter.drawPolygon(QPolygonF({at + QPointF(0, -9), at + QPointF(8, 6), at + QPointF(-8, 6)}));
                    break;
                case Morph::Vanishing:
                    painter.drawPolygon(QPolygonF({at + QPointF(0, 9), at + QPointF(8, -6), at + QPointF(-8, -6)}));
                    break;
                default:
                    painter.drawEllipse(at, 7.0, 7.0);
                }
                painter.drawText(QRectF(at.x() - 7.0, at.y() - 7.0, 14.0, 14.0), Qt::AlignCenter, QString::number(k + 1));
            }
        }
    // Show Other Morphing Key Drawing: the other key drawing in a thumbnail in the top right corner.
    if (m_otherKeyShown) {
        QPair<int, bool> const one = here.first();
        int const otherFrame = one.second ? layer.morphs[one.first].from : layer.morphEnd(one.first);
        int const other = otherFrame >= 0 ? layer.drawingAt(otherFrame) : Layer::kNoDrawing;
        if (other >= 0 && other < layer.drawings.size()) {
            Drawing const& d = layer.drawings[other];
            QRectF box;
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Fill const& f : d.fillsOf(sheet))
                    box = box.united(f.bounds());
                for (Stroke const& s : d.strokesOf(sheet))
                    box = box.united(s.bounds());
            }
            QRectF const frame(width() - 172.0, 12.0, 160.0, 120.0);
            painter.setPen(QPen(QColor(60, 60, 60), 1.0));
            painter.setBrush(QColor(255, 255, 255, 230));
            painter.drawRect(frame);
            if (!box.isEmpty()) {
                double const scale = std::min((frame.width() - 12.0) / std::max(1.0, box.width()), (frame.height() - 12.0) / std::max(1.0, box.height()));
                painter.save();
                painter.setClipRect(frame);
                painter.setTransform(QTransform::fromTranslate(-box.center().x(), -box.center().y()) * QTransform::fromScale(scale, scale)
                                     * QTransform::fromTranslate(frame.center().x(), frame.center().y()));
                paintDrawing(painter, scene, d, 1.0);
                painter.restore();
            }
            painter.setPen(QColor(40, 40, 40));
            painter.drawText(frame.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, one.second ? tr("First") : tr("Last"));
        }
    }
    painter.restore();
}
