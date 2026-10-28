#pragma once

#include <QVector>

class Scene;

struct PlanningPathPoint {
    double x = 0.0, y = 0.0, z = 0.0;
    bool key = false;
    int keyIndex = -1, controlIndex = -1;
    double localDepth = 0.0, depthX = 0.0, depthY = 0.0, depthZ = 0.0;
};

struct PlanningMotionPath {
    QVector<PlanningPathPoint> samples;
    QVector<PlanningPathPoint> controls;
};

PlanningMotionPath planningMotionPath(Scene const& scene, int layer);
