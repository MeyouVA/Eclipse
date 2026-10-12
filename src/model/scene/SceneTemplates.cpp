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
    return piece;
}

int Scene::insert(Scene const& piece, int frame)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    if (frame + piece.frameCount() > m_frameCount)
        setFrameCount(frame + piece.frameCount());
    // The colours: those the scene has already, by name and colour; the others are added.
    QHash<int, int> swatchOf;
    for (Swatch const& theirs : piece.palette) {
        int mine = kNoSwatch;
        for (Swatch const& have : palette)
            if (have.name == theirs.name && have.color == theirs.color)
                mine = have.id;
        swatchOf.insert(theirs.id, mine);   // kNoSwatch: added below, if anything uses it
    }
    auto relink = [&](int& swatch, QColor const& colour) {
        if (swatch == kNoSwatch)
            return;
        auto found = swatchOf.find(swatch);
        if (found == swatchOf.end()) {
            swatch = kNoSwatch;     // a colour the piece itself did not know: the artwork keeps its own
            return;
        }
        if (found.value() == kNoSwatch) {
            Swatch const* const theirs = piece.findSwatch(swatch);
            found.value() = addSwatch(theirs ? theirs->name : QStringLiteral("Colour"), theirs ? theirs->color : colour, 0);
        }
        swatch = found.value();
    };

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
    // What was attached to what, among them.
    for (int index : std::as_const(added)) {
        Layer& layer = layers[index];
        layer.parent = idOf.value(layer.parent, 0);
        if (layer.parent == 0)
            layer.parentBone = -1;
    }
    return firstIndex;
}
