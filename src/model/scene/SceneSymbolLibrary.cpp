// Eclipse - the scene's symbols as the Library shows them: copied, renamed, counted and taken away.
#include "model/scene/Scene.h"

#include <QHash>

namespace {

QString freeName(QVector<Scene::Symbol> const& symbols, QString const& base, int except)
{
    QString name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (Scene::Symbol const& other : symbols)
            taken = taken || (other.id != except && other.name.compare(name, Qt::CaseInsensitive) == 0);
        if (!taken)
            return name;
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    }
}

}  // namespace

int Scene::copySymbol(int id)
{
    Symbol const* const from = findSymbol(id);
    if (!from)
        return 0;
    Symbol copy = *from;
    copy.id = 1;
    for (Symbol const& other : std::as_const(symbols))
        copy.id = std::max(copy.id, other.id + 1);
    copy.name = freeName(symbols, from->name + QStringLiteral(" copy"), 0);
    symbols.push_back(copy);
    return copy.id;
}

bool Scene::renameSymbol(int id, QString const& name)
{
    QString const clean = name.trimmed();
    if (clean.isEmpty() || !findSymbol(id))
        return false;
    for (Symbol const& other : std::as_const(symbols))
        if (other.id != id && other.name.compare(clean, Qt::CaseInsensitive) == 0)
            return false;
    for (Symbol& symbol : symbols)
        if (symbol.id == id)
            symbol.name = clean;
    return true;
}

int Scene::symbolReferences(int id) const
{
    int count = 0;
    for (Layer const& layer : layers)
        count += layer.showsSymbol() && layer.symbolId == id ? 1 : 0;
    for (Symbol const& symbol : symbols)
        for (Layer const& layer : symbol.layers)
            count += layer.showsSymbol() && layer.symbolId == id ? 1 : 0;
    return count;
}

int Scene::removeSymbol(int id)
{
    if (!findSymbol(id))
        return -1;
    int const counted = symbolReferences(id);
    // Inside other symbols: the layers that show it go, and what hung from them hangs from what they hung from.
    for (Symbol& symbol : symbols) {
        if (symbol.id == id)
            continue;
        QVector<Layer> kept;
        QHash<int, int> parentOf;
        for (Layer const& layer : std::as_const(symbol.layers))
            if (layer.showsSymbol() && layer.symbolId == id)
                parentOf.insert(layer.id, layer.parent);
        for (Layer layer : std::as_const(symbol.layers)) {
            if (parentOf.contains(layer.id))
                continue;
            for (int guard = 0; parentOf.contains(layer.parent) && guard < 64; ++guard)
                layer.parent = parentOf.value(layer.parent);
            kept.push_back(layer);
        }
        if (kept.isEmpty() && !symbol.layers.isEmpty()) {
            // A symbol keeps one layer: an empty drawing layer in place of what went.
            Layer sheet;
            sheet.id = m_nextLayerId++;
            sheet.name = QStringLiteral("Drawing");
            sheet.cells.fill(Layer::kNoDrawing, symbol.frameCount);
            kept.push_back(sheet);
        }
        symbol.layers = kept;
    }
    // In the scene: the layers that show it, the last first; a scene keeps one layer.
    for (int l = int(layers.size()) - 1; l >= 0; --l) {
        if (!layers[l].showsSymbol() || layers[l].symbolId != id)
            continue;
        if (layers.size() == 1) {
            layers[l].symbolId = 0;
            layers[l].drawings.clear();
            layers[l].cells.fill(Layer::kNoDrawing, m_frameCount);
            continue;
        }
        removeLayer(l);
    }
    symbols.removeIf([id](Symbol const& symbol) { return symbol.id == id; });
    refreshSymbolInstances();
    return counted;
}

int Scene::makeSymbolFromArt(int layer, int drawing, Drawing const& piece, QString const& name)
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].holdsDrawings() || drawing < 0 || drawing >= layers[layer].drawings.size())
        return -1;
    int const id = addEmptySymbol(name.trimmed().isEmpty() ? layers[layer].name : name);
    Layer const source = layers[layer];
    for (Symbol& symbol : symbols) {
        if (symbol.id != id)
            continue;
        symbol.frameCount = 1;
        Layer& sheet = symbol.layers.first();
        sheet.pivot = source.pivot;
        sheet.drawings = {piece};
        sheet.cells = {0};
    }
    int const made = addSymbolInstance(id, layer + 1);
    // Where the art was: on the same peg, moving as the layer did, on the frames that showed that drawing.
    Layer& shows = layers[made];
    shows.parent = source.parent;
    shows.parentBone = source.parentBone;
    shows.pivot = source.pivot;
    shows.keys = source.keys;
    shows.links = source.links;
    for (Keyframe& key : shows.keys)
        key.pose.points.clear();
    for (int f = 0; f < shows.cells.size(); ++f)
        shows.cells[f] = source.drawingAt(f) == drawing ? 0 : Layer::kNoDrawing;
    refreshSymbolInstances(id);
    return made;
}
