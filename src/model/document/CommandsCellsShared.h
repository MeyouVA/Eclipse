// Eclipse - the document's cell commands: what their files share.
#pragma once

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"

namespace commands_cells_shared {

inline void makeRoom(Scene& s, int layer, int frame, int count)
{
    Layer& l = s.layers[layer];
    if (frame < 0 || frame >= l.cells.size() || count < 1)
        return;
    l.cells.insert(frame, count, Layer::kNoDrawing);
    l.cells.resize(s.frameCount());
    l.shiftKeyExposures(frame, count);
    for (Keyframe& key : l.keys)
        if (key.frame >= frame)
            key.frame += count;
    l.keys.erase(std::remove_if(l.keys.begin(), l.keys.end(), [&](Keyframe const& k) { return k.frame >= s.frameCount(); }), l.keys.end());
}

// No drawing shown and no keyframe in the frames `first`..`last` of these layers.
inline void emptyCells(Scene& s, QVector<int> const& layers, int first, int last)
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

// Where the row's drawings go in the layer, as Paste Special asks: by their names, made, overwritten or renamed (-1: left out).
inline QVector<int> placeDrawings(Layer& layer, CellBlock::Row const& row, int mode)
{
    QVector<int> place(row.drawings.size(), -1);
    for (int i = 0; i < row.drawings.size(); ++i) {
        QString const label = row.labels.value(i);
        int found = -1;
        for (int d = 0; d < layer.drawings.size() && found < 0; ++d)
            if (layer.drawingLabel(d) == label && layer.drawings[d].set == row.drawings[i].set)
                found = d;
        if (mode == CellBlock::TimingOnly) {
            place[i] = found;
            continue;
        }
        if (found >= 0 && mode == CellBlock::CreateMissing) {
            place[i] = found;
            continue;
        }
        Drawing made = row.drawings[i];
        if (found >= 0 && mode == CellBlock::CreateOverwrite) {
            made.name = layer.drawings[found].name;
            made.pinned = layer.drawings[found].pinned;
            layer.drawings[found] = made;
            place[i] = found;
            continue;
        }
        // A drawing of its own; with Rename, an underscore added while the name is taken.
        QString name = label;
        auto taken = [&](QString const& n) {
            for (int d = 0; d < layer.drawings.size(); ++d)
                if (layer.drawingLabel(d) == n && layer.drawings[d].set == made.set)
                    return true;
            return false;
        };
        while (taken(name))
            name += QLatin1Char('_');
        made.name = name;
        place[i] = int(layer.drawings.size());
        layer.drawings.push_back(made);
    }
    return place;
}

// Puts a block down from `frame` on, its rows onto these layers in turn (or, matching names, onto the layers of their names).
inline void putBlock(Scene& s, QVector<int> const& layers, int frame, CellBlock const& block)
{
    int const begin = std::max(frame, 0);
    int const end = std::min(frame + block.frames, s.frameCount());  // one past the last frame written
    for (int r = 0; r < block.rows.size() && (r < layers.size() || block.matchNames); ++r) {
        CellBlock::Row const& row = block.rows[r];
        int l = r < layers.size() ? layers[r] : -1;
        if (block.matchNames && !row.layerName.isEmpty()) {
            l = -1;
            for (int candidate : layers)
                if (candidate >= 0 && candidate < s.layers.size() && s.layers[candidate].name == row.layerName)
                    l = candidate;
            for (int candidate = 0; candidate < s.layers.size() && l < 0; ++candidate)
                if (s.layers[candidate].name == row.layerName)
                    l = candidate;
        }
        if (l < 0 || l >= s.layers.size())
            continue;
        Layer& layer = s.layers[l];
        if (block.pastePivot)
            layer.pivot = row.pivot;
        QVector<Keyframe> pastedKeys = row.keys;
        if (layer.holdsDrawings() && row.holdsDrawings) {
            // Synced layers: the drawings of the other drawing layers left out.
            QString const set = layer.drawingAt(begin) != Layer::kNoDrawing ? layer.drawings[layer.drawingAt(begin)].set : QString();
            auto other = [&](int cell) { return block.currentSetOnly && cell >= 0 && cell < row.drawings.size() && row.drawings[cell].set != set; };
            QVector<int> place;
            if (block.drawingMode == CellBlock::CopyDrawings) {
                int const base = int(layer.drawings.size());
                layer.drawings += row.drawings;
                for (int i = 0; i < row.drawings.size(); ++i)
                    place.push_back(base + i);
            } else {
                place = placeDrawings(layer, row, block.drawingMode);
            }
            for (int f = begin; f < end; ++f) {
                int const cell = row.cells[f - frame];
                if (other(cell))
                    continue;
                if (cell >= 0 && place.value(cell, -1) < 0 && block.drawingMode == CellBlock::TimingOnly)
                    continue;       // no drawing of that name here: the cell is left as it is
                layer.cells[f] = cell < 0 ? Layer::kNoDrawing : place.value(cell, Layer::kNoDrawing);
            }
            for (Keyframe& key : pastedKeys) {
                for (PointMove& point : key.pose.points)
                    point.drawing = place.value(point.drawing, -1);
                key.pose.points.removeIf([](PointMove const& point) { return point.drawing < 0; });
            }
        }
        struct CompactOnExit { Layer& layer; ~CompactOnExit() { layer.compact(); } } compact{layer};
        if (block.leaveKeys)
            continue;
        if ((block.keyParts & Keyframe::AllParts) != Keyframe::AllParts) {
            // Some parts only: each key pasted sets those parts, on a key of the layer's where there is one.
            for (Keyframe key : pastedKeys) {
                key.frame += frame;
                if (key.frame < begin || key.frame >= end)
                    continue;
                int const parts = key.parts & block.keyParts;
                if (parts == 0)
                    continue;
                auto here = std::find_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame == key.frame; });
                Pose pose = here != layer.keys.end() ? here->pose : layer.poseAt(key.frame);
                if (parts & Keyframe::PartX)
                    pose.x = key.pose.x;
                if (parts & Keyframe::PartY)
                    pose.y = key.pose.y;
                if (parts & Keyframe::PartZ)
                    pose.z = key.pose.z;
                if (parts & Keyframe::PartAngle)
                    pose.rotation = key.pose.rotation;
                if (parts & Keyframe::PartScaleX)
                    pose.scaleX = key.pose.scaleX;
                if (parts & Keyframe::PartScaleY)
                    pose.scaleY = key.pose.scaleY;
                if (parts & Keyframe::PartSkew)
                    pose.skew = key.pose.skew;
                if (parts & Keyframe::PartOpacity)
                    pose.opacity = key.pose.opacity;
                if (parts & Keyframe::PartFov)
                    pose.fov = key.pose.fov;
                if (parts & Keyframe::PartRest) {
                    pose.rotX = key.pose.rotX;
                    pose.rotY = key.pose.rotY;
                    pose.quad = key.pose.quad;
                    pose.bend = key.pose.bend;
                    pose.warp = key.pose.warp;
                    pose.fx = key.pose.fx;
                    pose.points = key.pose.points;
                }
                if (here != layer.keys.end()) {
                    here->pose = pose;
                    here->parts |= parts;
                    for (ValueCurve const& curve : key.curves)
                        if (parts & (1 << curve.value))
                            here->setCurve(curve);
                } else {
                    key.pose = pose;
                    key.parts = parts;
                    layer.keys.push_back(key);
                }
            }
            std::sort(layer.keys.begin(), layer.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
            continue;
        }
        if (block.offsetKeys) {
            // Offset Keyframes: what the layer has at each key's frame, moved, turned and scaled by the key pasted.
            for (Keyframe key : pastedKeys) {
                key.frame += frame;
                if (key.frame < begin || key.frame >= end)
                    continue;
                Pose const there = layer.poseAt(key.frame);
                key.pose.x += there.x;
                key.pose.y += there.y;
                key.pose.z += there.z;
                key.pose.rotation += there.rotation;
                key.pose.skew += there.skew;
                key.pose.scaleX *= there.scaleX;
                key.pose.scaleY *= there.scaleY;
                auto here = std::find_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame == key.frame; });
                if (here != layer.keys.end())
                    *here = key;
                else
                    layer.keys.push_back(key);
            }
            std::sort(layer.keys.begin(), layer.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
            continue;
        }
        // The keyframes of the block take the place of those that were in its frames.
        layer.keys.erase(std::remove_if(layer.keys.begin(), layer.keys.end(), [&](Keyframe const& k) { return k.frame >= begin && k.frame < end; }),
                         layer.keys.end());
        for (Keyframe key : pastedKeys) {
            key.frame += frame;
            if (key.frame >= begin && key.frame < end)
                layer.keys.push_back(key);
        }
        std::sort(layer.keys.begin(), layer.keys.end(), [](Keyframe const& a, Keyframe const& b) { return a.frame < b.frame; });
    }
}

}  // namespace commands_cells_shared
