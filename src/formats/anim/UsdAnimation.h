// Eclipse - layers' keyframes read from and written to USD files (.usda, .usdc, .usdz): each layer a prim of that name with its
// transform as xformOps - translate (X, Y up the picture, Z), rotateXYZ (the Z angle the other way round) and scale - sampled
// at the layer's keyframes. Positions in fields or scene units, as in AtomAnimation.
//
// .usda is read and written here; .usdc and .usdz go through Python with the USD package (pxr): the Python and the package that
// come with Eclipse (thirdparty/python, thirdparty/usd), or else a Python of the computer's own that has it. With it, a .usda's
// references are also put together before it is read.
#pragma once

#include "formats/anim/AtomAnimation.h"

namespace UsdAnimation {

bool isUsd(QString const& path);
AtomAnimation::Result write(Scene const& scene, QString const& path, AtomAnimation::Options const& options);
AtomAnimation::Result read(Scene& scene, QString const& path, AtomAnimation::Options const& options);
// The same from and to the text of a .usda file.
AtomAnimation::Result readText(Scene& scene, QString const& text, AtomAnimation::Options const& options);
QString writeText(Scene const& scene, AtomAnimation::Options const& options, AtomAnimation::Result* result);
// Whether the computer's Python has the USD package (for .usdc and .usdz).
bool hasUsdPython();

}  // namespace UsdAnimation
