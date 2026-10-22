// Eclipse - the document's cell commands: what their files share.
#pragma once

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"

namespace commands_cells_shared {

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

// Puts a block down from `frame` on, its rows onto these layers in turn.
inline void putBlock(Scene& s, QVector<int> const& layers, int frame, CellBlock const& block)
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

}  // namespace commands_cells_shared
