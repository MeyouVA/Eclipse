// Eclipse - resolving a symbol's exposure timing into ordinary layer animation.
#pragma once
#include "model/scene/Scene.h"
namespace SymbolTiming {
Layer expanded(Scene const& source, Layer layer, Layer const& instance, int frames);
}
