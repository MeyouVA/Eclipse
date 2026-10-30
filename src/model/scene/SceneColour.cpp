// Eclipse - the colour management of a scene: what is converted into its working space, and out of it.
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
