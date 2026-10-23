// Eclipse - the scene: cameras, depth and turning in space - where each layer is seen.
#include "model/scene/Scene.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

int Scene::cameraIndex() const
{
    int const chosen = layerIndex(activeCamera);
    if (chosen >= 0 && layers[chosen].kind == Layer::Camera)
        return chosen;
    for (int i = 0; i < layers.size(); ++i)
        if (layers[i].kind == Layer::Camera)
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
    Pose pose = over && over->layer == layer && over->frame == frame && !over->hasMatrix ? over->pose : other ? *other : scene.layers[layer].poseAt(frame);
    Layer const& l = scene.layers[layer];
    // The values linked to function columns: as the columns have them.
    for (ValueLink const& link : l.links)
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

// From a layer to the scene, in space.
Mat4 spaceOf(Scene const& scene, int layer, int frame, PoseOverride const* over)
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
        int const parent = scene.parentIndex(layer);
        if (parent >= 0 && l.parentBone >= 0)
            t = t * Mat4::flat(Deform::boneTransform(scene.layers[parent].bones, poseFor(scene, parent, frame, over).bend, l.parentBone));
    }
    return t;
}

}  // namespace

double Layer::fovAt(int frame) const
{
    double const keyed = poseAt(frame).fov;
    return std::clamp(keyed > 0.0 ? keyed : fov, 1.0, 170.0);
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
    for (int guard = 0; layer >= 0 && layer < layers.size() && guard < 256; ++guard, layer = parentIndex(layer))
        z += poseFor(*this, layer, frame, over).z;
    return z;
}

double Scene::focalDistance(int frame) const
{
    int const camera = cameraIndex();
    double const angle = camera >= 0 ? layers[camera].fovAt(frame) : fov;
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
    return away >= focalDistance(frame) * 0.98;
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
        if (flat || !behindCamera(i, frame))
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

QTransform Scene::worldTransform(int layer, int frame, PoseOverride const* over) const
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
        QPointF const middle(width / 2.0, height / 2.0), pivot = layers[layer].pivot;
        Mat4 space = spaceOf(*this, layer, frame, over);
        if (cameraTurned && lens.isInvertible()) {
            // The camera turned one way: what it sees is turned the other, about the middle of what it looks at.
            QPointF const centre = lens.inverted().map(middle);
            space = space * Mat4::move(-centre.x(), -centre.y(), -cameraDepth) * Mat4::aboutX(-cameraPose.rotX) * Mat4::aboutY(-cameraPose.rotY)
                    * Mat4::move(centre.x(), centre.y(), cameraDepth);
        }
        QPolygonF from, to;
        bool seen = true;
        for (QPointF const& corner : {QPointF(-200, -200), QPointF(200, -200), QPointF(200, 200), QPointF(-200, 200)}) {
            double x, y, z;
            space.map(pivot + corner, x, y, z);
            double const left = distance - (z - cameraDepth);
            if (left < distance * 0.02)
                seen = false;       // that corner is behind the camera
            from << pivot + corner;
            to << middle + (lens.map(QPointF(x, y)) - middle) * (distance / std::max(left, distance * 0.02));
        }
        QTransform picture;
        if (seen && QTransform::quadToQuad(from, to, picture) && picture.isInvertible())
            return picture * lens.inverted();
    }
    return flatTransform(layer, frame, over) * depthTransform(depthOf(layer, frame, over), frame);
}

QTransform Scene::flatTransform(int layer, int frame, PoseOverride const* over) const
{
    QTransform t;
    for (int guard = 0; layer >= 0 && layer < layers.size() && guard < 256; ++guard, layer = parentIndex(layer)) {
        t = t * localFor(*this, layer, frame, over);
        int const parent = parentIndex(layer);
        if (parent >= 0 && layers[layer].parentBone >= 0)
            t = t * Deform::boneTransform(layers[parent].bones, poseFor(*this, parent, frame, over).bend, layers[layer].parentBone);
        if (parent >= 0 && layers[layer].ignoreParentScale)
            return t * withoutScale(flatTransform(parent, frame, over));
    }
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
    case Fov: pose.fov = std::clamp(v, 1.0, 170.0); break;
    default: break;
    }
}

Pose Scene::poseOf(int layer, int frame, PoseOverride const* over) const
{
    if (layer < 0 || layer >= layers.size())
        return Pose();
    return poseFor(*this, layer, frame, over);
}
