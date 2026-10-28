#pragma once

#include <QVector>

class Scene;

struct PlanningPathPoint {
    double x = 0.0, y = 0.0, z = 0.0;
    bool key = false;
};

struct PlanningMotionPath {
    QVector<PlanningPathPoint> samples;
    QVector<PlanningPathPoint> controls;
};

PlanningMotionPath planningMotionPath(Scene const& scene, int layer);
