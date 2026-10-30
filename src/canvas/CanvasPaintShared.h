// Eclipse - the Camera view's painting: what its files share.
#pragma once

#include "canvas/Canvas.h"
#include "model/scene/StrokePrivate.h"

#include <atomic>
#include <chrono>
#include <QPainter>
#include <QPainterPath>

#include <QHash>
#include <QSet>

#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "canvas/PencilTexture.h"
#include "render/effects/Effects.h"

namespace canvas_paint_shared {

inline std::atomic<bool> gBlurs{true};
// Painting for the Camera view (not a render): a Visibility node then goes by its Camera view switch.
inline thread_local bool gViewing = false;
inline std::atomic<bool> gFocus{true};
inline std::atomic<bool> gToneHighlight{true};
inline std::atomic<bool> gColourOverride{true};
// Painting a layer for an Animate Pencil Texture node: its pencil textures moved along the lines by so many units, and each line by up to `gTextureSpread` more.
inline thread_local double gTextureShift = 0.0;
inline thread_local double gTextureSpread = 0.0;
inline thread_local quint32 gTextureSeed = 0;
// How strongly lines are painted just now (Preferences: Pencil Lines Opacity, while the view is painted).
inline thread_local double gLineOpacity = 1.0;
// Paint drawn with its fine edge (off while something moves in the view).
inline thread_local bool gFineFills = true;
// Preferences > Render: what is left out (Canvas::RenderSwitch bits); the view-only ones only while the Camera view paints.
inline std::atomic<unsigned> gRenderOff{0};
// Painting a layer whose pictures are scaled to the nearest pixel (Layer Properties: Texture Filter).
inline thread_local bool gNearestPictures = false;
// The layer being painted: its pictures are read in its colour space (Layer Properties: Colour Space).
inline thread_local Layer const* gPictureLayer = nullptr;

// The Camera view's time limit for effects (Preferences > Render): past it, effects are left out of the picture being drawn.
inline thread_local bool gEffectsTimed = false;
inline thread_local bool gEffectsTimedOut = false;
inline thread_local std::chrono::steady_clock::time_point gEffectsDeadline{};
inline bool effectsTimedOut()
{
    if (gEffectsTimed && !gEffectsTimedOut && std::chrono::steady_clock::now() > gEffectsDeadline)
        gEffectsTimedOut = true;
    return gEffectsTimedOut;
}
inline bool leftOut(unsigned which)
{
    unsigned const off = gRenderOff;
    return (off & which) != 0 && ((which & Canvas::ViewOnlySwitches) == 0 || gViewing);
}

}  // namespace canvas_paint_shared
