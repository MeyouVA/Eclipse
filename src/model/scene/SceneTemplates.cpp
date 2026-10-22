// Eclipse - the scene: templates - a piece of a scene taken out, and a piece brought in.
#include "model/scene/Scene.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <cstdio>

Scene Scene::extract(QVector<int> const& chosen, int first, int last) const
{
    first = std::clamp(first, 0, m_frameCount - 1);
    last = std::clamp(last, first, m_frameCount - 1);
    int const frames = last - first + 1;
    // The layers asked for, and whatever hangs under any of them, in the order of the scene.
    QVector<bool> in(layers.size(), false);
    for (int l : chosen)
        if (l >= 0 && l < layers.size())
            in[l] = true;
    for (int l = 0; l < layers.size(); ++l)
        for (int up = parentIndex(l), guard = 0; up >= 0 && guard < 256 && !in[l]; up = parentIndex(up), ++guard)
            in[l] = in[up];

    Scene piece;
    piece.layers.clear();
    piece.width = width;
    piece.height = height;
    piece.fps = fps;
    piece.background = background;
    piece.palette = palette;
    piece.paletteNames = paletteNames;
    piece.m_nextSwatchId = m_nextSwatchId;
    piece.m_nextLayerId = m_nextLayerId;
    piece.m_frameCount = frames;
    for (int l = 0; l < layers.size(); ++l) {
        if (!in[l])
            continue;
        Layer layer = layers[l];
        layer.cells = layer.cells.mid(first, frames);
        layer.cells.resize(frames, Layer::kNoDrawing);
        for (int& mark : layer.keyExposures)
            mark -= first;
        layer.tidyKeyExposures();
        layer.compact();
        // Its keyframes in those frames; and, when it was posed before them, that pose to start from.
        Pose const starting = layers[l].poseAt(first);
        bool const posed = !layers[l].keys.isEmpty();
        layer.keys.clear();
        for (Keyframe key : layers[l].keys) {
            if (key.frame < first || key.frame > last)
                continue;
            key.frame -= first;
            layer.keys.push_back(key);
        }
        layer.morphs.clear();
        for (Morph morph : layers[l].morphs) {
            if (morph.from < first || morph.from > last)
                continue;
            morph.from -= first;
            layer.morphs.push_back(morph);
        }
        if (posed && (layer.keys.isEmpty() || layer.keys.first().frame > 0)) {
            Keyframe start;
            start.frame = 0;
            start.pose = starting;
            // Going on to the next keyframe as the one before these frames did.
            int const before = layers[l].previousKeyFrame(first + 1);
            start.tween = before >= 0 ? layers[l].keys[layers[l].keyIndex(before)].tween : false;
            layer.keys.push_front(start);
        }
        layer.soundStart = std::max(0, layer.soundStart - first);
        // Attached to a layer that is not coming along: attached to nothing.
        int const up = parentIndex(l);
        if (up < 0 || !in[up]) {
            layer.parent = 0;
            layer.parentBone = -1;
        }
        layer.solo = false;
        piece.layers.push_back(layer);
    }
    // The symbols those layers show come along.
    piece.symbols = symbolsUsed(piece.layers);
    piece.refreshSymbolInstances();
    return piece;
}

int Scene::pasteOnto(Scene const& piece, int frame, bool drawings, bool keys)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    // Which layer each of them goes to.
    QVector<int> target;
    for (Layer const& theirs : piece.layers) {
        if (theirs.showsSymbol())
            return -1;          // (a layer that shows a symbol comes in as a layer of its own)
        int found = -1;
        for (int l = 0; l < layers.size() && found < 0; ++l)
            if (layers[l].name == theirs.name && layers[l].kind == theirs.kind && !target.contains(l))
                found = l;
        if (found < 0)
            return -1;
        target.push_back(found);
    }
    if (frame + piece.frameCount() > m_frameCount)
        setFrameCount(frame + piece.frameCount());
    // The colours, as when a piece is brought in as layers of its own.
    QHash<int, int> swatchOf;
    auto relink = [&](int& swatch, QColor const& colour) { swatch = takeColour(piece, swatch, colour, swatchOf); };
    int const count = piece.frameCount();
    for (int i = 0; i < piece.layers.size(); ++i) {
        Layer const& theirs = piece.layers[i];
        Layer& mine = layers[target[i]];
        if (drawings && mine.holdsDrawings()) {
            // Each drawing of theirs that is shown comes over once; the frames show what theirs showed.
            QHash<int, int> brought;
            for (int f = 0; f < count && f < theirs.cells.size() && frame + f < mine.cells.size(); ++f) {
                int const index = theirs.cells[f];
                if (index == Layer::kNoDrawing || index < 0 || index >= theirs.drawings.size()) {
                    mine.cells[frame + f] = Layer::kNoDrawing;
                    continue;
                }
                auto have = brought.find(index);
                if (have == brought.end()) {
                    Drawing d = theirs.drawings[index];
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        for (Stroke& stroke : d.strokesOf(sheet))
                            relink(stroke.swatch, stroke.color);
                        for (Fill& fill : d.fillsOf(sheet))
                            relink(fill.swatch, fill.color);
                    }
                    d.name.clear();     // (a name belongs to one drawing of a layer)
                    mine.drawings.push_back(d);
                    have = brought.insert(index, int(mine.drawings.size()) - 1);
                }
                mine.cells[frame + f] = have.value();
            }
            mine.compact();
        }
        // The keyframes over those frames are theirs.
        if (!keys)
            continue;
        for (int k = int(mine.keys.size()) - 1; k >= 0; --k)
            if (mine.keys[k].frame >= frame && mine.keys[k].frame < frame + count)
                mine.keys.removeAt(k);
        for (Keyframe const& key : theirs.keys)
            if (key.frame >= 0 && key.frame < count)
                mine.setKey(frame + key.frame, key.pose);
    }
    return *std::min_element(target.begin(), target.end());
}

Scene Scene::pasteReady(Scene piece, bool drawings, bool keys, int room)
{
    if (room > 0 && piece.frameCount() > room)
        piece.setFrameCount(room);
    for (Layer& l : piece.layers) {
        if (!keys && !l.keys.isEmpty()) {
            Pose const first = l.poseAt(0);
            l.keys.clear();
            l.setKey(0, first);
        }
        if (!drawings && l.holdsDrawings() && !l.showsSymbol())
            l.cells.fill(Layer::kNoDrawing);
    }
    return piece;
}

int Scene::insert(Scene const& piece, int frame)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    if (frame + piece.frameCount() > m_frameCount)
        setFrameCount(frame + piece.frameCount());
    // The colours: as Preferences say (by default those the scene has already, by name and colour; the others added).
    QHash<int, int> swatchOf;
    auto relink = [&](int& swatch, QColor const& colour) { swatch = takeColour(piece, swatch, colour, swatchOf); };

    // The symbols of the piece become symbols of the scene, under numbers (and, where need be, names) of their own.
    QHash<int, int> symbolOf;
    {
        int next = 1;
        for (Symbol const& have : std::as_const(symbols))
            next = std::max(next, have.id + 1);
        QVector<Symbol> brought;
        for (Symbol theirs : piece.symbols) {
            symbolOf.insert(theirs.id, next);
            theirs.id = next++;
            QString const base = theirs.name;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (Symbol const& have : std::as_const(symbols))
                    taken = taken || have.name == theirs.name;
                for (Symbol const& have : std::as_const(brought))
                    taken = taken || have.name == theirs.name;
                if (!taken)
                    break;
                theirs.name = QStringLiteral("%1 %2").arg(base).arg(n);
            }
            for (Layer& inner : theirs.layers) {
                for (Drawing& d : inner.drawings) {
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        for (Stroke& stroke : d.strokesOf(sheet))
                            relink(stroke.swatch, stroke.color);
                        for (Fill& fill : d.fillsOf(sheet))
                            relink(fill.swatch, fill.color);
                    }
                }
            }
            brought.push_back(theirs);
        }
        for (Symbol& one : brought)
            for (Layer& inner : one.layers)
                if (inner.showsSymbol())
                    inner.symbolId = symbolOf.value(inner.symbolId, 0);
        symbols += brought;
    }
    bool const haveCamera = std::any_of(layers.begin(), layers.end(), [](Layer const& l) { return l.kind == Layer::Camera; });
    QHash<int, int> idOf;       // of the layers brought in: the id they had -> the id they get
    int firstIndex = -1;
    QVector<int> added;
    for (Layer const& theirs : piece.layers) {
        if (theirs.kind == Layer::Camera && haveCamera)
            continue;           // a scene is seen through one camera
        Layer layer = theirs;
        layer.id = m_nextLayerId++;
        idOf.insert(theirs.id, layer.id);
        layer.name = uniqueLayerName(theirs.name);
        layer.nodePlaced = false;
        if (layer.showsSymbol())
            layer.symbolId = symbolOf.value(layer.symbolId, 0);
        // Its frames, from `frame` on.
        layer.cells = QVector<int>(m_frameCount, Layer::kNoDrawing);
        for (int f = 0; f < theirs.cells.size() && frame + f < m_frameCount; ++f)
            layer.cells[frame + f] = theirs.cells[f];
        for (Keyframe& key : layer.keys)
            key.frame += frame;
        for (Morph& morph : layer.morphs)
            morph.from += frame;
        layer.soundStart += frame;
        for (Drawing& d : layer.drawings) {
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Stroke& stroke : d.strokesOf(sheet))
                    relink(stroke.swatch, stroke.color);
                for (Fill& fill : d.fillsOf(sheet))
                    relink(fill.swatch, fill.color);
            }
        }
        layers.push_back(layer);
        added.push_back(int(layers.size()) - 1);
        if (firstIndex < 0)
            firstIndex = int(layers.size()) - 1;
    }
    // What was attached to what, among them; and the mattes their effects take from among them.
    for (int index : std::as_const(added)) {
        Layer& layer = layers[index];
        layer.parent = idOf.value(layer.parent, 0);
        if (layer.parent == 0)
            layer.parentBone = -1;
        for (Effect& effect : layer.effects)
            if (idOf.contains(effect.matte))
                effect.matte = idOf.value(effect.matte);
    }
    if (!symbolOf.isEmpty())
        refreshSymbolInstances();
    return firstIndex;
}
