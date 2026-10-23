// Eclipse - a pose and the keyframes that hold one, and how a tween eases between them.
#pragma once

#include "EclipseExport.h"
#include "model/scene/Drawing.h"

// One point of a drawing moved on a keyframe (point animation): how far, and how far its two handles turn.
struct PointMove {
    int drawing = 0;
    int sheet = 2;
    int fill = -1;      // the painted area whose contour holds the point; -1: a line
    int line = 0;       // the line, or the contour of that area
    int point = 0;
    QPointF by, in, out;
    bool operator==(PointMove const&) const = default;
    bool samePoint(PointMove const& o) const { return drawing == o.drawing && sheet == o.sheet && fill == o.fill && line == o.line && point == o.point; }
    bool comesBefore(PointMove const& o) const;
};

struct Pose {
    double x = 0.0;
    double y = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    // Leaning: its uprights tipped over by so many degrees (positive: the top goes to the right).
    double skew = 0.0;
    double z = 0.0;
    double rotX = 0.0;
    double rotY = 0.0;
    double fov = 0.0;
    QVector<QPointF> quad;
    QVector<double> bend;
    QVector<QPointF> warp;
    // How opaque the layer is (0..1): keyframed like the rest, so that it can fade.
    double opacity = 1.0;
    QVector<double> fx;
    // Point animation: the points of the drawings moved (in their order; see PointMove::comesBefore).
    QVector<PointMove> points;

    bool operator==(Pose const&) const = default;
    static Pose lerp(Pose const& a, Pose const& b, double t);
};

struct Keyframe {
    int frame = 0;
    Pose pose;
    enum Part { PartX = 1, PartY = 2, PartScaleX = 4, PartScaleY = 8, PartAngle = 16, PartOpacity = 32, PartZ = 64, PartSkew = 128,
                PartFov = 256, PartRest = 512, AllParts = 1023 };
    int parts = AllParts;
    bool sets(int part) const { return (parts & part) != 0; }
    bool tween = true;
    double easeIn = 0.0;
    double easeOut = 0.0;
    double liftIn = 0.0;
    double liftOut = 0.0;
    QVector<QPointF> controls;
    QVector<double> controlsZ;
    // The depth of the way `along` (0..1), through the control points' depths at the places `controlsAt` gives them.
    double pathDepth(double fromZ, double toZ, double along, QVector<double> const& controlsAt) const;
    double tension = defaultTension;
    double continuity = defaultContinuity;
    double bias = defaultBias;
    // What new keyframes and control points start with (Preferences, -1..1).
    ECLIPSE_MODEL_API static double defaultTension, defaultContinuity, defaultBias;
    QVector<QPointF> pathLine(QPointF from, QPointF to, QVector<double>* controlsAt = nullptr) const;
    // The place `along` (0..1) of the way.
    QPointF pathPoint(QPointF from, QPointF to, double along) const;
};

double easedProgress(double t, double easeOut, double easeIn, double liftOut = 0.0, double liftIn = 0.0);
