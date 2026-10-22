// Eclipse - the document: blocks of cells copied, moved and pasted; markers.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"
#include "model/document/CommandsCellsShared.h"

using namespace commands_cells_shared;

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
                row.labels.push_back(layer.drawingLabel(index));
            }
            row.cells.push_back(at);
        }
        for (Keyframe key : layer.keys) {
            if (key.frame < first || key.frame > last)
                continue;
            key.frame -= first;
            row.keys.push_back(key);
        }
        row.layerName = layer.name;
        row.pivot = layer.pivot;
        block.rows.push_back(row);
    }
    return block;
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

bool Document::setMarkerNote(int index, QString const& note)
{
    if (index < 0 || index >= m_scene.markers.size() || m_scene.markers[index].note == note)
        return false;
    apply(tr("Scene Marker Note"), [&](Scene& s) { s.markers[index].note = note; });
    return true;
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
