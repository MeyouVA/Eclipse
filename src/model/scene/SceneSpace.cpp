// Eclipse - the scene: cameras, depth and turning in space - where each layer is seen.
#include "model/scene/Scene.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

int Scene::cameraIndex() const
{
    int const chosen = layerIndex(activeCamera);
    if (chosen >= 0 && layers[chosen].kind == Layer::Camera && layers[chosen].cameraAvailable)
        return chosen;
    for (int i = 0; i < layers.size(); ++i)
        if (layers[i].kind == Layer::Camera && layers[i].cameraAvailable)
            return i;
    return -1;
}

QVector<int> Scene::cameras() const
{
    QVector<int> found;
    for (int i = 0; i < layers.size(); ++i)
        if (layers[i].kind == Layer::Camera)
            found.push_back(i);
    return found;
}

namespace {

// A matrix of space, for points written as rows: p' = p * M, as QTransform has it.
struct Mat4 {
    double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    Mat4 operator*(Mat4 const& o) const
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = 0.0;
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
            }
        return r;
    }
    // A matrix of the flat sheet, which leaves depth alone.
    static Mat4 flat(QTransform const& t)
    {
        Mat4 r;
        r.m[0][0] = t.m11(); r.m[0][1] = t.m12(); r.m[0][3] = t.m13();
        r.m[1][0] = t.m21(); r.m[1][1] = t.m22(); r.m[1][3] = t.m23();
        r.m[3][0] = t.m31(); r.m[3][1] = t.m32(); r.m[3][3] = t.m33();
        return r;
    }
    static Mat4 move(double x, double y, double z)
    {
        Mat4 r;
        r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
        return r;
    }
    // About the line across: positive, what is above (smaller y) goes away from the camera.
    static Mat4 aboutX(double degrees)
    {
        double const a = degrees * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
        Mat4 r;
        r.m[1][1] = c; r.m[1][2] = s;
        r.m[2][1] = -s; r.m[2][2] = c;
        return r;
    }
    // About the line up: positive, what is to the right goes away from the camera.
    static Mat4 aboutY(double degrees)
    {
        double const a = degrees * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
        Mat4 r;
        r.m[0][0] = c; r.m[0][2] = -s;
        r.m[2][0] = s; r.m[2][2] = c;
        return r;
    }
    void map(QPointF p, double& x, double& y, double& z) const
    {
        double const w = p.x() * m[0][3] + p.y() * m[1][3] + m[3][3];
        double const d = std::abs(w) > 1e-12 ? w : 1e-12;
        x = (p.x() * m[0][0] + p.y() * m[1][0] + m[3][0]) / d;
        y = (p.x() * m[0][1] + p.y() * m[1][1] + m[3][1]) / d;
        z = (p.x() * m[0][2] + p.y() * m[1][2] + m[3][2]) / d;
    }
};

Pose poseFor(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    Pose const* const other = over ? over->otherPose(layer, frame) : nullptr;
    Pose pose = over && over->layer == layer && over->frame == frame && !over->hasMatrix ? over->pose : other ? *other
                                                                                                          : scene.layers[layer].poseAt(scene.layers[layer].loopedFrame(frame));
    Layer const& l = scene.layers[layer];
    if (!l.enable3d) {
        pose.rotX = 0.0;
        pose.rotY = 0.0;
    }
    // The values linked to function columns: as the columns have them.
    for (ValueLink const& link : (other || (over && over->layer == layer && over->frame == frame && !over->hasMatrix)) ? QVector<ValueLink>() : l.links)
        if (FunctionColumn const* column = scene.function(link.function)) {
            double v = column->valueFor(link.value, frame, scene);
            if (link.value == Driver::Opacity)
                v = std::clamp(v, 0.0, 1.0);
            Driver::write(pose, link.value, v);
        }
    if (l.drivers.isEmpty())
        return pose;
    // The values that follow other layers.  (A loop of layers following one another goes no deeper than a few rounds.)
    static thread_local int depth = 0;
    if (depth >= 8)
        return pose;
    ++depth;
    for (Driver const& d : l.drivers) {
        int const from = scene.layerIndex(d.source);
        if (from < 0 || from == layer)
            continue;
        Driver::write(pose, d.value, Driver::read(poseFor(scene, from, frame, over), d.sourceValue) * d.factor + d.offset);
    }
    --depth;
    return pose;
}

// From a layer to what it hangs under, on the flat sheet.
QTransform localFor(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    if (over && over->layer == layer && over->frame == frame && over->hasMatrix)
        return over->matrix;
    return scene.layers[layer].matrixFor(poseFor(scene, layer, frame, over), scene.pivotOf(layer, frame));
}

// A parent's matrix without its scaling: turned and moved only.
QTransform withoutScale(QTransform const& m)
{
    double const sx = std::hypot(m.m11(), m.m12()), sy = std::hypot(m.m21(), m.m22());
    if (sx < 1e-12 || sy < 1e-12)
        return QTransform::fromTranslate(m.dx(), m.dy());
    return QTransform(m.m11() / sx, m.m12() / sx, m.m21() / sy, m.m22() / sy, m.dx(), m.dy());
}

QTransform flatChain(Scene const& scene, int layer, int frame, PoseOverride const* over);

// ---- constraints (Layer::constraint): what a layer hangs from, worked out another way ----

// A transformation as its parts: moved, turned (degrees), scaled across and down, leaning (degrees); and back.
struct Parts {
    double x = 0.0, y = 0.0, angle = 0.0, sx = 1.0, sy = 1.0, skew = 0.0;
};
double const kPi = 3.14159265358979323846;

Parts partsOf(QTransform const& m)
{
    Parts p;
    p.x = m.dx();
    p.y = m.dy();
    double const a = std::atan2(m.m12(), m.m11());
    p.angle = a * 180.0 / kPi;
    p.sx = std::hypot(m.m11(), m.m12());
    double const c = std::cos(a), s = std::sin(a);
    p.sy = -m.m21() * s + m.m22() * c;
    double const lean = m.m21() * c + m.m22() * s;
    p.skew = std::abs(p.sy) > 1e-12 ? std::atan2(lean, p.sy) * 180.0 / kPi : 0.0;
    if (p.sy < 0.0)
        p.skew = std::atan(lean / p.sy) * 180.0 / kPi;
    return p;
}

QTransform fromParts(Parts const& p)
{
    double const a = p.angle * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    double const k = std::tan(p.skew * kPi / 180.0) * p.sy;
    return QTransform(p.sx * c, p.sx * s, k * c - p.sy * s, k * s + p.sy * c, p.x, p.y);
}

Parts mixed(Parts const& a, Parts const& b, double t)
{
    Parts m;
    m.x = a.x + (b.x - a.x) * t;
    m.y = a.y + (b.y - a.y) * t;
    m.angle = a.angle + std::remainder(b.angle - a.angle, 360.0) * t;
    m.sx = a.sx + (b.sx - a.sx) * t;
    m.sy = a.sy + (b.sy - a.sy) * t;
    m.skew = a.skew + (b.skew - a.skew) * t;
    return m;
}

// Where a layer's pivot lies in the scene.
QPointF anchorAt(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    return flatChain(scene, layer, frame, over).map(scene.pivotOf(layer, frame));
}

// The affine map that takes `from` (three or more points) as near `to` as it can (least squares).
bool affineFit(QVector<QPointF> const& from, QVector<QPointF> const& to, QTransform& out)
{
    int const n = int(std::min(from.size(), to.size()));
    if (n < 3)
        return false;
    // Normal equations for [x y 1] * [a c; b d; e f]: one 3x3 system for each column of the result.
    double m[3][3] = {}, rx[3] = {}, ry[3] = {};
    for (int i = 0; i < n; ++i) {
        double const v[3] = {from[i].x(), from[i].y(), 1.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                m[r][c] += v[r] * v[c];
            rx[r] += v[r] * to[i].x();
            ry[r] += v[r] * to[i].y();
        }
    }
    auto det3 = [](double const a[3][3]) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    double const d = det3(m);
    if (std::abs(d) < 1e-12)
        return false;
    auto solve = [&](double const r[3], double out3[3]) {
        for (int k = 0; k < 3; ++k) {
            double t[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    t[i][j] = j == k ? r[i] : m[i][j];
            out3[k] = det3(t) / d;
        }
    };
    double cx[3], cy[3];
    solve(rx, cx);
    solve(ry, cy);
    out = QTransform(cx[0], cy[0], cx[1], cy[1], cx[2], cy[2]);
    return true;
}

// The parent transformation a constrained layer has on `frame` (false: it has none, and hangs as usual).
bool constrainedParent(Scene const& scene, int layer, int frame, PoseOverride const* over, QTransform& out)
{
    Layer const& l = scene.layers[layer];
    Constraint const& c = l.constraint;
    if (c.kind == Constraint::None)
        return false;
    static thread_local int depth = 0;
    if (depth >= 8)
        return false;       // (constraints that lean on one another round in a ring go no deeper)
    struct Guard {
        Guard() { ++depth; }
        ~Guard() { --depth; }
    } const guard;
    int const parent = scene.parentIndex(layer);
    auto chainOf = [&](int other, int f) { return other >= 0 ? flatChain(scene, other, f, over) : QTransform(); };
    QVector<int> anchors;
    for (int id : c.anchors)
        if (int const at = scene.layerIndex(id); at >= 0 && at != layer)
            anchors.push_back(at);
    switch (c.kind) {
    case Constraint::Points: {
        if (anchors.size() < 2 || c.rest.size() < anchors.size())
            return false;
        QVector<QPointF> now;
        for (int a : anchors)
            now.push_back(anchorAt(scene, a, frame, over));
        // The first two: turned and stretched between them, squashed across as the volume asks.
        QPointF const r0 = c.rest[0], v0 = c.rest[1] - c.rest[0], p0 = now[0], v1 = now[1] - now[0];
        double const len0 = std::hypot(v0.x(), v0.y());
        double len1 = std::hypot(v1.x(), v1.y());
        if (len0 < 1e-9)
            return false;
        if (c.distanceMin > 0.0)
            len1 = std::max(len1, c.distanceMin);
        if (c.distanceMax > 0.0)
            len1 = std::min(len1, c.distanceMax);
        double const ratio = len1 / len0;
        double const across = std::clamp(std::pow(ratio, -c.volume), c.volumeMin, c.volumeMax);
        QTransform two = QTransform::fromTranslate(-r0.x(), -r0.y());
        two *= QTransform().rotateRadians(-std::atan2(v0.y(), v0.x()));
        two *= QTransform::fromScale(ratio, across);
        two *= QTransform().rotateRadians(std::atan2(v1.y(), v1.x()));
        two *= QTransform::fromTranslate(p0.x(), p0.y());
        if (anchors.size() == 2 || c.balance <= 0.0) {
            out = two;
            return true;
        }
        QTransform fit;
        if (!affineFit(c.rest.mid(0, anchors.size()), now, fit)) {
            out = two;
            return true;
        }
        Parts parts = partsOf(fit);
        parts.skew *= c.skewModifier;
        if (c.skewType == Constraint::SkewIgnored)
            parts.skew = 0.0;
        else if (c.skewType == Constraint::SkewAsRotation) {
            parts.angle += parts.skew / 2.0;
            parts.skew = 0.0;
        }
        if (parts.sx * parts.sy < 0.0 && !c.allowFlip) {
            // Not flipped: the axis whose flip is smaller (or the one allowed) put back.
            bool const backX = !c.flipX || (c.flipY && std::abs(parts.sx) < std::abs(parts.sy));
            if (backX)
                parts.sx = -parts.sx;
            else
                parts.sy = -parts.sy;
        }
        out = fromParts(mixed(partsOf(two), parts, std::clamp(c.balance, 0.0, 1.0)));
        return true;
    }
    case Constraint::Spring: {
        if (parent < 0)
            return false;
        // Its parent's movement followed with inertia and tension, from the first frame on.
        auto target = [&](int f) { return chainOf(parent, f).map(scene.pivotOf(layer, f)); };
        QPointF spot = target(0), speed;
        Parts const first = partsOf(chainOf(parent, 0));
        double angle = first.angle, spin = 0.0, size = first.sx, grow = 0.0, lean = first.skew, tilt = 0.0;
        double was = first.angle;
        for (int f = 1; f <= frame; ++f) {
            QPointF const aim = target(f);
            Parts const now = partsOf(chainOf(parent, f));
            double const unwrapped = was + std::remainder(now.angle - was, 360.0);
            was = unwrapped;
            speed = QPointF(speed.x() * c.inertiaX + (aim.x() - spot.x()) * c.tensionX, speed.y() * c.inertiaY + (aim.y() - spot.y()) * c.tensionY);
            spot += speed;
            spin = spin * c.inertiaRotate + (unwrapped - angle) * c.tensionRotate;
            angle += spin;
            grow = grow * c.inertiaScale + (now.sx - size) * c.tensionScale;
            size += grow;
            tilt = tilt * c.inertiaSkew + (now.skew - lean) * c.tensionSkew;
            lean += tilt;
        }
        QTransform const here = chainOf(parent, frame);
        Parts const now = partsOf(here);
        QPointF const aim = target(frame);
        double const behind = angle - was;
        double const scaled = now.sx > 1e-9 ? size / now.sx : 1.0;
        double const leaning = std::tan(lean * kPi / 180.0) - std::tan(now.skew * kPi / 180.0);
        out = here * QTransform::fromTranslate(-aim.x(), -aim.y()) * QTransform().rotate(behind) * QTransform::fromScale(scaled, scaled)
              * QTransform(1.0, 0.0, leaning, 1.0, 0.0, 0.0) * QTransform::fromTranslate(spot.x(), spot.y());
        return true;
    }
    case Constraint::Static: {
        Parts p;
        p.x = c.staticX;
        p.y = c.staticY;
        p.angle = c.staticAngle;
        p.sx = c.staticScaleX;
        p.sy = c.staticUniform ? c.staticScaleX : c.staticScaleY;
        p.skew = c.staticSkew;
        // (Skew Y: the uprights stay, the level lines lean.)
        out = QTransform(1.0, std::tan(c.staticSkewY * kPi / 180.0), 0.0, 1.0, 0.0, 0.0) * fromParts(p);
        if (c.fromGrandparent && parent >= 0)
            out = out * chainOf(scene.parentIndex(parent), frame);
        return true;
    }
    case Constraint::Limit: {
        double const amount = std::clamp(poseFor(scene, layer, frame, over).constraint, 0.0, 1.0);
        Parts const full = partsOf(chainOf(parent, frame));
        Parts limited = full;
        Parts const none;
        Parts const part = mixed(none, full, amount);
        if (c.limitTranslate) {
            limited.x = part.x;
            limited.y = part.y;
        }
        if (c.limitRotate)
            limited.angle = part.angle;
        if (c.limitScale) {
            limited.sx = part.sx;
            limited.sy = part.sy;
        }
        if (c.limitSkew)
            limited.skew = part.skew;
        out = fromParts(limited);
        if (c.invert && out.isInvertible())
            out = out.inverted();
        return true;
    }
    case Constraint::Gate: {
        if (anchors.isEmpty())
            return false;
        int const a = anchors.size() >= 2 ? anchors[0] : parent, b = anchors.size() >= 2 ? anchors[1] : anchors[0];
        double const amount = std::clamp(poseFor(scene, layer, frame, over).constraint, 0.0, 1.0);
        out = fromParts(mixed(partsOf(chainOf(a, frame)), partsOf(chainOf(b, frame)), amount));
        return true;
    }
    case Constraint::Switch: {
        // Which, held from its keyframe on (Transformation 1, 2 ...; below 1: the parent).
        double value = l.poseAt(frame).constraint;
        for (Keyframe const& k : l.keys)
            if (k.frame <= frame)
                value = k.pose.constraint;
        auto pick = [&](double v, int f) {
            int const which = int(std::lround(v)) - 1;
            return which >= 0 && which < anchors.size() ? chainOf(anchors[which], f) : chainOf(parent, f);
        };
        out = pick(value, frame);
        if (c.matchSwitch) {
            // Match Animation on Active Attribute: where it switched last, the new transformation put where the old one was, so
            // that the layer does not jump.
            int switchedAt = -1;
            double before = value;
            for (int k = 1; k < l.keys.size(); ++k)
                if (l.keys[k].frame <= frame && std::lround(l.keys[k].pose.constraint) != std::lround(l.keys[k - 1].pose.constraint)) {
                    switchedAt = l.keys[k].frame;
                    before = l.keys[k - 1].pose.constraint;
                }
            if (switchedAt >= 0) {
                QTransform const was = pick(before, switchedAt), then = pick(value, switchedAt);
                if (then.isInvertible())
                    out = was * then.inverted() * out;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

// From a layer to the scene on its own plane, before any camera alignment.
QTransform flatChain(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    QTransform t;
    for (int guard = 0; layer >= 0 && layer < scene.layers.size() && guard < 256; ++guard, layer = scene.parentIndex(layer)) {
        t = t * localFor(scene, layer, frame, over);
        // A constraint says what it hangs from, in place of its parents.
        if (QTransform held; constrainedParent(scene, layer, frame, over, held))
            return t * held;
        int const parent = scene.parentIndex(layer);
        if (parent >= 0 && scene.layers[layer].parentBone >= 0)
            t = t * Deform::boneTransform(scene.layers[parent].bonesAt(frame), poseFor(scene, parent, frame, over).bend, scene.layers[layer].parentBone);
        else if (parent >= 0 && !scene.layers[layer].trackers.isEmpty())
            t = t * Deform::trackerTransform(scene.layers[parent], poseFor(scene, parent, frame, over), scene.layers[layer].trackers, scene.layers[layer].trackerSampling);
        if (parent >= 0 && scene.layers[layer].ignoreParentScale)
            return t * withoutScale(flatChain(scene, parent, frame, over));
    }
    return t;
}

// Turning about a point as the active camera is turned (or, `back`, the other way, as the camera sees it).
Mat4 cameraTurn(Scene const& scene, int frame, PoseOverride const* over, double x, double y, double z, bool back)
{
    int const camera = scene.cameraIndex();
    Pose const pose = camera >= 0 ? poseFor(scene, camera, frame, over) : Pose();
    if (pose.rotX == 0.0 && pose.rotY == 0.0)
        return Mat4();
    if (back)
        return Mat4::move(-x, -y, -z) * Mat4::aboutX(-pose.rotX) * Mat4::aboutY(-pose.rotY) * Mat4::move(x, y, z);
    return Mat4::move(-x, -y, -z) * Mat4::aboutY(pose.rotY) * Mat4::aboutX(pose.rotX) * Mat4::move(x, y, z);
}

Mat4 spaceChain(Scene const& scene, int layer, int frame, PoseOverride const* over);

// From a layer to the scene, in space: as it hangs, then as its camera alignment has it.
Mat4 spaceOf(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    Mat4 t = spaceChain(scene, layer, frame, over);
    int const camera = scene.cameraIndex();
    if (camera < 0 || layer < 0 || layer >= scene.layers.size())
        return t;
    int const align = scene.cameraAlignmentOf(layer);
    if (align == Layer::PinToCamera) {
        // Carried by the camera: placed on its picture, at its depth, and turned with it.
        QTransform const carried = flatChain(scene, camera, frame, over);
        QPointF const centre = carried.map(QPointF(scene.width / 2.0, scene.height / 2.0));
        double const depth = scene.depthOf(camera, frame, over);
        return t * Mat4::flat(carried) * Mat4::move(0, 0, depth) * cameraTurn(scene, frame, over, centre.x(), centre.y(), depth, false);
    }
    if (align == Layer::FaceCamera) {
        // Where its pivot is, kept; how it is tilted, undone - and turned as the camera is, so it faces it square.
        QPointF const pivot = scene.pivotOf(layer, frame);
        double x, y, z;
        t.map(pivot, x, y, z);
        QTransform const flat = flatChain(scene, layer, frame, over);
        QPointF const at = flat.map(pivot);
        return Mat4::flat(flat * QTransform::fromTranslate(x - at.x(), y - at.y())) * Mat4::move(0, 0, z) * cameraTurn(scene, frame, over, x, y, z, false);
    }
    return t;
}

// From a layer to the scene, in space, as it hangs.
Mat4 spaceChain(Scene const& scene, int layer, int frame, PoseOverride const* over)
{
    Mat4 t;
    for (int guard = 0; layer >= 0 && layer < scene.layers.size() && guard < 256; ++guard, layer = scene.parentIndex(layer)) {
        Layer const& l = scene.layers[layer];
        Pose const pose = poseFor(scene, layer, frame, over);
        t = t * Mat4::flat(localFor(scene, layer, frame, over));
        // (A 3D model turns in its own picture: the sheet of it stays flat.)
        if ((pose.rotX != 0.0 || pose.rotY != 0.0) && !l.modelAt(frame)) {
            // About the pivot, where the layer has put it.
            QPointF const pivot(l.pivot.x() + pose.x, l.pivot.y() + pose.y);
            t = t * Mat4::move(-pivot.x(), -pivot.y(), 0) * Mat4::aboutY(pose.rotY) * Mat4::aboutX(pose.rotX) * Mat4::move(pivot.x(), pivot.y(), 0);
        }
        if (pose.z != 0.0)
            t = t * Mat4::move(0, 0, pose.z);
        // (A constraint works on the flat plane: in space it holds as it does there.)
        if (QTransform held; constrainedParent(scene, layer, frame, over, held))
            return t * Mat4::flat(held);
        int const parent = scene.parentIndex(layer);
        if (parent >= 0 && l.parentBone >= 0)
            t = t * Mat4::flat(Deform::boneTransform(scene.layers[parent].bonesAt(frame), poseFor(scene, parent, frame, over).bend, l.parentBone));
        else if (parent >= 0 && !l.trackers.isEmpty())
            t = t * Mat4::flat(Deform::trackerTransform(scene.layers[parent], poseFor(scene, parent, frame, over), l.trackers, l.trackerSampling));
    }
    return t;
}

}  // namespace

int Scene::cameraAlignmentOf(int layer) const
{
    if (layer < 0 || layer >= layers.size() || layers[layer].kind == Layer::Camera)
        return Layer::AlignNormal;
    for (int guard = 0; layer >= 0 && layer < layers.size() && guard < 256; ++guard, layer = parentIndex(layer))
        if (layers[layer].kind != Layer::Camera && layers[layer].cameraAlignment != Layer::AlignNormal)
            return layers[layer].cameraAlignment;
    return Layer::AlignNormal;
}

double Scene::truckFactor(int layer, int frame) const
{
    if (layer < 0 || layer >= layers.size() || cameraAlignmentOf(layer) == Layer::ProjectOnCameraPlane)
        return 1.0;
    return depthScale(depthOf(layer, frame), frame);
}

double Layer::fovAt(int frame) const
{
    double const keyed = poseAt(frame).fov;
    return std::clamp(keyed > 0.0 ? keyed : fov, Layer::kMinFov, Layer::kMaxFov);
}

bool Scene::turned(int layer, int frame, PoseOverride const* over) const
{
    for (int guard = 0; layer >= 0 && layer < layers.size() && guard < 256; ++guard, layer = parentIndex(layer)) {
        Pose const pose = poseFor(*this, layer, frame, over);
        if ((pose.rotX != 0.0 || pose.rotY != 0.0) && !layers[layer].modelAt(frame))
            return true;
    }
    return false;
}

void Scene::toSpace(int layer, int frame, QPointF p, double& x, double& y, double& z, PoseOverride const* over) const
{
    x = p.x();
    y = p.y();
    z = 0.0;
    if (layer >= 0 && layer < layers.size())
        spaceOf(*this, layer, frame, over).map(p, x, y, z);
}

void Scene::depthPlane(int layer, int frame, double& a, double& b, double& c) const
{
    a = b = 0.0;
    c = depthOf(layer, frame);
    if (layer < 0 || layer >= layers.size() || layers[layer].kind == Layer::Camera)
        return;
    Mat4 space = spaceOf(*this, layer, frame, nullptr);
    int const camera = cameraIndex();
    Pose const cameraPose = camera >= 0 ? poseFor(*this, camera, frame, nullptr) : Pose();
    QTransform const lens = cameraTransform(frame);
    if ((cameraPose.rotX != 0.0 || cameraPose.rotY != 0.0) && lens.isInvertible()) {
        // As worldTransform() has it: what a turned camera sees is turned the other way.
        double const cameraDepth = depthOf(camera, frame);
        QPointF const centre = lens.inverted().map(QPointF(width / 2.0, height / 2.0));
        space = space * Mat4::move(-centre.x(), -centre.y(), -cameraDepth) * Mat4::aboutX(-cameraPose.rotX) * Mat4::aboutY(-cameraPose.rotY)
                * Mat4::move(centre.x(), centre.y(), cameraDepth);
    }
    double x, y, z0, z1, z2;
    space.map(QPointF(0, 0), x, y, z0);
    space.map(QPointF(1, 0), x, y, z1);
    space.map(QPointF(0, 1), x, y, z2);
    a = z1 - z0;
    b = z2 - z0;
    c = z0;
}

double Scene::depthOf(int layer, int frame, PoseOverride const* over) const
{
    if (layer < 0 || layer >= layers.size())
        return 0.0;
    if (layers[layer].kind != Layer::Camera && turned(layer, frame, over)) {
        double x, y, z;
        toSpace(layer, frame, layers[layer].pivot, x, y, z, over);
        return z;
    }
    double z = 0.0;
    bool const pinned = layers[layer].kind != Layer::Camera && cameraAlignmentOf(layer) == Layer::PinToCamera && cameraIndex() >= 0;
    for (int guard = 0; layer >= 0 && layer < layers.size() && guard < 256; ++guard, layer = parentIndex(layer))
        z += poseFor(*this, layer, frame, over).z;
    return pinned ? z + depthOf(cameraIndex(), frame, over) : z;
}

double Scene::focalDistance(int frame) const
{
    int const camera = cameraIndex();
    double const keyed = camera >= 0 ? poseOf(camera, frame).fov : 0.0;
    double const angle = camera >= 0 ? (keyed > 0.0 ? std::clamp(keyed, Layer::kMinFov, Layer::kMaxFov) : layers[camera].fovAt(frame)) : fov;
    return (fovAcross ? width : height) / 2.0 / std::tan(angle * 3.14159265358979323846 / 360.0);
}

double Scene::depthScale(double z, int frame) const
{
    if (orthographic)
        return 1.0;
    int const camera = cameraIndex();
    double const away = z - (camera >= 0 ? depthOf(camera, frame) : 0.0);   // from the plane the camera sees as it is
    if (std::abs(away) < 1e-9)
        return 1.0;
    double const distance = focalDistance(frame);
    // Right up against the camera it is as big as it gets (and past it, it is not drawn at all).
    return distance / std::max(distance - away, distance * 0.02);
}

bool Scene::behindCamera(int layer, int frame) const
{
    if (layer < 0 || layer >= layers.size() || layers[layer].kind == Layer::Camera)
        return false;
    int const camera = cameraIndex();
    double const away = depthOf(layer, frame) - (camera >= 0 ? depthOf(camera, frame) : 0.0);
    if (camera >= 0 && layers[layer].holdsDrawings()) {
        Drawing const drawing = layers[layer].shownAt(frame);
        if (!drawing.isEmpty())
            return cameraClip(layer, frame, drawing.bounds().adjusted(-2, -2, 2, 2)).isEmpty();
    }
    return !orthographic && away >= focalDistance(frame) * 0.98;
}

QPolygonF Scene::cameraClip(int layer, int frame, QRectF bounds) const
{
    QPolygonF polygon(bounds);
    int const camera = cameraIndex();
    if (camera < 0 || layer < 0 || layer >= layers.size())
        return polygon;
    double a, b, c;
    depthPlane(layer, frame, a, b, c);
    double const eye = depthOf(camera, frame) + focalDistance(frame);
    double const near = std::max(layers[camera].nearPlane, orthographic ? 0.0 : focalDistance(frame) * 0.02);
    auto cut = [&](double plane, bool closer) {
        QPolygonF kept;
        if (polygon.isEmpty())
            return;
        auto value = [&](QPointF p) { return (closer ? 1.0 : -1.0) * (eye - a * p.x() - b * p.y() - c - plane); };
        QPointF previous = polygon.last();
        double before = value(previous);
        for (QPointF const& p : std::as_const(polygon)) {
            double const after = value(p);
            if ((before >= 0.0) != (after >= 0.0))
                kept << previous + (p - previous) * (before / (before - after));
            if (after >= 0.0)
                kept << p;
            previous = p;
            before = after;
        }
        polygon = kept;
    };
    if (!orthographic || layers[camera].nearPlane > 0.0)
        cut(near, true);
    if (layers[camera].farPlane > 0.0)
        cut(layers[camera].farPlane, false);
    return polygon;
}

QTransform Scene::depthTransform(double z, int frame) const
{
    double const scale = depthScale(z, frame);
    if (scale == 1.0)
        return QTransform();
    QTransform const camera = cameraTransform(frame);
    QTransform about;
    about.translate(width / 2.0, height / 2.0);
    about.scale(scale, scale);
    about.translate(-width / 2.0, -height / 2.0);
    return camera * about * camera.inverted();
}

QVector<int> Scene::drawOrder(int frame) const
{
    QVector<int> order;
    QVector<double> depth(layers.size());
    bool flat = true;
    for (int i = 0; i < layers.size(); ++i) {
        depth[i] = depthOf(i, frame);
        flat = flat && depth[i] == depth[0];
    }
    int const n = int(layers.size());
    QVector<QVector<int>> children(n);
    QVector<int> tops;
    for (int l = n - 1; l >= 0; --l) {
        int const parent = parentIndex(l);
        (parent >= 0 && parent < n && parent != l ? children[parent] : tops).push_back(l);
    }
    QVector<int> front;
    QVector<bool> seen(n, false);
    auto visit = [&](auto&& self, int l, int guard) -> void {
        if (seen[l] || guard > 256)
            return;
        seen[l] = true;
        front.push_back(l);
        for (int child : std::as_const(children[l]))
            self(self, child, guard + 1);
    };
    for (int l : std::as_const(tops))
        visit(visit, l, 0);
    for (int l = n - 1; l >= 0; --l)
        visit(visit, l, 0);     // (a loop a damaged file left)
    for (int k = n - 1; k >= 0; --k) {
        int const i = front[k];
        if (!behindCamera(i, frame))
            order.push_back(i);
    }
    if (!flat)
        std::stable_sort(order.begin(), order.end(), [&depth](int a, int b) { return depth[a] < depth[b]; });
    return order;
}

QTransform Scene::cameraTransform(int frame) const
{
    int const camera = cameraIndex();
    // The camera is drawn where its matrix puts the frame; seeing through it is the reverse.
    return camera >= 0 ? flatTransform(camera, frame).inverted() : QTransform();
}

double Scene::customValue(int layer, QString const& name, int frame) const
{
    if (layer < 0 || layer >= layers.size())
        return 0.0;
    for (CustomAttribute const& a : layers[layer].customAttributes)
        if (a.name.compare(name, Qt::CaseInsensitive) == 0) {
            FunctionColumn const* const column = function(a.function);
            return column ? column->valueFor(Driver::X, frame, *this) : a.value;
        }
    return 0.0;
}

QTransform Scene::contentTransform(int layer) const
{
    if (layer < 0 || layer >= layers.size())
        return QTransform();
    Layer const& l = layers[layer];
    if (!l.turnBeforeAlignment && !l.flipHorizontal && !l.flipVertical)
        return QTransform();
    // About the middle of the frame: a quarter turn, then the flips.
    QPointF const middle(width / 2.0, height / 2.0);
    QTransform t = QTransform::fromTranslate(-middle.x(), -middle.y());
    if (l.turnBeforeAlignment)
        t = t * QTransform().rotate(90.0);
    t = t * QTransform::fromScale(l.flipHorizontal ? -1.0 : 1.0, l.flipVertical ? -1.0 : 1.0);
    return t * QTransform::fromTranslate(middle.x(), middle.y());
}

QTransform Scene::worldTransform(int layer, int frame, PoseOverride const* over) const
{
    return contentTransform(layer) * spaceTransform(layer, frame, over);
}

QTransform Scene::spaceTransform(int layer, int frame, PoseOverride const* over) const
{
    if (layer < 0 || layer >= layers.size())
        return QTransform();
    // A camera is not seen through itself: it lies where it lies.
    if (layers[layer].kind == Layer::Camera)
        return flatTransform(layer, frame, over);
    int const camera = cameraIndex();
    Pose const cameraPose = camera >= 0 ? poseFor(*this, camera, frame, over) : Pose();
    bool const cameraTurned = cameraPose.rotX != 0.0 || cameraPose.rotY != 0.0;
    if (turned(layer, frame, over) || cameraTurned) {
        QTransform const lens = cameraTransform(frame);
        double const cameraDepth = camera >= 0 ? depthOf(camera, frame) : 0.0, distance = focalDistance(frame);
        QPointF const middle(width / 2.0, height / 2.0);
        Mat4 space = spaceOf(*this, layer, frame, over);
        if (cameraTurned && lens.isInvertible()) {
            // The camera turned one way: what it sees is turned the other, about the middle of what it looks at.
            QPointF const centre = lens.inverted().map(middle);
            space = space * Mat4::move(-centre.x(), -centre.y(), -cameraDepth) * Mat4::aboutX(-cameraPose.rotX) * Mat4::aboutY(-cameraPose.rotY)
                    * Mat4::move(centre.x(), centre.y(), cameraDepth);
        }
        space = space * Mat4::flat(lens);
        if (!orthographic && cameraAlignmentOf(layer) != Layer::ProjectOnCameraPlane) {
            Mat4 projection;
            projection.m[2][3] = -1.0 / distance;
            space = space * Mat4::move(-middle.x(), -middle.y(), -cameraDepth) * projection * Mat4::move(middle.x(), middle.y(), 0);
        }
        QTransform const picture(space.m[0][0], space.m[0][1], space.m[0][3], space.m[1][0], space.m[1][1], space.m[1][3],
                                 space.m[3][0], space.m[3][1], space.m[3][3]);
        return picture * lens.inverted();
    }
    if (cameraAlignmentOf(layer) == Layer::ProjectOnCameraPlane)
        return flatTransform(layer, frame, over);
    return flatTransform(layer, frame, over) * depthTransform(depthOf(layer, frame, over), frame);
}

QTransform Scene::flatTransform(int layer, int frame, PoseOverride const* over) const
{
    QTransform const t = flatChain(*this, layer, frame, over);
    int const camera = cameraIndex();
    // Pinned to the camera: what the camera does, the layer does too.
    if (camera >= 0 && layer >= 0 && layer < layers.size() && cameraAlignmentOf(layer) == Layer::PinToCamera)
        return t * flatChain(*this, camera, frame, over);
    return t;
}

QPointF Scene::pivotOf(int layer, int frame) const
{
    if (layer < 0 || layer >= layers.size())
        return QPointF();
    Layer const& l = layers[layer];
    auto drawingPivot = [this, frame](int at, QPointF* p) {
        Layer const& d = layers[at];
        int const index = d.holdsDrawings() ? d.drawingAt(frame) : Layer::kNoDrawing;
        if (index < 0 || index >= d.drawings.size() || !d.drawings[index].pivotSet)
            return false;
        *p = d.drawings[index].pivot;
        return true;
    };
    QPointF p;
    if (l.embeddedPivot == Layer::PivotOnLayer && drawingPivot(layer, &p))
        return p;
    if (l.kind == Layer::Peg) {
        // A drawing hung right on the peg that gives it its pivot: where that is, in the peg's own space.
        for (int c = 0; c < layers.size(); ++c)
            if (layers[c].parent == l.id && layers[c].embeddedPivot == Layer::PivotOnParentPeg && drawingPivot(c, &p))
                return layers[c].matrixFor(layers[c].poseAt(frame), layers[c].pivot).map(p);
    }
    return l.pivot;
}

double Driver::read(Pose const& pose, int value)
{
    switch (value) {
    case X: return pose.x;
    case Y: return pose.y;
    case ScaleX: return pose.scaleX;
    case ScaleY: return pose.scaleY;
    case Angle: return pose.rotation;
    case Opacity: return pose.opacity;
    case Z: return pose.z;
    case Skew: return pose.skew;
    case Fov: return pose.fov;
    default: return 0.0;
    }
}

void Driver::write(Pose& pose, int value, double v)
{
    switch (value) {
    case X: pose.x = v; break;
    case Y: pose.y = v; break;
    case ScaleX: pose.scaleX = v; break;
    case ScaleY: pose.scaleY = v; break;
    case Angle: pose.rotation = v; break;
    case Opacity: pose.opacity = std::clamp(v, 0.0, 1.0); break;
    case Z: pose.z = v; break;
    case Skew: pose.skew = v; break;
    case Fov: pose.fov = std::clamp(v, Layer::kMinFov, Layer::kMaxFov); break;
    default: break;
    }
}

Pose Scene::poseOf(int layer, int frame, PoseOverride const* over) const
{
    if (layer < 0 || layer >= layers.size())
        return Pose();
    return poseFor(*this, layer, frame, over);
}

Constraint Scene::bakedConstraint(int layer, int frame, bool immediateOnly) const
{
    Constraint c = layer >= 0 && layer < layers.size() ? layers[layer].constraint : Constraint();
    int const parent = layer >= 0 && layer < layers.size() ? parentIndex(layer) : -1;
    QTransform held;
    if (parent >= 0)
        held = immediateOnly ? localFor(*this, parent, frame, nullptr) : flatChain(*this, parent, frame, nullptr);
    Parts const p = partsOf(held);
    c.kind = Constraint::Static;
    c.staticX = p.x;
    c.staticY = p.y;
    c.staticAngle = p.angle;
    c.staticScaleX = p.sx;
    c.staticScaleY = p.sy;
    c.staticSkew = p.skew;
    c.fromGrandparent = immediateOnly;
    return c;
}

QVector<QPointF> Scene::anchorPlaces(QVector<int> const& anchorIds, int frame) const
{
    QVector<QPointF> places;
    for (int id : anchorIds)
        if (int const at = layerIndex(id); at >= 0)
            places.push_back(anchorAt(*this, at, frame, nullptr));
    return places;
}
