// Eclipse - the scene: symbols - layers kept together under a name, and shown by other layers as often as wanted.
#include "model/scene/Scene.h"

#include <algorithm>

#include <QHash>

Scene::Symbol const* Scene::findSymbol(int id) const
{
    for (Symbol const& symbol : symbols)
        if (symbol.id == id)
            return &symbol;
    return nullptr;
}

Scene Scene::symbolScene(Symbol const& symbol) const
{
    // The same size, the same colours, the same symbols to show in turn: only the layers and the length are its own.
    Scene inner = *this;
    inner.layers = symbol.layers;
    inner.m_frameCount = std::max(1, symbol.frameCount);
    inner.markers.clear();
    inner.guides.clear();
    inner.metadata.clear();
    inner.references.clear();
    for (Layer& layer : inner.layers)
        layer.cells.resize(inner.m_frameCount, Layer::kNoDrawing);
    return inner;
}

Drawing Scene::symbolPicture(int symbolId, int frame, int depth) const
{
    Drawing out;
    Symbol const* const symbol = findSymbol(symbolId);
    if (!symbol || depth > kSymbolDepth)
        return out;
    Scene const inner = symbolScene(*symbol);
    int const f = std::clamp(frame, 0, inner.frameCount() - 1);
    for (int l : inner.drawOrder(f)) {
        Layer const& layer = inner.layers[l];
        int const index = layer.drawingAt(f);
        if (!inner.enabled(l) || index == Layer::kNoDrawing || !layer.holdsDrawings())
            continue;
        Drawing const shown = layer.showsSymbol() ? inner.symbolPicture(layer.symbolId, index, depth + 1) : layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index];
        QTransform const world = inner.worldTransform(l, f);
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            for (Fill fill : shown.fillsOf(sheet)) {
                fill.transform(world);
                out.fillsOf(sheet).push_back(fill);
            }
            for (Stroke stroke : shown.strokesOf(sheet)) {
                stroke.transform(world);
                out.strokesOf(sheet).push_back(stroke);
            }
        }
        for (Picture picture : shown.pictures) {
            picture.transform(world);
            out.pictures.push_back(picture);
        }
    }
    return out;
}

void Scene::refreshSymbolInstances(int symbolId)
{
    for (Layer& layer : layers) {
        if (!layer.showsSymbol() || (symbolId != 0 && layer.symbolId != symbolId))
            continue;
        Symbol const* const symbol = findSymbol(layer.symbolId);
        int const count = symbol ? std::max(1, symbol->frameCount) : 1;
        // A drawing for each frame of the symbol: what it shows there, all in one - to be picked and measured by
        // (it is the symbol itself that is painted).
        QVector<Drawing> made(count);
        for (int i = 0; i < count; ++i) {
            if (symbol)
                made[i] = symbolPicture(symbol->id, i);
            made[i].name = QString::number(i + 1);
            made[i].pinned = true;
        }
        layer.drawings = made;
        for (int& cell : layer.cells)
            if (cell >= count)
                cell = count - 1;
    }
}

int Scene::makeSymbol(QVector<int> const& picked, QString const& name)
{
    // Those picked, and everything attached to them.  A camera picked goes in, as in Harmony (an ordinary layer there: the
    // scene is seen through its own camera, never one inside a symbol); one only attached to them stays in the scene.  A
    // sound picked goes in, and plays where the symbol is shown.
    QVector<bool> in(layers.size(), false);
    for (int l : picked)
        if (l >= 0 && l < layers.size())
            in[l] = true;
    for (bool grew = true; grew;) {
        grew = false;
        for (int l = 0; l < layers.size(); ++l) {
            int const parent = layerIndex(layers[l].parent);
            if (!in[l] && parent >= 0 && in[parent] && layers[l].kind != Layer::Camera && layers[l].kind != Layer::Sound) {
                in[l] = true;
                grew = true;
            }
        }
    }
    int top = -1, count = 0;
    for (int l = 0; l < layers.size(); ++l) {
        if (in[l]) {
            top = l;
            ++count;
        }
    }
    if (count == 0)
        return -1;
    Symbol symbol;
    symbol.id = 1;
    for (Symbol const& other : std::as_const(symbols))
        symbol.id = std::max(symbol.id, other.id + 1);
    symbol.frameCount = m_frameCount;
    {
        QString const base = name.trimmed().isEmpty() ? QStringLiteral("Symbol") : name.trimmed();
        QString unique = base;
        auto taken = [&](QString const& candidate) {
            for (Symbol const& other : std::as_const(symbols))
                if (other.name == candidate)
                    return true;
            return false;
        };
        for (int n = 2; taken(unique); ++n)
            unique = QStringLiteral("%1 %2").arg(base).arg(n);
        symbol.name = unique;
    }
    // What its topmost layers were attached to outside: the layer that shows the symbol is attached there instead
    // (when they all agree), so that it stays where it was.
    int outside = 0;
    bool agreed = true, first = true;
    for (int l = 0; l < layers.size(); ++l) {
        if (!in[l])
            continue;
        int const parent = layerIndex(layers[l].parent);
        if (parent >= 0 && in[parent])
            continue;
        if (first)
            outside = layers[l].parent;
        else
            agreed = agreed && layers[l].parent == outside;
        first = false;
    }
    if (!agreed)
        outside = 0;
    for (int l = 0; l < layers.size(); ++l) {
        if (!in[l])
            continue;
        Layer inner = layers[l];
        int const parent = layerIndex(inner.parent);
        if (parent < 0 || !in[parent]) {
            inner.parent = 0;
            inner.parentBone = -1;
        }
        symbol.layers.push_back(inner);
    }
    Layer shows;
    shows.id = m_nextLayerId++;
    shows.kind = Layer::DrawingLayer;
    shows.pivot = QPointF(width / 2.0, height / 2.0);
    shows.symbolId = symbol.id;
    shows.parent = layerIndex(outside) >= 0 ? outside : 0;
    shows.cells.resize(m_frameCount);
    for (int f = 0; f < m_frameCount; ++f)
        shows.cells[f] = f;         // frame for frame, as the layers played before
    // The camera the scene is seen through, gone into the symbol: a copy of it stays, so that the scene is seen as before.
    int const seenThrough = cameraIndex();
    bool const keepCamera = seenThrough >= 0 && in[seenThrough];
    Layer keptCamera;
    if (keepCamera) {
        keptCamera = layers[seenThrough];
        keptCamera.id = m_nextLayerId++;
        int const up = layerIndex(keptCamera.parent);
        if (up >= 0 && in[up]) {
            keptCamera.parent = 0;
            keptCamera.parentBone = -1;
        }
    }
    // A camera or a sound left behind that was attached to one of them follows the whole now.
    QVector<int> goneIds;
    for (int l = 0; l < layers.size(); ++l)
        if (in[l])
            goneIds.push_back(layers[l].id);
    for (int l = int(layers.size()) - 1; l >= 0; --l)
        if (in[l])
            layers.removeAt(l);
    for (Layer& left : layers) {
        if (goneIds.contains(left.parent)) {
            left.parent = shows.id;
            left.parentBone = -1;
        }
    }
    shows.name = uniqueLayerName(symbol.name);
    int const at = std::clamp(top - (count - 1), 0, int(layers.size()));
    layers.insert(at, shows);
    if (keepCamera) {
        layers.push_back(keptCamera);
        activeCamera = keptCamera.id;
    }
    symbols.push_back(symbol);
    refreshSymbolInstances(symbol.id);
    return at;
}

int Scene::addSymbolInstance(int symbolId, int at)
{
    Symbol const* const symbol = findSymbol(symbolId);
    if (!symbol)
        return -1;
    int const made = addLayer(symbol->name, at, Layer::DrawingLayer);
    Layer& shows = layers[made];
    shows.symbolId = symbolId;
    // Frame for frame from the start; past its end, its last frame is held.
    for (int f = 0; f < shows.cells.size(); ++f)
        shows.cells[f] = std::min(f, std::max(1, symbol->frameCount) - 1);
    refreshSymbolInstances(symbolId);
    return made;
}

int Scene::expandSymbol(int layer)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].showsSymbol())
        return -1;
    Symbol const* const found = findSymbol(layers[layer].symbolId);
    if (!found)
        return -1;
    Symbol const symbol = *found;
    // A peg in its place carries what the layer had: where it is among the layers, what it is attached to, how it moves.
    Layer peg = layers[layer];
    peg.kind = Layer::Peg;
    peg.symbolId = 0;
    peg.drawings.clear();
    peg.cells.fill(Layer::kNoDrawing, m_frameCount);
    peg.keyExposures.clear();
    peg.effects.clear();
    peg.morphs.clear();
    layers[layer] = peg;
    // Copies of the symbol's layers, under numbers of their own, attached as they were - the topmost to the peg.
    QHash<int, int> renumbered;
    for (Layer const& inner : symbol.layers)
        renumbered.insert(inner.id, m_nextLayerId++);
    int at = layer + 1;
    for (Layer inner : symbol.layers) {
        inner.id = renumbered.value(inner.id);
        if (renumbered.contains(inner.parent)) {
            inner.parent = renumbered.value(inner.parent);
        } else {
            inner.parent = peg.id;
            inner.parentBone = -1;
        }
        for (Effect& effect : inner.effects)
            if (renumbered.contains(effect.matte))
                effect.matte = renumbered.value(effect.matte);
        inner.cells.resize(m_frameCount, Layer::kNoDrawing);
        inner.name = uniqueLayerName(inner.name);
        layers.insert(at++, inner);
    }
    refreshSymbolInstances();
    return layer;
}

void Scene::takeSymbol(int symbolId, Scene const& worked)
{
    // (Symbols made, or changed, while inside this one are the scene's too.)
    QVector<Symbol> all = worked.symbols;
    bool found = false;
    for (Symbol& symbol : all) {
        if (symbol.id != symbolId)
            continue;
        symbol.layers = worked.layers;
        symbol.frameCount = worked.frameCount();
        found = true;
    }
    if (!found)
        return;
    symbols = all;
    palette = worked.palette;
    paletteNames = worked.paletteNames;
    paletteLinks = worked.paletteLinks;
    m_nextSwatchId = std::max(m_nextSwatchId, worked.m_nextSwatchId);
    m_nextLayerId = std::max(m_nextLayerId, worked.m_nextLayerId);
    refreshSymbolInstances();
}

int Scene::addEmptySymbol(QString const& name)
{
    Symbol symbol;
    symbol.id = 1;
    for (Symbol const& other : std::as_const(symbols))
        symbol.id = std::max(symbol.id, other.id + 1);
    QString const base = name.trimmed().isEmpty() ? QStringLiteral("Symbol") : name.trimmed();
    symbol.name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (Symbol const& other : std::as_const(symbols))
            taken = taken || other.name == symbol.name;
        if (!taken)
            break;
        symbol.name = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    symbol.frameCount = m_frameCount;
    Layer sheet;
    sheet.id = m_nextLayerId++;
    sheet.kind = Layer::DrawingLayer;
    sheet.name = QStringLiteral("Drawing");
    sheet.pivot = QPointF(width / 2.0, height / 2.0);
    sheet.cells.fill(Layer::kNoDrawing, m_frameCount);
    symbol.layers = {sheet};
    symbols.push_back(symbol);
    return symbol.id;
}

int Scene::duplicateSymbol(int layer)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].showsSymbol())
        return 0;
    Symbol const* const from = findSymbol(layers[layer].symbolId);
    if (!from)
        return 0;
    Symbol copy = *from;
    copy.id = 1;
    for (Symbol const& other : std::as_const(symbols))
        copy.id = std::max(copy.id, other.id + 1);
    QString const base = from->name;
    for (int n = 2;; ++n) {
        copy.name = QStringLiteral("%1 %2").arg(base).arg(n);
        bool taken = false;
        for (Symbol const& other : std::as_const(symbols))
            taken = taken || other.name == copy.name;
        if (!taken)
            break;
    }
    symbols.push_back(copy);
    layers[layer].symbolId = copy.id;
    refreshSymbolInstances(copy.id);
    return copy.id;
}

QVector<Scene::Symbol> Scene::symbolsUsed(QVector<Layer> const& by) const
{
    QVector<Symbol> used;
    QVector<int> wanted;
    for (Layer const& layer : by)
        if (layer.showsSymbol())
            wanted.push_back(layer.symbolId);
    while (!wanted.isEmpty()) {
        int const id = wanted.takeLast();
        bool have = false;
        for (Symbol const& one : std::as_const(used))
            have = have || one.id == id;
        Symbol const* const symbol = have ? nullptr : findSymbol(id);
        if (!symbol)
            continue;
        used.push_back(*symbol);
        for (Layer const& inner : symbol->layers)
            if (inner.showsSymbol())
                wanted.push_back(inner.symbolId);
    }
    return used;
}

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
    // Each drawing of the one stands for a drawing of the other: the one that was shown where it first
    // appears, when that is not spoken for already - else a new, empty one.
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
            // The drawings only: every one of them is kept in the bank, so that the places the frames of
            // each layer point to stay the same whatever the others show.
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
