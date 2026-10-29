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
        QVector<Keyframe> const original = row.keys;
        Layer source;
        source.keys = original;
        for (int i = 0; i < row.keys.size(); ++i) {
            Keyframe& key = row.keys[i];
            key.frame = frames - 1 - key.frame;
            std::swap(key.easeIn, key.easeOut);
            std::swap(key.liftIn, key.liftOut);
            key.curves.clear();
            for (int value = 0; value < Driver::ValueCount; ++value) {
                ValueCurve curve = source.curveAt(value, i);
                std::swap(curve.in, curve.out);
                curve.in.rx() = -curve.in.x();
                curve.out.rx() = -curve.out.x();
                if (i > 0)
                    curve.after = source.curveAt(value, i - 1).after;
                key.setCurve(curve);
            }
            if (i > 0) {
                key.tween = original[i - 1].tween;
                key.controls = original[i - 1].controls;
                key.controlsZ = original[i - 1].controlsZ;
                std::reverse(key.controls.begin(), key.controls.end());
                std::reverse(key.controlsZ.begin(), key.controlsZ.end());
            } else {
                key.controls.clear();
                key.controlsZ.clear();
            }
        }
        for (int i = 0; i + 1 < original.size(); ++i) {
            int parts = original[i].tween ? 0 : original[i].parts;
            for (ValueCurve const& curve : original[i].curves)
                if (curve.after == ValueCurve::Held)
                    parts |= 1 << curve.value;
            int const at = frames - original[i + 1].frame;
            if (!parts || at >= frames - 1 - original[i].frame)
                continue;
            Keyframe step;
            step.frame = at;
            step.parts = parts;
            step.pose = original[i].pose;
            step.tween = false;
            row.keys << step;
        }
        std::sort(row.keys.begin(), row.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
    }
    return out;
}

CellBlock CellBlock::cycled(int times, int kind) const
{
    if (times < 2 || kind == Repeat)
        return repeated(times);
    CellBlock out = *this;
    out.frames = frames * times;
    CellBlock const back = reversed();
    for (int r = 0; r < rows.size(); ++r) {
        Row const& row = rows[r];
        // Repeat and Offset: each time on by as far as the keyframes went from the first to the last.
        Pose step;
        if (kind == RepeatOffset && row.keys.size() >= 2) {
            Pose const& a = row.keys.first().pose;
            Pose const& b = row.keys.last().pose;
            step.x = b.x - a.x;
            step.y = b.y - a.y;
            step.z = b.z - a.z;
            step.rotation = b.rotation - a.rotation;
        }
        for (int n = 1; n < times; ++n) {
            bool const there = kind == PingPong && n % 2 == 1;
            Row const& from = there ? back.rows[r] : row;
            out.rows[r].cells += from.cells;
            for (Keyframe key : from.keys) {
                key.frame += n * frames;
                if (kind == RepeatOffset) {
                    key.pose.x += step.x * n;
                    key.pose.y += step.y * n;
                    key.pose.z += step.z * n;
                    key.pose.rotation += step.rotation * n;
                }
                out.rows[r].keys.push_back(key);
            }
        }
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

bool CellBlock::forceEdgeKeys = true;

CellBlock Document::copyCells(QVector<int> const& layers, int first, int last, bool edgeKeys) const
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
            for (PointMove& point : key.pose.points)
                point.drawing = int(seen.indexOf(point.drawing));
            key.pose.points.removeIf([](PointMove const& point) { return point.drawing < 0; });
            row.keys.push_back(key);
        }
        if (edgeKeys && !layer.keys.isEmpty()) {
            // Keyframes where the block begins and ends, holding the motion as it is there (pose only: no points of drawings).
            for (int f : {first, last}) {
                if (std::any_of(row.keys.begin(), row.keys.end(), [&](Keyframe const& k) { return k.frame == f - first; }))
                    continue;
                Keyframe edge;
                edge.frame = f - first;
                edge.pose = layer.poseAt(f);
                edge.pose.points.clear();
                row.keys.push_back(edge);
            }
            std::sort(row.keys.begin(), row.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
        }
        row.layerName = layer.name;
        row.pivot = layer.pivot;
        block.rows.push_back(row);
    }
    return block;
}

void Document::moveCells(QVector<int> const& from, int first, int last, QVector<int> const& to, int frame, bool copy, bool insert)
{
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    CellBlock const block = copyCells(from, first, last);
    if (block.isEmpty() || (!copy && from == to && frame == first))
        return;
    apply(copy ? tr("Copy Cells") : tr("Move Cells"), [&](Scene& s) {
        if (!copy)
            emptyCells(s, from, first, last);
        if (insert)
            for (int t : to)
                if (t >= 0 && t < s.layers.size())
                    makeRoom(s, t, frame, last - first + 1);
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
    int const delta = frame - now.frame;
    auto rippled = [&](Scene::Marker const& m) { return m_rippleMarkers && m.frame > now.frame; };
    for (int i = 0; i < m_scene.markers.size(); ++i) {
        Scene::Marker const& other = m_scene.markers[i];
        if (i != index && !rippled(other) && other.frame < frame + now.length && frame < other.frame + other.length)
            return false;
    }
    apply(tr("Move Scene Marker"), [&](Scene& s) {
        // Ripple: the markers after it go as far.
        for (int i = 0; i < s.markers.size(); ++i)
            if (i != index && rippled(s.markers[i]))
                s.markers[i].frame = std::clamp(s.markers[i].frame + delta, 0, std::max(0, s.frameCount() - s.markers[i].length));
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

bool Document::setMarker(int index, Scene::Marker const& changed)
{
    if (index < 0 || index >= m_scene.markers.size())
        return false;
    Scene::Marker marker = changed;
    marker.frame = std::clamp(marker.frame, 0, m_scene.frameCount() - 1);
    marker.length = std::clamp(marker.length, 1, m_scene.frameCount() - marker.frame);
    for (int i = 0; i < m_scene.markers.size(); ++i)
        if (i != index && m_scene.markers[i].frame == marker.frame)
            return false;   // two markers cannot start on the same frame
    if (m_scene.markers[index] == marker)
        return true;
    apply(tr("Edit Scene Marker"), [&](Scene& s) {
        s.markers.removeAt(index);
        int at = 0;
        while (at < s.markers.size() && s.markers[at].frame < marker.frame)
            ++at;
        s.markers.insert(at, marker);
    });
    return true;
}

void Document::setSoloMode(bool on)
{
    if (m_scene.soloMode == on)
        return;
    apply(on ? tr("Solo Mode On") : tr("Solo Mode Off"), [&](Scene& s) { s.soloMode = on; });
}
