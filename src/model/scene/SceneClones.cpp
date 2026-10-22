// Eclipse - the scene: cloned, synced and shared-drawing layers; motion-path control points; the display.
#include "model/scene/Scene.h"

#include <algorithm>

#include <QHash>

int Scene::cloneLayer(int layer)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings())
        return -1;
    if (!layers[layer].showsSymbol()) {
        layer = makeSymbol({layer}, layers[layer].name);
        if (layer < 0)
            return -1;
    }
    Layer twin = layers[layer];
    twin.id = m_nextLayerId++;
    twin.name = uniqueLayerName(twin.name);
    twin.nodePlaced = false;
    twin.solo = false;
    layers.insert(layer + 1, twin);
    return layer + 1;
}

void Scene::copyTiming(int from, int to)
{
    if (from < 0 || from >= layers.size() || to < 0 || to >= layers.size() || from == to)
        return;
    Layer const& source = layers[from];
    Layer& target = layers[to];
    QHash<int, int> standsFor;
    QVector<int> spoken;
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
            spoken.push_back(own);
            found = standsFor.insert(shown, own);
        }
        cells[frame] = *found;
    }
    target.cells = cells;
}

void Scene::followSyncedTiming(Scene const& before)
{
    // The layer of each group whose timing this change touched (the first, should there be several).
    QHash<int, int> touched;
    QVector<bool> same(layers.size(), false);
    for (int l = 0; l < layers.size(); ++l) {
        if (layers[l].syncGroup == 0)
            continue;
        int const was = before.layerIndex(layers[l].id);
        // (A layer new to its group counts as untouched: it is the one that follows.)
        bool const changed = was >= 0 && before.layers[was].syncGroup == layers[l].syncGroup && before.layers[was].cells != layers[l].cells;
        same[l] = !changed;
        if (changed && !touched.contains(layers[l].syncGroup))
            touched.insert(layers[l].syncGroup, l);
    }
    for (int l = 0; l < layers.size(); ++l) {
        if (layers[l].syncGroup == 0 || !same[l])
            continue;
        auto leader = touched.constFind(layers[l].syncGroup);
        if (leader != touched.constEnd() && layers[*leader].cells.size() == layers[l].cells.size())
            copyTiming(*leader, l);
    }
}

void Scene::syncLayerWith(int layer, int other)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings() || layers[layer].showsSymbol())
        return;
    if (other < 0 || other >= layers.size() || other == layer || !layers[other].holdsDrawings() || layers[other].showsSymbol()) {
        layers[layer].syncGroup = 0;
    } else {
        if (layers[other].syncGroup == 0) {
            int next = 1;
            for (Layer const& l : std::as_const(layers))
                next = std::max(next, l.syncGroup + 1);
            layers[other].syncGroup = next;
        }
        layers[layer].syncGroup = layers[other].syncGroup;
        copyTiming(other, layer);
    }
    // A layer left alone in its group is synced with nothing.
    QHash<int, int> members;
    for (Layer const& l : std::as_const(layers))
        if (l.syncGroup != 0)
            ++members[l.syncGroup];
    for (Layer& l : layers)
        if (l.syncGroup != 0 && members.value(l.syncGroup) < 2)
            l.syncGroup = 0;
}

void Scene::followClones(Scene const& before)
{
    // The layer of each group this change touched (the first, should there be several).
    QHash<int, int> touched;
    QVector<bool> same(layers.size(), false);
    for (int l = 0; l < layers.size(); ++l) {
        if (layers[l].cloneGroup == 0)
            continue;
        int const was = before.layerIndex(layers[l].id);
        // (A layer new to its group counts as untouched: it is the one that follows.)
        bool changed = false;
        if (was >= 0 && before.layers[was].cloneGroup == layers[l].cloneGroup) {
            Layer const& old = before.layers[was];
            // (Drawings that were not touched are still the very ones the scene had before.)
            bool const drawingsChanged = old.drawings.size() != layers[l].drawings.size() || old.drawings.constData() != layers[l].drawings.constData();
            changed = drawingsChanged || (layers[l].cloneTiming && old.cells != layers[l].cells);
        }
        same[l] = !changed;
        if (changed && !touched.contains(layers[l].cloneGroup))
            touched.insert(layers[l].cloneGroup, l);
    }
    for (int l = 0; l < layers.size(); ++l) {
        if (layers[l].cloneGroup == 0 || !same[l])
            continue;
        auto leader = touched.constFind(layers[l].cloneGroup);
        if (leader == touched.constEnd() || layers[*leader].cells.size() != layers[l].cells.size())
            continue;
        if (!layers[*leader].cloneTiming) {
            for (Drawing& d : layers[*leader].drawings)
                d.pinned = true;
            layers[l].drawings = layers[*leader].drawings;
            continue;
        }
        layers[l].drawings = layers[*leader].drawings;
        layers[l].cells = layers[*leader].cells;
        layers[l].keyExposures = layers[*leader].keyExposures;
    }
}

int Scene::addCloneLayer(int layer, bool timing)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings() || layers[layer].showsSymbol())
        return -1;
    int const id = layers[layer].id, parent = layers[layer].parent;
    int const at = addLayer(layers[layer].name + QStringLiteral(" Clone"), layer + 1);
    int const from = layerIndex(id);
    if (layers[from].cloneGroup == 0) {
        int next = 1;
        for (Layer const& l : std::as_const(layers))
            next = std::max(next, l.cloneGroup + 1);
        layers[from].cloneGroup = next;
    }
    layers[at].parent = parent;
    layers[at].parentBone = layers[from].parentBone;
    layers[at].cloneGroup = layers[from].cloneGroup;
    // (A layer already a clone of the one kind stays it; a new group is of the kind asked for.)
    if (layers[from].cloneGroup != 0 && std::count_if(layers.begin(), layers.end(), [&](Layer const& o) { return o.cloneGroup == layers[from].cloneGroup; }) == 2)
        layers[from].cloneTiming = timing;
    layers[at].cloneTiming = layers[from].cloneTiming;
    if (!layers[at].cloneTiming)
        for (Drawing& d : layers[from].drawings)
            d.pinned = true;
    layers[at].bitmap = layers[from].bitmap;
    layers[at].bitmapArt = layers[from].bitmapArt;
    layers[at].drawings = layers[from].drawings;
    layers[at].cells = layers[from].cells;
    layers[at].keyExposures = layers[from].keyExposures;
    return at;
}

void Scene::makeCloneIndependent(int layer)
{
    if (layer < 0 || layer >= layers.size() || layers[layer].cloneGroup == 0)
        return;
    int const group = layers[layer].cloneGroup;
    layers[layer].cloneGroup = 0;
    // A layer left alone in its group is a clone of nothing.
    int members = 0;
    for (Layer const& l : std::as_const(layers))
        members += l.cloneGroup == group ? 1 : 0;
    if (members < 2)
        for (Layer& l : layers)
            if (l.cloneGroup == group)
                l.cloneGroup = 0;
}

bool Scene::useDrawingsOf(int layer, int source)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings() || layers[layer].showsSymbol())
        return false;
    if (source < 0) {
        if (layers[layer].cloneGroup == 0)
            return false;
        makeCloneIndependent(layer);
        return true;
    }
    if (source >= layers.size() || source == layer || !layers[source].holdsDrawings() || layers[source].showsSymbol())
        return false;
    if (layers[source].cloneGroup != 0 && layers[source].cloneTiming)
        return false;       // shared with their timing: that is a clone, not an element
    if (layers[layer].cloneGroup != 0 && layers[layer].cloneGroup == layers[source].cloneGroup)
        return false;       // it shows those already
    makeCloneIndependent(layer);
    if (layers[source].cloneGroup == 0) {
        int next = 1;
        for (Layer const& l : std::as_const(layers))
            next = std::max(next, l.cloneGroup + 1);
        layers[source].cloneGroup = next;
        layers[source].cloneTiming = false;
    }
    for (Drawing& d : layers[source].drawings)
        d.pinned = true;
    Layer& mine = layers[layer];
    mine.cloneGroup = layers[source].cloneGroup;
    mine.cloneTiming = false;
    mine.bitmap = layers[source].bitmap;
    mine.bitmapArt = layers[source].bitmapArt;
    mine.drawings = layers[source].drawings;
    // Its timing stays its own; a frame that showed a drawing the source has not got shows nothing.
    for (int& cell : mine.cells)
        if (cell >= mine.drawings.size())
            cell = -1;
    return true;
}

int Scene::drawingsSource(int layer) const
{
    if (layer < 0 || layer >= layers.size() || layers[layer].cloneGroup == 0 || layers[layer].cloneTiming)
        return -1;
    for (int l = 0; l < layers.size(); ++l)
        if (l != layer && layers[l].cloneGroup == layers[layer].cloneGroup)
            return l;
    return -1;
}

int Scene::insertControlPoint(int layer, int frame)
{
    if (layer < 0 || layer >= layers.size())
        return -1;
    Layer& l = layers[layer];
    for (int k = 0; k + 1 < l.keys.size(); ++k) {
        Keyframe& a = l.keys[k];
        Keyframe const& b = l.keys[k + 1];
        if (frame <= a.frame || frame >= b.frame)
            continue;
        if (!a.tween)
            return -1;
        double const t = double(frame - a.frame) / double(b.frame - a.frame);
        double const along = std::clamp(easedProgress(t, a.easeOut, b.easeIn, a.liftOut, b.liftIn), 0.0, 1.0);
        QVector<double> at;
        QPointF const from(a.pose.x, a.pose.y), to(b.pose.x, b.pose.y);
        a.pathLine(from, to, &at);
        QPointF const here = a.pathPoint(from, to, along);
        int place = 0;
        while (place < at.size() && at[place] < along)
            ++place;
        a.controls.insert(place, here);
        if (!a.controlsZ.isEmpty()) {
            // Its depth where the layer is now, so the way is as it was.
            double const z = a.pathDepth(a.pose.z, b.pose.z, along, at);
            while (a.controlsZ.size() < place)
                a.controlsZ.push_back(a.pose.z + (b.pose.z - a.pose.z) * at.value(a.controlsZ.size(), 0.0));
            a.controlsZ.insert(std::min(place, int(a.controlsZ.size())), z);
        }
        return place;
    }
    return -1;
}

int Scene::controlPointFrame(int layer, int key, int control) const
{
    if (layer < 0 || layer >= layers.size())
        return -1;
    Layer const& l = layers[layer];
    if (key < 0 || key + 1 >= l.keys.size() || control < 0 || control >= l.keys[key].controls.size())
        return -1;
    QPointF const want = l.keys[key].controls[control];
    int best = -1;
    double nearest = 1e300;
    for (int f = l.keys[key].frame + 1; f < l.keys[key + 1].frame; ++f) {
        Pose const p = l.poseAt(f);
        double const d = std::hypot(p.x - want.x(), p.y - want.y());
        if (d < nearest) {
            nearest = d;
            best = f;
        }
    }
    return best;
}

int Scene::lockControlPointInTime(int layer, int key, int control)
{
    int const frame = controlPointFrame(layer, key, control);
    if (frame < 0)
        return -1;
    Layer& l = layers[layer];
    Keyframe const before = l.keys[key];
    Pose pose = l.poseAt(frame);
    pose.x = before.controls[control].x();
    pose.y = before.controls[control].y();
    if (control < before.controlsZ.size())
        pose.z = before.controlsZ[control];
    l.setKey(frame, pose);
    // The points before it stay on the way to the new keyframe; those after it go on from there.
    Keyframe& first = l.keys[l.keyIndex(before.frame)];
    Keyframe& second = l.keys[l.keyIndex(frame)];
    first.controls = before.controls.mid(0, control);
    second.controls = before.controls.mid(control + 1);
    first.controlsZ = before.controlsZ.mid(0, std::min(control, int(before.controlsZ.size())));
    second.controlsZ = control + 1 < before.controlsZ.size() ? before.controlsZ.mid(control + 1) : QVector<double>();
    second.tension = before.tension;
    second.continuity = before.continuity;
    second.bias = before.bias;
    second.tween = before.tween;
    return frame;
}

bool Scene::inDisplay(int layer) const
{
    int const shown = display != 0 ? layerIndex(display) : -1;
    if (shown < 0)
        return true;
    for (int at = layer, guard = 0; at >= 0 && guard < 256; at = parentIndex(at), ++guard)
        if (at == shown)
            return true;
    return false;
}

int Scene::addSyncedLayer(int layer)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings() || layers[layer].showsSymbol())
        return -1;
    int const id = layers[layer].id, parent = layers[layer].parent;
    int const at = addLayer(layers[layer].name + QStringLiteral(" Sync"), layer + 1);
    int const from = layerIndex(id);
    layers[at].parent = parent;
    layers[at].parentBone = layers[from].parentBone;
    syncLayerWith(at, from);
    return at;
}
