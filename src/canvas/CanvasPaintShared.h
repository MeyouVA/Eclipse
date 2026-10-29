// Eclipse - the Camera view's painting: what its files share.
#pragma once

#include "canvas/Canvas.h"
#include "model/scene/StrokePrivate.h"

#include <atomic>
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

}  // namespace canvas_paint_shared
