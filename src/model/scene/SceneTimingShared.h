#pragma once
#include "model/scene/Layer.h"

void copyLayerTiming(Layer const& source, Layer& target, Layer const* beforeSource = nullptr, Layer const* beforeTarget = nullptr);
QVector<int> drawingRemap(Layer const& before, Layer const& after);
