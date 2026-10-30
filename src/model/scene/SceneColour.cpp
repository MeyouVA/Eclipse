// Eclipse - the colour management of a scene: what is converted into its working space, and out of it.
#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"

#include "colour/images/ColourTransform.h"

Cms::Transform Scene::swatchTransform() const
{
    if (!colourManaged || !palettesInSrgb)
        return {};
    return Cms::between(QStringLiteral("sRGB"), workingSpace);
}

Cms::Transform Scene::textureTransform() const
{
    if (!colourManaged)
        return {};
    return Cms::between(textureSpace.isEmpty() ? QStringLiteral("sRGB") : textureSpace, workingSpace);
}

QColor Scene::managedColour(QColor const& colour) const
{
    return colourManaged && palettesInSrgb ? swatchTransform().apply(colour) : colour;
}

Cms::Transform Scene::pictureTransform(Layer const& layer, bool painted) const
{
    if (!colourManaged)
        return {};
    // (Bitmaps painted in Eclipse are of palette colours; imported pictures are in the layer's colour space.)
    if (painted)
        return swatchTransform();
    if (layer.colourSpace.isEmpty())
        return {};
    return Cms::between(layer.colourSpace, workingSpace);
}

Cms::Transform Scene::pictureTransform(Layer const& layer, Picture const& picture) const
{
    if (!colourManaged || picture.painted || picture.colourSpace.isEmpty())
        return pictureTransform(layer, picture.painted);
    return Cms::between(picture.colourSpace, workingSpace);
}

QString Scene::outputSpace(QString const& writeSpace, QString const& format) const
{
    if (!colourManaged)
        return {};
    if (!writeSpace.isEmpty())
        return writeSpace;
    if (format.compare(QLatin1String("exr"), Qt::CaseInsensitive) == 0) {
        Cms::Space const* const linear = Cms::config()->space(QStringLiteral("scene_linear"));
        if (linear)
            return linear->name;
    }
    return workingSpace;
}

Cms::Transform Scene::outputTransform(QString const& space, QString* error) const
{
    if (!colourManaged || space.isEmpty())
        return {};
    return Cms::between(workingSpace, space, error);
}

void Scene::timedFrames(int l, int frame, int* drawingFrame, int* transformFrame) const
{
    int drawn = frame, moved = frame;
    if (l >= 0 && l < layers.size()) {
        Layer const& layer = layers[l];
        int const last = std::max(0, frameCount() - 1);
        for (int i = 0; i < layer.effects.size(); ++i) {
            Effect const& e = layer.effects[i];
            if (!e.enabled)
                continue;
            bool const both = e.type == Effect::HoldTiming || e.type == Effect::Retime || e.type == Effect::Loop;
            auto timed = [&](int at) {
                if (e.type == Effect::HoldTiming || e.type == Effect::HoldTransformationTiming) {
                    int const step = std::max(1, int(std::lround(e.value("hold"))));
                    int const from = int(std::lround(e.value("start"))) - 1;
                    int const held = at < from ? at : from + (at - from) / step * step;
                    return std::clamp(held, 0, last);
                }
                if (e.type == Effect::Loop || e.type == Effect::LoopTransformation) {
                    // Frames Loop Start..Loop End over and over (only in the output range, when it is used).
                    int const from = int(std::lround(e.value("start"))) - 1, to = std::max(from, int(std::lround(e.value("end"))) - 1);
                    int const length = to - from + 1;
                    bool const ranged = e.value("useOutput") > 0.5;
                    int const outFrom = ranged ? int(std::lround(e.value("outStart"))) - 1 : 0;
                    int const outTo = ranged ? int(std::lround(e.value("outEnd"))) - 1 : last;
                    auto looped = [&](int f) { return from + ((f - outFrom) % length + length) % length; };
                    if (at < outFrom)
                        return at;
                    if (at <= outTo)
                        return std::clamp(looped(at), 0, last);
                    return std::clamp(e.value("resume") > 0.5 ? at : looped(outTo) + (at - outTo), 0, last);
                }
                if (e.value("useTarget") > 0.5)
                    return std::clamp(int(std::lround(layer.effectAmount(i, layer.poseAt(frame)))) - 1, 0, last);
                return std::clamp(at + int(std::lround(e.value("offset"))), 0, last);
            };
            if (e.type == Effect::HoldTiming || e.type == Effect::HoldTransformationTiming || e.type == Effect::Retime || e.type == Effect::RetimeTransformation
                || e.type == Effect::Loop || e.type == Effect::LoopTransformation) {
                moved = timed(moved);
                if (both)
                    drawn = timed(drawn);
            }
        }
    }
    if (drawingFrame)
        *drawingFrame = drawn;
    if (transformFrame)
        *transformFrame = moved;
}
