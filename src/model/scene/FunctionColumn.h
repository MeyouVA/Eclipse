// Eclipse - function columns: named curves kept by the scene that the values of layers can be linked to.
#pragma once

#include "EclipseExport.h"
#include "model/scene/Layer.h"

#include <QVector3D>

#include <functional>

class Scene;

struct FunctionColumn {
    enum Kind { Bezier, Ease, VeloBased, Expression, Path3D, KindCount };
    int id = 0;             // unique in the scene and never reused
    QString name;
    int kind = Bezier;
    // Bezier, ease, velocity-based: the points, as keyframes that set pos x (shaped like a layer value's curve).
    Layer curve;
    QString expression;
    // Imported expressions retain their original frame origin and scene length.
    int expressionOffset = 0;
    int expressionFrames = 0;
    bool extendedTiming = false;
    // Implicit path velocity keeps its full source range across crops and insertions.
    bool velocityRangeSet = false;
    int velocityStart = 0, velocityEnd = 0;
    // Path points may be locked before the scene starts; an unlocked point follows the velocity.
    struct PathPoint {
        double x = 0.0, y = 0.0, z = 0.0;
        int frame = -1;
        double tension = 0.0, continuity = 0.0, bias = 0.0;
        bool locked = false;
        bool timeLocked() const { return locked || frame >= 0; }
        bool operator==(PathPoint const&) const = default;
    };
    QVector<PathPoint> path;
    bool constantZ = false;
    // Velocity-based and 3D paths: how far along the way (0..100 %) on each frame; without points, steadily.
    Layer velocity;
    // Shown in the Xsheet.
    bool shown = true;

    // The value on a frame (a 3D path: across); the place on a 3D path.  `scene` for expressions naming other columns.
    double valueAt(double frame, Scene const* scene = nullptr, QString* error = nullptr) const;
    QVector3D pathAt(double frame, int lastFrame) const;
    // The value a linked value (Driver::Value) of a layer takes: a 3D path gives its across, down or depth.
    double valueFor(int value, double frame, Scene const& scene) const;
    // Points to edit in the Function view (bezier and ease columns).
    bool hasPoints() const { return kind == Bezier || kind == Ease || kind == VeloBased; }
    static QString kindName(int kind);
    // Preferences: new columns are beziers (or eases); new velocity points of a path are bezier (or eased); new paths keep one depth.
    ECLIPSE_MODEL_API static bool defaultBezier, defaultBezierVelocity, defaultConstantZ;
    // The line a 3D path takes through its points, and how far along it each of them is.
    QVector<QVector3D> pathLine(QVector<double>* atPoints = nullptr) const;
};

// Expressions: arithmetic, comparisons, functions (sin, cos, ... in degrees; min, max, if ...), the frame, other columns.
namespace ExpressionValue {
// The value of `text` on `frame`; false, with the reason, when it cannot be worked out.  `lookup` gives a column's value.
ECLIPSE_MODEL_API bool evaluate(QString const& text, double frame, int frameCount, std::function<bool(QString const&, double, double&)> const& lookup,
                                double& out, QString* error);
}
