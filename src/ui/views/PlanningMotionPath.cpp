#include "ui/views/PlanningMotionPath.h"
#include "model/scene/Scene.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

PlanningMotionPath planningMotionPath(Scene const& scene, int index)
{
    PlanningMotionPath result;
    if (index < 0 || index >= scene.layers.size() || scene.frameCount() < 1)
        return result;
    int first = scene.frameCount(), last = -1;
    QSet<int> keyFrames;
    auto include = [&](int frame, bool key) {
        if (frame < 0 || frame >= scene.frameCount())
            return;
        first = std::min(first, frame);
        last = std::max(last, frame);
        if (key)
            keyFrames.insert(frame);
    };
    auto allFrames = [&] { include(0, false); include(scene.frameCount() - 1, false); };
    for (int current = index, guard = 0; current >= 0 && guard < scene.layers.size(); current = scene.parentIndex(current), ++guard) {
        Layer const& layer = scene.layers[current];
        for (Keyframe const& key : layer.keys)
            include(key.frame, current == index);
        for (ValueLink const& link : layer.links) {
            if (link.value != Driver::X && link.value != Driver::Y && link.value != Driver::Z)
                continue;
            FunctionColumn const* const column = scene.function(link.function);
            if (!column)
                continue;
            if (column->kind == FunctionColumn::Expression || column->kind == FunctionColumn::Path3D)
                allFrames();
            for (Keyframe const& key : column->curve.keys)
                include(key.frame, current == index);
            for (Keyframe const& key : column->velocity.keys)
                include(key.frame, current == index);
            for (FunctionColumn::PathPoint const& point : column->path)
                if (point.timeLocked())
                    include(point.frame, current == index);
        }
        for (Driver const& driver : layer.drivers)
            if (driver.value == Driver::X || driver.value == Driver::Y || driver.value == Driver::Z)
                allFrames();
    }
    if (last <= first)
        return result;
    Layer const& layer = scene.layers[index];
    auto pointAt = [&](int frame, PoseOverride const* over, bool key) {
        PlanningPathPoint point;
        point.key = key;
        if (layer.kind == Layer::Camera) {
            QPointF const p = scene.flatTransform(index, frame, over).map(QPointF(scene.width / 2.0, scene.height / 2.0));
            point.x = p.x(); point.y = p.y();
            point.z = scene.depthOf(index, frame, over) + scene.focalDistance(frame);
        } else {
            scene.toSpace(index, frame, layer.pivot, point.x, point.y, point.z, over);
        }
        return point;
    };
    int const stride = std::max(1, int((qint64(last) - first + 599) / 600));
    QSet<int> frames;
    for (qint64 frame = first; frame <= last; frame += stride)
        frames.insert(int(frame));
    frames.insert(last);
    if (keyFrames.size() <= 1024)
        frames.unite(keyFrames);
    QList<int> ordered = frames.values();
    std::sort(ordered.begin(), ordered.end());
    for (int frame : std::as_const(ordered)) {
        PlanningPathPoint const point = pointAt(frame, nullptr, keyFrames.contains(frame));
        if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
            result.samples.push_back(point);
    }
    for (int k = 0; k + 1 < layer.keys.size(); ++k) {
        Keyframe const& firstKey = layer.keys[k];
        Keyframe const& nextKey = layer.keys[k + 1];
        QVector<double> positions;
        firstKey.pathLine(QPointF(firstKey.pose.x, firstKey.pose.y), QPointF(nextKey.pose.x, nextKey.pose.y), &positions);
        QVector<int> const controlFrames = scene.controlPointFrames(index, k);
        for (int c = 0; c < firstKey.controls.size(); ++c) {
            int const frame = controlFrames.value(c, -1);
            if (frame < 0 || frame >= scene.frameCount())
                continue;
            PoseOverride over;
            over.layer = index; over.frame = frame; over.pose = scene.poseOf(index, frame);
            over.pose.x = layer.keys[k].controls[c].x();
            over.pose.y = layer.keys[k].controls[c].y();
            over.pose.z = c < firstKey.controlsZ.size() ? firstKey.controlsZ[c]
                                                       : firstKey.pose.z + (nextKey.pose.z - firstKey.pose.z) * positions.value(c, 0.0);
            PlanningPathPoint point = pointAt(frame, &over, false);
            point.keyIndex = k; point.controlIndex = c; point.localDepth = over.pose.z;
            over.pose.z += 1.0;
            PlanningPathPoint const shifted = pointAt(frame, &over, false);
            point.depthX = shifted.x - point.x; point.depthY = shifted.y - point.y; point.depthZ = shifted.z - point.z;
            over.pose.z -= 1.0; over.pose.x += 1.0;
            PlanningPathPoint const shiftedX = pointAt(frame, &over, false);
            point.xX = shiftedX.x - point.x; point.xY = shiftedX.y - point.y; point.xZ = shiftedX.z - point.z;
            over.pose.x -= 1.0; over.pose.y += 1.0;
            PlanningPathPoint const shiftedY = pointAt(frame, &over, false);
            point.yX = shiftedY.x - point.x; point.yY = shiftedY.y - point.y; point.yZ = shiftedY.z - point.z;
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && std::isfinite(point.depthX) && std::isfinite(point.depthY) && std::isfinite(point.depthZ)
                && std::isfinite(point.xX) && std::isfinite(point.xY) && std::isfinite(point.xZ)
                && std::isfinite(point.yX) && std::isfinite(point.yY) && std::isfinite(point.yZ))
                result.controls.push_back(point);
        }
    }
    return result;
}
