// Eclipse - the document: frames inserted and removed; exposures longer, shorter, keyed, blank, filled.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"
#include "model/document/CommandsCellsShared.h"

using namespace commands_cells_shared;

// Frames put in at `at`: what an annotation column holds from there on goes down with them.
static void shiftAnnotation(Layer& layer, int at, int count)
{
    QMap<int, QString> notes;
    for (auto it = layer.notes.constBegin(); it != layer.notes.constEnd(); ++it)
        notes.insert(it.key() >= at ? it.key() + count : it.key(), it.value());
    layer.notes = notes;
    for (AnnotationInk& mark : layer.ink)
        for (QPointF& p : mark.points)
            if (p.y() >= at)
                p.ry() += count;
    for (AnnotationPicture& picture : layer.pictures)
        if (picture.frame >= at)
            picture.frame += count;
}

// Frames taken out: what was on them goes too, what was below comes up.
static void cutAnnotation(Layer& layer, int first, int last)
{
    int const count = last - first + 1;
    QMap<int, QString> notes;
    for (auto it = layer.notes.constBegin(); it != layer.notes.constEnd(); ++it)
        if (it.key() < first || it.key() > last)
            notes.insert(it.key() > last ? it.key() - count : it.key(), it.value());
    layer.notes = notes;
    QVector<AnnotationInk> ink;
    for (AnnotationInk mark : std::as_const(layer.ink)) {
        bool gone = false;
        for (QPointF& p : mark.points) {
            if (p.y() >= first && p.y() < last + 1)
                gone = true;
            else if (p.y() >= last + 1)
                p.ry() -= count;
        }
        if (!gone)
            ink.push_back(mark);
    }
    layer.ink = ink;
    QVector<AnnotationPicture> pictures;
    for (AnnotationPicture picture : std::as_const(layer.pictures)) {
        if (picture.frame >= first && picture.frame <= last)
            continue;
        if (picture.frame > last)
            picture.frame -= count;
        pictures.push_back(picture);
    }
    layer.pictures = pictures;
}

void Document::insertFrames(int at, int count)
{
    if (count < 1 || at < 0 || at > m_scene.frameCount())
        return;
    apply(tr("Add Frames"), [&](Scene& s) {
        s.setFrameCount(s.frameCount() + count);
        for (FunctionColumn& column : s.functions) {
            for (Layer* curve : {&column.curve, &column.velocity})
                for (Keyframe& key : curve->keys)
                    if (key.frame >= at)
                        key.frame += count;
            for (FunctionColumn::PathPoint& point : column.path)
                if (point.frame >= at)
                    point.frame += count;
            if (column.velocityRangeSet) {
                if (column.velocityStart >= at) column.velocityStart += count;
                if (column.velocityEnd >= at) column.velocityEnd += count;
            }
        }
        for (GraphNode& node : s.graph.nodes) {
            QMap<int, double> shifted;
            for (auto it = node.amountKeys.cbegin(); it != node.amountKeys.cend(); ++it)
                shifted.insert(it.key() >= at ? it.key() + count : it.key(), it.value());
            node.amountKeys = shifted;
        }
        for (Layer& layer : s.layers) {
            layer.cells.insert(at, count, Layer::kNoDrawing);
            layer.cells.resize(s.frameCount());
            layer.shiftKeyExposures(at, count);
            for (Keyframe& key : layer.keys)
                if (key.frame >= at)
                    key.frame += count;
            for (Morph& morph : layer.morphs)
                if (morph.from >= at)
                    morph.from += count;
            if (layer.kind == Layer::Sound && !layer.soundFrames.isEmpty() && at > layer.soundStart && at <= layer.soundStart + layer.soundFrames.size())
                layer.soundFrames.insert(at - layer.soundStart, count, -1);
            if (layer.kind == Layer::Sound && layer.soundStart >= at)
                layer.soundStart += count;
            QMap<int, QColor> shifted;
            for (auto it = layer.frameMarks.constBegin(); it != layer.frameMarks.constEnd(); ++it)
                shifted.insert(it.key() >= at ? it.key() + count : it.key(), it.value());
            layer.frameMarks = shifted;
            shiftAnnotation(layer, at, count);
        }
        for (Scene::SheetMarker& m : s.sheetMarkers) {
            if (m.frame >= at)
                m.frame += count;
            else if (m.frame + m.length > at)
                m.length += count;
        }
        for (Scene::Marker& m : s.markers) {
            if (m.frame >= at)
                m.frame += count;
            else if (m.frame + m.length > at)
                m.length += count;      // put in the middle of it: it grows
        }
        // The controllers' own keyframes go with the frames too.
        for (Scene::Controller& c : s.controllers) {
            QMap<int, double> keys;
            for (auto it = c.keys.cbegin(); it != c.keys.cend(); ++it)
                keys.insert(it.key() >= at ? it.key() + count : it.key(), it.value());
            c.keys = keys;
            QMap<int, QPointF> points;
            for (auto it = c.pointKeys.cbegin(); it != c.pointKeys.cend(); ++it)
                points.insert(it.key() >= at ? it.key() + count : it.key(), it.value());
            c.pointKeys = points;
        }
    });
}

void Document::removeFrames(int first, int last)
{
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    if (last - first + 1 >= m_scene.frameCount())
        ++first;  // a scene keeps one frame
    if (last < first)
        return;
    int const count = last - first + 1;
    apply(tr("Remove Frames"), [&](Scene& s) {
        for (FunctionColumn& column : s.functions) {
            for (Layer* curve : {&column.curve, &column.velocity}) {
                curve->keys.removeIf([&](Keyframe const& key) { return key.frame >= first && key.frame <= last; });
                for (Keyframe& key : curve->keys)
                    if (key.frame > last)
                        key.frame -= count;
            }
            column.path.removeIf([&](FunctionColumn::PathPoint const& point) { return point.frame >= first && point.frame <= last; });
            for (FunctionColumn::PathPoint& point : column.path)
                if (point.frame > last)
                    point.frame -= count;
            if (column.velocityRangeSet) {
                auto shifted = [&](int value) { return value > last ? value - count : value >= first ? first : value; };
                column.velocityStart = shifted(column.velocityStart);
                column.velocityEnd = shifted(column.velocityEnd);
            }
        }
        for (GraphNode& node : s.graph.nodes) {
            QMap<int, double> shifted;
            for (auto it = node.amountKeys.cbegin(); it != node.amountKeys.cend(); ++it)
                if (it.key() < first || it.key() > last)
                    shifted.insert(it.key() > last ? it.key() - count : it.key(), it.value());
            node.amountKeys = shifted;
        }
        for (Layer& layer : s.layers) {
            layer.cells.remove(first, count);
            layer.shiftKeyExposures(first, -count);
            layer.keys.erase(std::remove_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame >= first && k.frame <= last; }),
                             layer.keys.end());
            for (Keyframe& key : layer.keys)
                if (key.frame > last)
                    key.frame -= count;
            layer.morphs.erase(std::remove_if(layer.morphs.begin(), layer.morphs.end(), [&](Morph const& m) { return m.from >= first && m.from <= last; }),
                               layer.morphs.end());
            for (Morph& morph : layer.morphs)
                if (morph.from > last)
                    morph.from -= count;
            if (layer.kind == Layer::Sound && !layer.soundFrames.isEmpty()) {
                int const begin = std::max(0, first - layer.soundStart), end = std::min(int(layer.soundFrames.size()) - 1, last - layer.soundStart);
                if (end >= begin)
                    layer.soundFrames.remove(begin, end - begin + 1);
                if (layer.soundFrames.isEmpty())
                    layer.soundFrames << -1;
            }
            if (layer.kind == Layer::Sound && layer.soundStart > last)
                layer.soundStart -= count;
            else if (layer.kind == Layer::Sound && layer.soundStart >= first)
                layer.soundStart = first;
            QMap<int, QColor> left;
            for (auto it = layer.frameMarks.constBegin(); it != layer.frameMarks.constEnd(); ++it)
                if (it.key() < first || it.key() > last)
                    left.insert(it.key() > last ? it.key() - count : it.key(), it.value());
            layer.frameMarks = left;
            cutAnnotation(layer, first, last);
        }
        for (Scene::SheetMarker& m : s.sheetMarkers) {
            int const end = m.frame + m.length;
            int const newStart = m.frame > last ? m.frame - count : std::min(m.frame, first);
            int const newEnd = end > last + 1 ? end - count : std::min(end, first);
            m.frame = newStart;
            m.length = newEnd - newStart;
        }
        s.sheetMarkers.erase(std::remove_if(s.sheetMarkers.begin(), s.sheetMarkers.end(), [](Scene::SheetMarker const& m) { return m.length < 1; }),
                             s.sheetMarkers.end());
        for (Scene::Marker& m : s.markers) {
            // What is left of the marker once the frames are gone.
            int const end = m.frame + m.length;
            int const newStart = m.frame > last ? m.frame - count : std::min(m.frame, first);
            int const newEnd = end > last + 1 ? end - count : std::min(end, first);
            m.frame = newStart;
            m.length = newEnd - newStart;
        }
        s.markers.erase(std::remove_if(s.markers.begin(), s.markers.end(), [](Scene::Marker const& m) { return m.length < 1; }), s.markers.end());
        for (Scene::Controller& c : s.controllers) {
            QMap<int, double> keys;
            for (auto it = c.keys.cbegin(); it != c.keys.cend(); ++it)
                if (it.key() < first || it.key() > last)
                    keys.insert(it.key() > last ? it.key() - count : it.key(), it.value());
            c.keys = keys;
            QMap<int, QPointF> points;
            for (auto it = c.pointKeys.cbegin(); it != c.pointKeys.cend(); ++it)
                if (it.key() < first || it.key() > last)
                    points.insert(it.key() > last ? it.key() - count : it.key(), it.value());
            c.pointKeys = points;
        }
        s.setFrameCount(s.frameCount() - count);
    });
}

void Document::clearCells(QVector<int> const& layers, int first, int last, QString const& text)
{
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    bool any = false;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size())
            continue;
        Layer const& layer = m_scene.layers[l];
        for (int f = first; f <= last && !any; ++f)
            any = layer.drawingAt(f) != Layer::kNoDrawing || layer.keyIndex(f) >= 0;
    }
    if (!any)
        return;
    apply(text, [&](Scene& s) { emptyCells(s, layers, first, last); });
}

void Document::pasteCells(QVector<int> const& layers, int frame, CellBlock const& block)
{
    if (block.isEmpty() || layers.isEmpty() || frame < 0 || frame >= m_scene.frameCount())
        return;
    apply(tr("Paste Cells"), [&](Scene& s) { putBlock(s, layers, frame, block); });
}

static void fitCells(Scene& s, int layer)
{
    int last = int(s.layers[layer].cells.size()) - 1;
    while (last >= s.frameCount() && s.layers[layer].cells[last] == Layer::kNoDrawing)
        --last;
    if (last >= s.frameCount())
        s.setFrameCount(last + 1);
    s.layers[layer].cells.resize(s.frameCount(), Layer::kNoDrawing);
    s.layers[layer].compact();
}

// The exposure `frame` is in: its first cell and how many cells it has (0 for an empty cell).
static void exposureOf(Layer const& layer, int frame, int& start, int& length)
{
    start = frame;
    length = 0;
    int const drawing = layer.drawingAt(frame);
    if (drawing == Layer::kNoDrawing)
        return;
    while (start > 0 && layer.cells[start - 1] == drawing && !layer.keyExposures.contains(start))
        --start;
    length = 1;
    while (start + length < layer.cells.size() && layer.cells[start + length] == drawing && !layer.keyExposures.contains(start + length))
        ++length;
}

void Document::addKeyExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing || m_scene.layers[layer].isKey(frame))
        return;
    apply(tr("Add Key Exposure"), [&](Scene& s) {
        s.layers[layer].keyExposures.push_back(frame);
        std::sort(s.layers[layer].keyExposures.begin(), s.layers[layer].keyExposures.end());
    });
}

void Document::removeKeyExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int start = 0, length = 0;
    exposureOf(m_scene.layers[layer], frame, start, length);
    if (length == 0)
        return;
    apply(tr("Remove Key Exposure"), [&](Scene& s) {
        Layer& edited = s.layers[layer];
        if (edited.keyExposures.contains(start)) {
            edited.keyExposures.removeAll(start);       // the same drawing as before it: one exposure again
            return;
        }
        // What was shown before it is held over its cells (nothing, at the start of the layer).
        int const before = start > 0 ? edited.cells[start - 1] : Layer::kNoDrawing;
        for (int f = start; f < start + length; ++f)
            edited.cells[f] = before;
        edited.keyExposures.removeAll(start + length);  // (were the one after it the same drawing again, it would run on)
        edited.compact();
    });
}

void Document::removeDuplicateKeyExposures(int layer, int first, int last)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    bool any = false;
    for (int mark : m_scene.layers[layer].keyExposures)
        any = any || (mark >= first && mark <= last);
    if (!any)
        return;
    apply(tr("Remove Duplicate Key Exposure"), [&](Scene& s) {
        QVector<int>& marks = s.layers[layer].keyExposures;
        marks.erase(std::remove_if(marks.begin(), marks.end(), [&](int mark) { return mark >= first && mark <= last; }), marks.end());
    });
}

void Document::increaseExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Increase Exposure"), [&](Scene& s) {
        s.layers[layer].cells.insert(frame + 1, s.layers[layer].cells[frame]);
        s.layers[layer].shiftKeyExposures(frame + 1, 1);
        fitCells(s, layer);
    });
}

void Document::decreaseExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Decrease Exposure"), [&](Scene& s) {
        s.layers[layer].cells.removeAt(frame);
        s.layers[layer].shiftKeyExposures(frame, -1);
        fitCells(s, layer);
    });
}

void Document::setExposure(int layer, int frame, int length)
{
    if (!validCell(m_scene, layer, frame) || length < 1)
        return;
    int start = 0, now = 0;
    exposureOf(m_scene.layers[layer], frame, start, now);
    if (now == 0 || now == length)
        return;
    apply(tr("Set Exposure"), [&](Scene& s) {
        QVector<int>& cells = s.layers[layer].cells;
        int const drawing = cells[start];
        if (length > now) {
            cells.insert(start + now, length - now, drawing);
            s.layers[layer].shiftKeyExposures(start + now, length - now);
        } else {
            cells.remove(start + length, now - length);
            s.layers[layer].shiftKeyExposures(start + length, length - now);
        }
        fitCells(s, layer);
    });
}

void Document::insertBlankCell(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    apply(tr("Insert Blank Cell"), [&](Scene& s) {
        s.layers[layer].cells.insert(frame, Layer::kNoDrawing);
        s.layers[layer].shiftKeyExposures(frame, 1);
        fitCells(s, layer);
    });
}

void Document::clearExposureAndPull(int layer, int frame, int count)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int start = 0, length = 0;
    exposureOf(m_scene.layers[layer], frame, start, length);
    if (count > 0) {
        start = frame;
        length = std::min(count, m_scene.frameCount() - frame);
    }
    if (length == 0)
        return;
    apply(tr("Clear Exposure and Pull"), [&](Scene& s) {
        s.layers[layer].cells.remove(start, length);
        s.layers[layer].shiftKeyExposures(start, -length);
        fitCells(s, layer);
    });
}

void Document::fillEmptyCells(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    QVector<int> cells = m_scene.layers[layer].cells;
    for (int f = 1; f < cells.size(); ++f)
        if (cells[f] == Layer::kNoDrawing)
            cells[f] = cells[f - 1];
    if (cells == m_scene.layers[layer].cells)
        return;
    apply(tr("Fill Empty Cells"), [&](Scene& s) { s.layers[layer].cells = cells; });
}
