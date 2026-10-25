// Eclipse - independent function columns for imported scene pieces.
#pragma once
#include "model/scene/Scene.h"
namespace TemplateFunctions {
QHash<int, int> import(Scene& target, Scene const& source, int offset);
void relink(Layer& layer, QHash<int, int> const& functions);
}
