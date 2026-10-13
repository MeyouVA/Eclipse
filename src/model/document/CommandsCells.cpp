// Eclipse - the document: drawings and their exposure, blocks of cells, frames, markers.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"

void Document::newDrawing(int layer, int frame)
{
    if (validCell(m_scene, layer, frame) && m_scene.layers[layer].holdsDrawings())
        apply(tr("New Drawing"), [&](Scene& s) { startDrawingAt(s.layers[layer], frame, Drawing()); });
}

void Document::duplicateDrawing(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const index = m_scene.layers[layer].cells[frame];
    if (index == Layer::kNoDrawing)
        return;
    apply(tr("Duplicate Drawing"), [&](Scene& s) {
        Drawing copy = s.layers[layer].drawings[index];
        // A name belongs to one drawing: the copy takes it only when the drawing it was made from
        // is shown nowhere else, and so goes.
        bool elsewhere = s.layers[layer].drawings[index].pinned;
        // (The copy takes over from this frame to the end of the exposure.)
        QVector<int> const& cells = s.layers[layer].cells;
        int end = frame + 1;
        while (end < cells.size() && cells[end] == index)
            ++end;
        for (int f = 0; f < cells.size(); ++f)
            elsewhere = elsewhere || ((f < frame || f >= end) && cells[f] == index);
        if (elsewhere)
            copy.name.clear();
        startDrawingAt(s.layers[layer], frame, copy);
    });
}

bool Document::exposeDrawing(int layer, int frame, QString const& label)
{
    if (!validCell(m_scene, layer, frame) || !m_scene.layers[layer].holdsDrawings())
        return false;
    Layer const& l = m_scene.layers[layer];
    QString const clean = label.trimmed();
    if (clean.isEmpty()) {
        if (l.cells[frame] == Layer::kNoDrawing)
            return false;
        clearCell(layer, frame);
        return true;
    }
    for (int i = 0; i < l.drawings.size(); ++i) {
        if (l.drawingLabel(i) != clean)
            continue;
        if (l.cells[frame] == i)
            return false;
        apply(tr("Type Exposure"), [&](Scene& s) {
            s.layers[layer].cells[frame] = i;
            s.layers[layer].compact();
        });
        return true;
    }
    apply(tr("Type Exposure"), [&](Scene& s) {
        Layer& made = s.layers[layer];
        Drawing drawing;
        // The number the new drawing would have anyway is no name.
        bool number = false;
        int const n = clean.toInt(&number);
        if (!(number && n == made.drawings.size() + 1))
            drawing.name = clean;
        made.drawings.push_back(drawing);
        made.cells[frame] = int(made.drawings.size()) - 1;
        made.compact();
    });
    return true;
}

bool Document::renameDrawingsWithPrefix(int layer, QString const& prefix)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || prefix.trimmed().isEmpty())
        return false;
    Layer const& l = m_scene.layers[layer];
    bool any = false;
    for (int i = 0; i < l.drawings.size(); ++i)
        any = any || l.drawings[i].name != prefix.trimmed() + QString::number(i + 1);
    if (!any)
        return false;
    apply(tr("Rename Drawings with Prefix"), [&](Scene& s) {
        for (int i = 0; i < s.layers[layer].drawings.size(); ++i)
            s.layers[layer].drawings[i].name = prefix.trimmed() + QString::number(i + 1);
    });
    return true;
}

bool Document::renameDrawingsByFrame(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings())
        return false;
    Layer const& l = m_scene.layers[layer];
    QVector<QString> names(l.drawings.size());
    bool any = false;
    for (int i = 0; i < l.drawings.size(); ++i) {
        names[i] = l.drawings[i].name;
        int const first = int(l.cells.indexOf(i));
        if (first >= 0)
            names[i] = QString::number(first + 1);
        any = any || names[i] != l.drawings[i].name;
    }
    if (!any)
        return false;
    apply(tr("Rename Drawings by Frame"), [&](Scene& s) {
        for (int i = 0; i < names.size(); ++i)
            s.layers[layer].drawings[i].name = names[i];
    });
    return true;
}

bool Document::markDrawing(int layer, int frame, int mark)
{
    if (!validCell(m_scene, layer, frame) || mark < Drawing::NoMark || mark > Drawing::InBetweenMark)
        return false;
    int const index = m_scene.layers[layer].cells[frame];
    if (index == Layer::kNoDrawing || m_scene.layers[layer].drawings[index].mark == mark)
        return false;
    apply(tr("Mark Drawing"), [&](Scene& s) { s.layers[layer].drawings[index].mark = mark; });
    return true;
}

bool Document::renameDrawing(int layer, int frame, QString const& name)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    Layer const& l = m_scene.layers[layer];
    int const index = l.cells[frame];
    QString const clean = name.trimmed();
    if (index == Layer::kNoDrawing || clean == l.drawings[index].name)
        return false;
    for (int i = 0; i < l.drawings.size(); ++i)
        if (i != index && !clean.isEmpty() && l.drawingLabel(i) == clean)
            return false;       // two drawings of a layer cannot go by one name
    apply(tr("Rename Drawing"), [&](Scene& s) { s.layers[layer].drawings[index].name = clean; });
    return true;
}

void Document::extendExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || frame == 0)
        return;
    int const previous = m_scene.layers[layer].cells[frame - 1];
    if (previous == Layer::kNoDrawing || previous == m_scene.layers[layer].cells[frame])
        return;
    apply(tr("Extend Exposure"), [&](Scene& s) {
        s.layers[layer].cells[frame] = previous;
        s.layers[layer].compact();
    });
}

void Document::deleteDrawing(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const drawing = m_scene.layers[layer].drawingAt(frame);
    if (drawing == Layer::kNoDrawing)
        return;
    apply(tr("Delete Drawing"), [&](Scene& s) {
        for (int& cell : s.layers[layer].cells)
            if (cell == drawing)
                cell = Layer::kNoDrawing;
        s.layers[layer].compact();
    });
}

void Document::clearCell(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Clear Cell"), [&](Scene& s) {
        s.layers[layer].cells[frame] = Layer::kNoDrawing;
        s.layers[layer].compact();
    });
}

int Document::importDrawings(int layer, QString const& name, int frame, QVector<Drawing> const& drawings, int hold)
{
    if (drawings.isEmpty() || frame < 0)
        return -1;
    if (layer >= 0 && (layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings()))
        return -1;
    hold = std::max(1, hold);
    int at = layer;
    apply(tr("Import"), [&](Scene& s) {
        int const end = frame + int(drawings.size()) * hold;
        if (end > s.frameCount())
            s.setFrameCount(end);
        if (at < 0)
            at = s.addLayer(name.isEmpty() ? tr("Drawing") : name, int(s.layers.size()), Layer::DrawingLayer);
        Layer& target = s.layers[at];
        int const base = int(target.drawings.size());
        target.drawings += drawings;
        for (int i = 0; i < drawings.size(); ++i)
            for (int h = 0; h < hold; ++h)
                target.cells[frame + i * hold + h] = base + i;
        target.compact();
    });
    return at;
}

void Document::substituteDrawing(int layer, int frame, int drawing)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    if (drawing < 0 || drawing >= l.drawings.size() || l.cells[frame] == drawing)
        return;
    apply(tr("Substitute Drawing"), [&](Scene& s) { s.layers[layer].substitute(frame, drawing); });
}

void Document::stepSubstitution(int layer, int frame, int direction)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    int const count = int(l.drawings.size());
    if (count == 0)
        return;
    int const shown = l.cells[frame];
    int const next = shown == Layer::kNoDrawing ? (direction > 0 ? 0 : count - 1) : ((shown + direction) % count + count) % count;
    substituteDrawing(layer, frame, next);
}

CellBlock CellBlock::reversed() const
{
    CellBlock out = *this;
    for (Row& row : out.rows) {
        std::reverse(row.cells.begin(), row.cells.end());
        for (Keyframe& key : row.keys)
            key.frame = frames - 1 - key.frame;
        std::sort(row.keys.begin(), row.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
    }
    return out;
}

CellBlock CellBlock::repeated(int times) const
{
    CellBlock out = *this;
    if (times < 2)
        return out;
    out.frames = frames * times;
    for (int r = 0; r < rows.size(); ++r) {
        for (int n = 1; n < times; ++n) {
            out.rows[r].cells += rows[r].cells;      // the same drawings again, not copies of them
            for (Keyframe key : rows[r].keys) {
                key.frame += n * frames;
                out.rows[r].keys.push_back(key);
            }
        }
    }
    return out;
}

CellBlock Document::copyCells(QVector<int> const& layers, int first, int last) const
{
    CellBlock block;
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    if (last < first)
        return block;
    block.frames = last - first + 1;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size())
            continue;
        Layer const& layer = m_scene.layers[l];
        CellBlock::Row row;
        row.holdsDrawings = layer.holdsDrawings();
        QVector<int> seen;  // the drawings of the layer met so far, in the order of the row's own
        for (int f = first; f <= last; ++f) {
            int const index = layer.drawingAt(f);
            if (index == Layer::kNoDrawing) {
                row.cells.push_back(-1);
                continue;
            }
            int at = int(seen.indexOf(index));
            if (at < 0) {
                at = int(seen.size());
                seen.push_back(index);
                row.drawings.push_back(layer.drawings[index]);
                row.drawings.last().pinned = false;
            }
            row.cells.push_back(at);
        }
        for (Keyframe key : layer.keys) {
            if (key.frame < first || key.frame > last)
                continue;
            key.frame -= first;
            row.keys.push_back(key);
        }
        block.rows.push_back(row);
    }
    return block;
}

// No drawing shown and no keyframe in the frames `first`..`last` of these layers.
static void emptyCells(Scene& s, QVector<int> const& layers, int first, int last)
{
    for (int l : layers) {
        if (l < 0 || l >= s.layers.size())
            continue;
        Layer& layer = s.layers[l];
        for (int f = first; f <= last; ++f)
            layer.cells[f] = Layer::kNoDrawing;
        layer.compact();
        layer.keys.erase(std::remove_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame >= first && k.frame <= last; }),
                         layer.keys.end());
    }
}

// Puts a block down from `frame` on, its rows onto these layers in turn.
static void putBlock(Scene& s, QVector<int> const& layers, int frame, CellBlock const& block)
{
    int const begin = std::max(frame, 0);
    int const end = std::min(frame + block.frames, s.frameCount());  // one past the last frame written
    for (int r = 0; r < block.rows.size() && r < layers.size(); ++r) {
        int const l = layers[r];
        if (l < 0 || l >= s.layers.size())
            continue;
        Layer& layer = s.layers[l];
        CellBlock::Row const& row = block.rows[r];
        if (layer.holdsDrawings() && row.holdsDrawings) {
            int const base = int(layer.drawings.size());
            layer.drawings += row.drawings;
            for (int f = begin; f < end; ++f)
                layer.cells[f] = row.cells[f - frame] < 0 ? Layer::kNoDrawing : base + row.cells[f - frame];
            layer.compact();
        }
        if (block.leaveKeys)
            continue;
        // The keyframes of the block take the place of those that were in its frames.
        layer.keys.erase(std::remove_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame >= begin && k.frame < end; }),
                         layer.keys.end());
        for (Keyframe key : row.keys) {
            key.frame += frame;
            if (key.frame >= begin && key.frame < end)
                layer.keys.push_back(key);
        }
        std::sort(layer.keys.begin(), layer.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
    }
}

void Document::moveCells(QVector<int> const& from, int first, int last, QVector<int> const& to, int frame, bool copy)
{
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    CellBlock const block = copyCells(from, first, last);
    if (block.isEmpty() || (!copy && from == to && frame == first))
        return;
    apply(copy ? tr("Copy Cells") : tr("Move Cells"), [&](Scene& s) {
        if (!copy)
            emptyCells(s, from, first, last);
        putBlock(s, to, frame, block);
    });
}

int Document::addMarker(int frame, int length, QString const& name, QColor const& color)
{
    frame = std::clamp(frame, 0, m_scene.frameCount() - 1);
    length = std::clamp(length, 1, m_scene.frameCount() - frame);
    int at = -1;
    apply(tr("Create Scene Marker"), [&](Scene& s) {
        s.markers.erase(std::remove_if(s.markers.begin(), s.markers.end(),
                                       [&](Scene::Marker const& m) { return m.frame < frame + length && frame < m.frame + m.length; }),
                        s.markers.end());
        Scene::Marker marker;
        marker.frame = frame;
        marker.length = length;
        marker.name = name;
        if (color.isValid())
            marker.color = color;
        at = 0;
        while (at < s.markers.size() && s.markers[at].frame < frame)
            ++at;
        s.markers.insert(at, marker);
    });
    return at;
}

void Document::editMarker(int index, QString const& name, QColor const& color)
{
    if (index < 0 || index >= m_scene.markers.size())
        return;
    Scene::Marker const& now = m_scene.markers[index];
    if (now.name == name && (!color.isValid() || now.color == color))
        return;
    apply(tr("Edit Scene Marker"), [&](Scene& s) {
        s.markers[index].name = name;
        if (color.isValid())
            s.markers[index].color = color;
    });
}

bool Document::moveMarker(int index, int frame)
{
    if (index < 0 || index >= m_scene.markers.size())
        return false;
    Scene::Marker const now = m_scene.markers[index];
    frame = std::clamp(frame, 0, std::max(0, m_scene.frameCount() - now.length));
    if (frame == now.frame)
        return false;
    for (int i = 0; i < m_scene.markers.size(); ++i) {
        Scene::Marker const& other = m_scene.markers[i];
        if (i != index && other.frame < frame + now.length && frame < other.frame + other.length)
            return false;
    }
    apply(tr("Move Scene Marker"), [&](Scene& s) {
        Scene::Marker marker = s.markers.takeAt(index);
        marker.frame = frame;
        int at = 0;
        while (at < s.markers.size() && s.markers[at].frame < frame)
            ++at;
        s.markers.insert(at, marker);
    });
    return true;
}

void Document::setCells(int layer, int first, QVector<int> const& drawings, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || first < 0)
        return;
    Layer const& now = m_scene.layers[layer];
    bool any = false;
    for (int i = 0; i < drawings.size() && first + i < now.cells.size(); ++i)
        any = any || (drawings[i] < int(now.drawings.size()) && drawings[i] >= Layer::kNoDrawing && now.cells[first + i] != drawings[i]);
    if (!any)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        int const count = int(l.drawings.size());
        for (int i = 0; i < drawings.size() && first + i < l.cells.size(); ++i)
            if (drawings[i] < count && drawings[i] >= Layer::kNoDrawing)
                l.cells[first + i] = drawings[i];
        l.compact();
    });
}

void Document::removeMarker(int index)
{
    if (index < 0 || index >= m_scene.markers.size())
        return;
    apply(tr("Delete Scene Marker"), [&](Scene& s) { s.markers.removeAt(index); });
}

void Document::insertFrames(int at, int count)
{
    if (count < 1 || at < 0 || at > m_scene.frameCount())
        return;
    apply(tr("Add Frames"), [&](Scene& s) {
        s.setFrameCount(s.frameCount() + count);
        for (Layer& layer : s.layers) {
            layer.cells.insert(at, count, Layer::kNoDrawing);
            layer.cells.resize(s.frameCount());
            for (Keyframe& key : layer.keys)
                if (key.frame >= at)
                    key.frame += count;
            for (Morph& morph : layer.morphs)
                if (morph.from >= at)
                    morph.from += count;
            if (layer.kind == Layer::Sound && layer.soundStart >= at)
                layer.soundStart += count;
        }
        for (Scene::Marker& m : s.markers) {
            if (m.frame >= at)
                m.frame += count;
            else if (m.frame + m.length > at)
                m.length += count;      // put in the middle of it: it grows
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
        for (Layer& layer : s.layers) {
            layer.cells.remove(first, count);
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
            if (layer.kind == Layer::Sound && layer.soundStart > last)
                layer.soundStart -= count;
            else if (layer.kind == Layer::Sound && layer.soundStart >= first)
                layer.soundStart = first;
        }
        for (Scene::Marker& m : s.markers) {
            // What is left of the marker once the frames are gone.
            int const end = m.frame + m.length;
            int const newStart = m.frame > last ? m.frame - count : std::min(m.frame, first);
            int const newEnd = end > last + 1 ? end - count : std::min(end, first);
            m.frame = newStart;
            m.length = newEnd - newStart;
        }
        s.markers.erase(std::remove_if(s.markers.begin(), s.markers.end(), [](Scene::Marker const& m) { return m.length < 1; }), s.markers.end());
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

// The cells of a layer are as many as the scene has frames: what is pushed past the end is
// lost, and what is pulled back leaves empty cells at the end.
static void fitCells(Scene& s, int layer)
{
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
    while (start > 0 && layer.cells[start - 1] == drawing)
        --start;
    while (start + length < layer.cells.size() && layer.cells[start + length] == drawing)
        ++length;
}

void Document::increaseExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Increase Exposure"), [&](Scene& s) {
        s.layers[layer].cells.insert(frame + 1, s.layers[layer].cells[frame]);
        fitCells(s, layer);
    });
}

void Document::decreaseExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Decrease Exposure"), [&](Scene& s) {
        s.layers[layer].cells.removeAt(frame);
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
        if (length > now)
            cells.insert(start + now, length - now, drawing);
        else
            cells.remove(start + length, now - length);
        fitCells(s, layer);
    });
}

void Document::insertBlankCell(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    apply(tr("Insert Blank Cell"), [&](Scene& s) {
        s.layers[layer].cells.insert(frame, Layer::kNoDrawing);
        fitCells(s, layer);
    });
}

void Document::clearExposureAndPull(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int start = 0, length = 0;
    exposureOf(m_scene.layers[layer], frame, start, length);
    if (length == 0)
        return;
    apply(tr("Clear Exposure and Pull"), [&](Scene& s) {
        s.layers[layer].cells.remove(start, length);
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
