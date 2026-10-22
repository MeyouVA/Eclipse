// Eclipse - a pose and the keyframes that hold one, and how a tween eases between them.
#pragma once

#include "model/scene/Drawing.h"

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
    double tension = 0.0;
    double continuity = 0.0;
    double bias = 0.0;
    QVector<QPointF> pathLine(QPointF from, QPointF to, QVector<double>* controlsAt = nullptr) const;
    // The place `along` (0..1) of the way.
    QPointF pathPoint(QPointF from, QPointF to, double along) const;
};

double easedProgress(double t, double easeOut, double easeIn, double liftOut = 0.0, double liftIn = 0.0);
