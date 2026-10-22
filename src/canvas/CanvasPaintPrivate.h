// Eclipse - the Camera view's painting: what painting a drawing and painting a layer's effects share.
#pragma once

#include <QPainter>
#include <QTransform>

#include <functional>

#include "model/scene/Scene.h"

namespace canvas_paint {

void paintDrawingOpaque(QPainter& painter, Scene const& scene, Drawing const& drawing, QColor const* tint);

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
