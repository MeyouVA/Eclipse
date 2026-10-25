#include "model/document/Commands.h"
#include "model/document/CommandsPrivate.h"

void Document::previewExposureCells(Scene const& before, int layer, QVector<int> const& cells)
{
    if (layer < 0 || layer >= before.layers.size() || !before.layers[layer].holdsDrawings() || before.layers[layer].locked || cells.size() != before.frameCount())
        return;
    for (int drawing : cells)
        if (drawing < Layer::kNoDrawing || drawing >= before.layers[layer].drawings.size())
            return;
    Scene after = before;
    after.layers[layer].cells = cells;
    after.layers[layer].compact();
    after.followSyncedTiming(before);
    after.followClones(before);
    for (Layer& edited : after.layers)
        edited.tidyKeyExposures();
    pushUndo(new SceneStateCommand(this, m_scene, std::move(after), tr("Extend Exposure")));
}

bool Document::extendExposureTo(int layer, int from, int to, bool insert)
{
    if (!validCell(m_scene, layer, from) || to < from || to > 999999)
        return false;
    Layer const& current = m_scene.layers[layer];
    if (!current.holdsDrawings() || current.locked)
        return false;
    int source = from;
    while (source >= 0 && current.drawingAt(source) == Layer::kNoDrawing)
        --source;
    if (source < 0 || to <= source)
        return false;
    int const drawing = current.cells[source];
    bool changed = insert;
    for (int f = source + 1; f <= to && !changed; ++f)
        changed = current.cells.value(f, Layer::kNoDrawing) != drawing;
    if (!changed)
        return false;
    apply(tr("Extend Exposure"), [&](Scene& scene) {
        int const count = to - source;
        if (insert) {
            scene.layers[layer].cells.insert(source + 1, count, drawing);
            scene.layers[layer].shiftKeyExposures(source + 1, count);
            int last = int(scene.layers[layer].cells.size()) - 1;
            while (last > to && scene.layers[layer].cells[last] == Layer::kNoDrawing)
                --last;
            if (last >= scene.frameCount())
                scene.setFrameCount(last + 1);
            else
                scene.layers[layer].cells.resize(scene.frameCount());
        } else {
            if (to >= scene.frameCount())
                scene.layers[layer].cells.resize(to + 1, Layer::kNoDrawing);
            for (int f = source + 1; f <= to; ++f)
                scene.layers[layer].cells[f] = drawing;
            if (to >= scene.frameCount())
                scene.setFrameCount(to + 1);
        }
        scene.layers[layer].compact();
    });
    return true;
}
