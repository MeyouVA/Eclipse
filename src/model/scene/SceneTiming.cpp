#include "model/scene/Scene.h"
#include "model/scene/SceneTimingShared.h"
#include <QHash>
#include <QSet>

static bool sameDrawingStorage(Drawing const& a, Drawing const& b)
{
    if (a.name != b.name || a.set != b.set || a.model != b.model || a.pictures.constData() != b.pictures.constData())
        return false;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        if (a.strokesOf(sheet).constData() != b.strokesOf(sheet).constData() || a.fillsOf(sheet).constData() != b.fillsOf(sheet).constData())
            return false;
    return true;
}

static QVector<int> drawingOrigins(Layer const& current, Layer const& before)
{
    QVector<int> origins(current.drawings.size(), -1);
    QSet<int> used;
    for (int drawing = 0; drawing < current.drawings.size() && drawing < before.drawings.size(); ++drawing)
        if (sameDrawingStorage(current.drawings[drawing], before.drawings[drawing])) {
            origins[drawing] = drawing;
            used.insert(drawing);
        }
    for (int drawing = 0; drawing < current.drawings.size(); ++drawing) {
        if (origins[drawing] >= 0)
            continue;
        for (int old = 0; old < before.drawings.size(); ++old)
            if (!used.contains(old) && sameDrawingStorage(current.drawings[drawing], before.drawings[old])) {
                origins[drawing] = old;
                used.insert(old);
                break;
            }
    }
    return origins;
}

void copyLayerTiming(Layer const& source, Layer& target, Layer const* beforeSource, Layer const* beforeTarget)
{
    QHash<int, int> standsFor;
    if (beforeSource && beforeTarget) {
        QHash<int, int> oldPairs, targetNow;
        QVector<int> const sourceOrigins = drawingOrigins(source, *beforeSource), targetOrigins = drawingOrigins(target, *beforeTarget);
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
