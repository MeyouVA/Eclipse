// Eclipse - the Camera view's painting: what its files share.
#pragma once

#include "ui/canvas/Canvas.h"
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
#include "ui/canvas/PencilTexture.h"
#include "render/effects/Effects.h"

namespace canvas_paint_shared {

inline std::atomic<bool> gBlurs{true};
inline std::atomic<bool> gFocus{true};

}  // namespace canvas_paint_shared
