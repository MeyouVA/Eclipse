// Eclipse - function columns: their values on a frame - a curve through points, a velocity along points, an expression, a 3D path.
#include "model/scene/FunctionColumn.h"
#include "model/scene/Scene.h"

#include <QCoreApplication>

#include <cmath>

namespace {

// How far along (0..1) on `frame` by a velocity column (percent of the way by frame); without points, steadily over [first, last].
double alongByVelocity(Layer const& velocity, double frame, double first, double last)
{
    if (!velocity.keys.isEmpty()) {
        QVector<int> const through = velocity.keysOf(Driver::X);
        return std::clamp(velocity.curveValue(Driver::X, through, frame) / 100.0, 0.0, 1.0);
    }
    if (last <= first)
        return frame >= last ? 1.0 : 0.0;
    return std::clamp((frame - first) / (last - first), 0.0, 1.0);
}

}  // namespace

bool FunctionColumn::defaultBezier = true;
bool FunctionColumn::defaultBezierVelocity = true;
bool FunctionColumn::defaultConstantZ = false;

QString FunctionColumn::kindName(int kind)
{
    switch (kind) {
    case Bezier: return QCoreApplication::translate("FunctionColumn", "Bezier");
    case Ease: return QCoreApplication::translate("FunctionColumn", "Ease");
    case VeloBased: return QCoreApplication::translate("FunctionColumn", "Velocity-Based");
    case Expression: return QCoreApplication::translate("FunctionColumn", "Expression");
    default: return QCoreApplication::translate("FunctionColumn", "3D Path");
    }
}

double FunctionColumn::valueAt(double frame, Scene const* scene, QString* error) const
{
    if (error)
        error->clear();
    switch (kind) {
    case Bezier:
    case Ease: {
        if (curve.keys.isEmpty())
            return 0.0;
        return curve.curveValue(Driver::X, curve.keysOf(Driver::X), frame);
    }
    case VeloBased: {
        // The values of the points gone through in order, at the speed the velocity gives, from the first point's frame to the last's.
        QVector<Keyframe> const& keys = curve.keys;
        if (keys.isEmpty())
            return 0.0;
        double total = 0.0;
        for (int i = 0; i + 1 < keys.size(); ++i)
            total += std::abs(keys[i + 1].pose.x - keys[i].pose.x);
        double gone = alongByVelocity(velocity, frame, keys.first().frame, keys.last().frame) * total;
        for (int i = 0; i + 1 < keys.size(); ++i) {
            double const step = std::abs(keys[i + 1].pose.x - keys[i].pose.x);
            if (gone <= step && step > 0.0)
                return keys[i].pose.x + (keys[i + 1].pose.x - keys[i].pose.x) * gone / step;
            gone -= step;
        }
        return keys.last().pose.x;
    }
    case Expression: {
        double out = 0.0;
        auto lookup = [scene, this](QString const& name, double at, double& v) {
            if (!scene)
                return false;
            for (FunctionColumn const& c : scene->functions)
                if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
                    QString inner;
                    v = c.valueAt(at + expressionOffset, scene, &inner);
                    return inner.isEmpty();
                }
            return false;
        };
        int const frames = expressionFrames > 0 ? expressionFrames : scene ? scene->frameCount() : 1;
        if (!ExpressionValue::evaluate(expression, frame - expressionOffset, frames, lookup, out, error))
            return 0.0;
        return out;
    }
    default:
        return pathAt(frame, scene ? scene->frameCount() - 1 : 0).x();
    }
}

QVector<QVector3D> FunctionColumn::pathLine(QVector<double>* atPoints) const
{
    QVector<QVector3D> line;
    if (atPoints)
        atPoints->clear();
    int const n = int(path.size());
    if (n == 0)
        return line;
    auto pointOf = [&](int i) {
        PathPoint const& p = path[std::clamp(i, 0, n - 1)];
        return QVector3D(float(p.x), float(p.y), float(constantZ ? path.first().z : p.z));
    };
    if (atPoints)
        atPoints->push_back(0.0);
    line << pointOf(0);
    // The way leaves and arrives at each point as its tension, continuity and bias have it (Kochanek-Bartels).
    for (int i = 0; i + 1 < n; ++i) {
        auto tangent = [&](int k, bool leaving) {
            PathPoint const& p = path[k];
            double const t = std::clamp(p.tension, -1.0, 1.0), c = std::clamp(p.continuity, -1.0, 1.0), b = std::clamp(p.bias, -1.0, 1.0);
            QVector3D const before = pointOf(k) - pointOf(k - 1), after = pointOf(k + 1) - pointOf(k);
            double const wa = leaving ? (1 - t) * (1 + b) * (1 + c) / 2.0 : (1 - t) * (1 + b) * (1 - c) / 2.0;
            double const wb = leaving ? (1 - t) * (1 - b) * (1 - c) / 2.0 : (1 - t) * (1 - b) * (1 + c) / 2.0;
            return before * float(wa) + after * float(wb);
        };
        QVector3D const p0 = pointOf(i), p1 = pointOf(i + 1), m0 = tangent(i, true), m1 = tangent(i + 1, false);
        int const steps = 24;
        for (int s = 1; s <= steps; ++s) {
            float const u = float(s) / steps, u2 = u * u, u3 = u2 * u;
            line << p0 * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + p1 * (-2 * u3 + 3 * u2) + m1 * (u3 - u2);
        }
        if (atPoints)
            atPoints->push_back(double(line.size() - 1));
    }
    return line;
}

QVector3D FunctionColumn::pathAt(double frame, int lastFrame) const
{
    QVector<double> at;
    QVector<QVector3D> const line = pathLine(&at);
    if (line.isEmpty())
        return QVector3D();
    if (line.size() == 1)
        return line.first();
    // How far along the line each sample is.
    QVector<double> length(line.size(), 0.0);
    for (int i = 1; i < line.size(); ++i)
        length[i] = length[i - 1] + double((line[i] - line[i - 1]).length());
    double const total = length.last();
    double distance = 0.0;
    // The points on frames of their own: the way between them is gone steadily (unless a velocity says otherwise).
    QVector<QPair<double, double>> locked;      // (frame, distance)
    for (int i = 0; i < path.size(); ++i)
        if (path[i].timeLocked())
            locked.push_back({double(path[i].frame), length[int(at[i])]});
    if (!velocity.keys.isEmpty() || locked.size() < 2) {
        double const first = locked.isEmpty() ? (velocityRangeSet ? double(velocityStart) : 0.0) : locked.first().first;
        double const last = locked.size() < 2 ? std::max(first + 1.0, double(velocityRangeSet ? velocityEnd : lastFrame)) : locked.last().first;
        distance = alongByVelocity(velocity, frame, first, last) * total;
    } else if (frame <= locked.first().first) {
        distance = locked.first().second;
    } else if (frame >= locked.last().first) {
        distance = locked.last().second;
    } else {
        for (int i = 0; i + 1 < locked.size(); ++i)
            if (frame <= locked[i + 1].first) {
                double const span = locked[i + 1].first - locked[i].first;
                distance = locked[i].second + (locked[i + 1].second - locked[i].second) * (span > 0.0 ? (frame - locked[i].first) / span : 1.0);
                break;
            }
    }
    for (int i = 1; i < line.size(); ++i)
        if (distance <= length[i]) {
            double const piece = length[i] - length[i - 1];
            double const t = piece > 1e-12 ? (distance - length[i - 1]) / piece : 0.0;
            return line[i - 1] + (line[i] - line[i - 1]) * float(t);
        }
    return line.last();
}

double FunctionColumn::valueFor(int value, double frame, Scene const& scene) const
{
    if (kind != Path3D)
        return valueAt(frame, &scene);
    QVector3D const at = pathAt(frame, scene.frameCount() - 1);
    return value == Driver::Y ? at.y() : value == Driver::Z ? at.z() : at.x();
}

int Layer::linkOf(int value) const
{
    for (ValueLink const& link : links)
        if (link.value == value)
            return link.function;
    return 0;
}

int Scene::functionIndex(int id) const
{
    for (int i = 0; i < functions.size(); ++i)
        if (functions[i].id == id)
            return i;
    return -1;
}

FunctionColumn const* Scene::function(int id) const
{
    int const i = functionIndex(id);
    return i < 0 ? nullptr : &functions[i];
}

QString Scene::uniqueFunctionName(QString const& base) const
{
    auto taken = [this](QString const& name) {
        for (FunctionColumn const& c : functions)
            if (c.name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    if (!taken(base))
        return base;
    for (int n = 1;; ++n)
        if (!taken(base + QLatin1Char('_') + QString::number(n)))
            return base + QLatin1Char('_') + QString::number(n);
}
