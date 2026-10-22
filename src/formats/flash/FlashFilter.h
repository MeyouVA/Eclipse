// Eclipse - a filter of Flash's on a placed symbol (a glow, a blur, a drop shadow), as a document (.fla) or a movie (.swf) gives it.
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include "EclipseExport.h"

struct Layer;

namespace Flash {

// Sizes in pixels, as Flash gives them.
struct Filter {
    QString kind;               // "GlowFilter", "BlurFilter", "DropShadowFilter"
    double blurX = 5.0, blurY = 5.0;
    QColor colour = Qt::black;
    double strength = 1.0;      // (1: as strong as it comes)
    double distance = 0.0;      // a shadow: how far it falls, at so many degrees
    double angle = 45.0;
    bool inner = false;
};

// A layer given Flash's blend mode ("multiply" ...) and the filters Eclipse has effects for, their sizes times `scale`.
ECLIPSE_FORMATS_API void applyLook(Layer& layer, QString const& blend, QVector<Filter> const& filters, double scale);

}  // namespace Flash
