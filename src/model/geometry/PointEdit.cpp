#include "model/geometry/PointEdit.h"
#include "model/geometry/PointEditBinding.h"
#include "model/geometry/Deform.h"
#include <algorithm>

namespace {
using namespace point_edit;
struct LineBinding {
    int sheet = 0, fill = -1, line = 0;
    QVector<Binding> points;
};

QVector<LineBinding> bindingsFor(Drawing const& edited, Drawing const& reference)
{
    QVector<LineBinding> result;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (int l = 0; l < edited.strokesOf(sheet).size(); ++l)
            result.push_back({sheet, -1, l, bind(edited.strokesOf(sheet)[l], sheet, -1, reference)});
        for (int f = 0; f < edited.fillsOf(sheet).size(); ++f) {
            Fill const& fill = edited.fillsOf(sheet)[f];
            int source = -1;
            for (int old = 0; old < reference.fillsOf(sheet).size(); ++old)
                if (fill.sourceIdentity && fill.sourceIdentity == reference.fillsOf(sheet)[old].sourceIdentity) { source = old; break; }
            for (int c = 0; c < fill.contours.size(); ++c)
                result.push_back({sheet, f, c, bind(fill.contours[c], sheet, source, reference)});
        }
    }
    return result;
}

Stroke& target(Drawing& drawing, LineBinding const& b)
{
    return b.fill < 0 ? drawing.strokesOf(b.sheet)[b.line] : drawing.fillsOf(b.sheet)[b.fill].contours[b.line];
}

bool nonzero(PointMove const& m) { return !m.by.isNull() || !m.in.isNull() || !m.out.isNull(); }

void retargetKeys(Layer& layer, int index, Drawing const& before, QVector<LineBinding> const& bindings)
{
    for (Keyframe& key : layer.keys) {
        Drawing const posed = Deform::movedPoints(before, index, key.pose.points);
        QVector<PointMove> moves;
        for (LineBinding const& line : bindings)
            for (int p = 0; p < line.points.size(); ++p) {
                PointMove m = displacement(line.points[p], before, posed);
                if (!nonzero(m)) continue;
                m.drawing = index; m.sheet = line.sheet; m.fill = line.fill; m.line = line.line; m.point = p;
                moves.push_back(m);
            }
        key.pose.points = Deform::withMoves(key.pose.points, index, moves);
    }
}
}

Drawing DrawingPoints::restorePose(Layer& layer, int index, int frame, Drawing const& edited)
{
    Drawing const before = layer.drawings[index];
    Drawing const posed = Deform::movedPoints(before, index, layer.poseAt(frame).points);
    QVector<LineBinding> const bindings = bindingsFor(edited, posed);
    Drawing restored = edited;
    for (LineBinding const& b : bindings) {
        Stroke& line = target(restored, b);
        for (int p = 0; p < b.points.size(); ++p) {
            PointMove const m = displacement(b.points[p], before, posed);
            if (!nonzero(m)) continue;
            if (!line.points[p].handles) line.freezeHandles();
            line.points[p].pos -= m.by;
            line.points[p].in -= m.in;
            line.points[p].out -= m.out;
        }
    }
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        for (Fill& fill : restored.fillsOf(sheet)) {
            if (!fill.contours.isEmpty()) { fill.rebuild(); continue; }
            bool unchanged = false;
            for (int f = 0; f < before.fillsOf(sheet).size(); ++f)
                if (fill.sourceIdentity && fill.sourceIdentity == before.fillsOf(sheet)[f].sourceIdentity && fill.outlines == posed.fillsOf(sheet)[f].outlines) {
                    fill.outlines = before.fillsOf(sheet)[f].outlines;
                    fill.seed = before.fillsOf(sheet)[f].seed;
                    unchanged = true;
                    break;
                }
            if (unchanged) continue;
            for (QPolygonF& outline : fill.outlines)
                for (QPointF& point : outline)
                    point += edgeDisplacement(point, sheet, posed, before);
            fill.seed += edgeDisplacement(fill.seed, sheet, posed, before, false);
        }
    retargetKeys(layer, index, before, bindings);
    return restored;
}

void DrawingPoints::retarget(Layer& layer, int index, Drawing const& before, Drawing const& after)
{
    bool any = false;
    for (Keyframe const& key : layer.keys)
        any = any || Deform::movesPoints(key.pose.points, index);
    if (any)
        retargetKeys(layer, index, before, bindingsFor(after, before));
}
