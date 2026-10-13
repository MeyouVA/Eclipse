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
    // Those picked, and everything attached to them.  (A camera and a sound stay in the scene: a symbol is a picture.)
    QVector<bool> in(layers.size(), false);
    for (int l : picked)
        if (l >= 0 && l < layers.size() && layers[l].kind != Layer::Camera && layers[l].kind != Layer::Sound)
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
