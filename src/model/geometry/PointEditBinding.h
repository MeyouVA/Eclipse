#pragma once
#include "model/scene/Layer.h"

namespace point_edit {
struct Binding {
    int sheet = -1, fill = -1, line = -1, point = -1;
    double place = 0.0, in = 0.0, out = 0.0;
    bool reverse = false;
};
Stroke const* lineAt(Drawing const& art, Binding const& b);
QVector<Binding> bind(Stroke const& line, int sheet, int fill, Drawing const& reference);
PointMove displacement(Binding const& binding, Drawing const& before, Drawing const& posed);
QPointF edgeDisplacement(QPointF p, int sheet, Drawing const& before, Drawing const& posed, bool nearby = true);
}
