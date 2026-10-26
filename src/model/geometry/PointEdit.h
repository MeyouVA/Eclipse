#pragma once
#include "model/scene/Layer.h"

namespace DrawingPoints {
Drawing restorePose(Layer& layer, int index, int frame, Drawing const& edited);
void retarget(Layer& layer, int index, Drawing const& before, Drawing const& after);
}
