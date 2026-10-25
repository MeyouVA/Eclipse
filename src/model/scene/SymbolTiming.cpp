// Eclipse - preserving held, reversed and morphed symbol frames when expanding layers.
#include "model/scene/SymbolTiming.h"

Layer SymbolTiming::expanded(Scene const& source, Layer layer, Layer const& instance, int frames)
{
    int const index = source.layerIndex(layer.id);
    QVector<int> timing(frames, Layer::kNoDrawing);
    QVector<Keyframe> poses;
    QHash<int, int> morphed;
    QVector<int> audio;
    if (layer.kind == Layer::Sound)
        audio.fill(-1, frames);
    for (int frame = 0; frame < frames; ++frame) {
        int const shown = instance.drawingAt(frame);
        if (shown == Layer::kNoDrawing || shown >= source.frameCount())
            continue;
        timing[frame] = layer.drawingAt(shown);
        if (layer.kind == Layer::Sound)
            audio[frame] = std::max(-1, layer.soundFrameAt(shown));
        Keyframe key;
        key.frame = frame;
        key.pose = source.poseOf(index, shown);
        if (layer.morphing(shown) && !layer.showsSymbol()) {
            if (!morphed.contains(shown)) {
                morphed.insert(shown, int(layer.drawings.size()));
                layer.drawings << source.layers[index].shownAt(shown);
            }
            int const drawing = morphed[shown];
            for (PointMove& point : key.pose.points)
                if (point.drawing == timing[frame])
                    point.drawing = drawing;
            timing[frame] = drawing;
        }
        key.tween = false;
        poses << key;
    }
    layer.cells = timing;
    layer.keys = poses;
    layer.links.clear();
    layer.drivers.clear();
    layer.morphs.clear();
    if (layer.kind == Layer::Sound) {
        layer.soundFrames = audio;
        layer.soundStart = 0;
    }
    return layer;
}
