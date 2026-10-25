#include "model/scene/Scene.h"
#include "model/scene/SceneTimingShared.h"
#include <QHash>
#include <QSet>

QVector<int> drawingRemap(Layer const& before, Layer const& after)
{
    QVector<int> origins(before.drawings.size(), -1);
    QHash<quint64, int> current;
    for (int drawing = 0; drawing < after.drawings.size(); ++drawing)
        current.insert(after.drawings[drawing].editIdentity, drawing);
    for (int drawing = 0; drawing < before.drawings.size(); ++drawing)
        origins[drawing] = current.value(before.drawings[drawing].editIdentity, -1);
    return origins;
}

void copyLayerTiming(Layer const& source, Layer& target, Layer const* beforeSource, Layer const* beforeTarget)
{
    QHash<int, int> standsFor;
    if (beforeSource && beforeTarget) {
        QHash<int, int> oldPairs, targetNow;
        QVector<int> const sourceOrigins = drawingRemap(source, *beforeSource), targetOrigins = drawingRemap(target, *beforeTarget);
        for (int frame = 0; frame < beforeSource->cells.size() && frame < beforeTarget->cells.size(); ++frame)
            if (int const drawing = beforeSource->drawingAt(frame), own = beforeTarget->drawingAt(frame); drawing >= 0 && own >= 0 && !oldPairs.contains(drawing))
                oldPairs.insert(drawing, own);
        for (int own = 0; own < target.drawings.size(); ++own)
            if (int const old = targetOrigins[own]; old >= 0)
                targetNow.insert(old, own);
        for (int drawing = 0; drawing < source.drawings.size(); ++drawing) {
            int const old = sourceOrigins[drawing];
            int const own = oldPairs.value(old, -1);
            if (targetNow.contains(own))
                standsFor.insert(drawing, targetNow[own]);
        }
    }
    QSet<int> spoken;
    for (int own : std::as_const(standsFor))
        spoken.insert(own);
    QVector<int> cells(target.cells.size(), Layer::kNoDrawing);
    for (int frame = 0; frame < cells.size() && frame < source.cells.size(); ++frame) {
        int const shown = source.cells[frame];
        if (shown == Layer::kNoDrawing)
            continue;
        auto found = standsFor.constFind(shown);
        if (found == standsFor.constEnd()) {
            int own = target.cells[frame];
            if (own == Layer::kNoDrawing || spoken.contains(own)) {
                own = int(target.drawings.size());
                target.drawings.push_back(Drawing());
            }
            spoken.insert(own);
            found = standsFor.insert(shown, own);
        }
        cells[frame] = *found;
    }
    target.cells = cells;
    target.keyExposures = source.keyExposures;
}

void Scene::copyTiming(int from, int to)
{
    if (from >= 0 && from < layers.size() && to >= 0 && to < layers.size() && from != to)
        copyLayerTiming(layers[from], layers[to]);
}
