// Eclipse - the Camera view's painting: what painting a drawing and painting a layer's effects share.
#pragma once

#include <QPainter>
#include <QTransform>

#include <functional>

#include "model/scene/Scene.h"
#include "colour/images/ColourTransform.h"

namespace canvas_paint {

struct PictureWash { int percent = 0; QColor colour = Qt::white; };
PictureWash& pictureWashState();
void paintDrawingOpaque(QPainter& painter, Scene const& scene, Drawing const& drawing, QColor const* tint);
QImage washedPicture(QImage const& image, int percent, QColor const& colour);
// A picture put through a colour conversion (kept, so each is converted once).
QImage convertedPicture(QImage const& image, Cms::Transform const& transform);

}  // namespace canvas_paint

// What painting a layer has to hand when it does its effects (see applyLayerEffect).
struct LayerEffectSpace {
    QPainter& painter;
    Scene const& scene;
    int l;
    int frame;
    QTransform const& sceneToDevice;
    Layer const& layer;
    QSize size;
    double dpr;
    double pixelsPerUnit;
    Pose pose;
    std::function<void(QPainter&, int, int)> paintMatte;
    bool symbol;
};
