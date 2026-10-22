// Eclipse - the Timeline painted: what its files share.
#pragma once

#include "ui/timeline/Timeline.h"
#include "ui/timeline/TimelinePaintSpace.h"
#include "actions/theme/InterfaceColours.h"

#include <QPainter>

#include <functional>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include <QScrollBar>

namespace timeline_paint_headers_shared {

// A colour mixed part of the way towards another.
inline QColor mixed(QColor const& a, QColor const& b, double t)
{
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

}  // namespace timeline_paint_headers_shared
