#pragma once
#include "model/scene/Drawing.h"
#include "model/scene/Swatch.h"

namespace SvgStrokes {
struct Result {
    Drawing drawing;
    QVector<Swatch> gradients;
    QSize size;
};
bool read(QString const& path, Result& result, QString* error = nullptr);
}
