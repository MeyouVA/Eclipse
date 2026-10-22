// Eclipse - what a layer shows once deformed: game bone chains, drawings bent on bones and ridden.
#include "model/geometry/Deform.h"

#include "model/geometry/Model3D.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include "model/geometry/DeformShared.h"

namespace Deform {

// Everything of a drawing moved by `t`.
static void transformAll(Drawing& drawing, QTransform const& t)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill& fill : drawing.fillsOf(sheet))
            fill.transform(t);
        for (Stroke& stroke : drawing.strokesOf(sheet))
            stroke.transform(t);
    }
    for (Picture& picture : drawing.pictures)
        picture.transform(t);
}

namespace {

bool gameChain(Scene const& scene, int layer, int frame, QVector<int>& chain, QVector<QPointF>& joints, QVector<double>& bend, QTransform& into)
{
    chain.clear();
    joints.clear();
    bend.clear();
    for (int b = scene.parentIndex(layer), guard = 0; b >= 0 && guard < 64 && scene.layers[b].kind == Layer::Bone; b = scene.parentIndex(b), ++guard)
        chain.push_back(b);
    if (chain.isEmpty())
        return false;
    std::reverse(chain.begin(), chain.end());      // from the first bone to the last
    auto local = [&scene](int l, int f) { return scene.layers[l].matrixFor(scene.layers[l].poseAt(f)); };
    QVector<QTransform> toRoot(chain.size());
    for (int i = 0; i < chain.size(); ++i)
        toRoot[i] = i == 0 ? QTransform() : local(chain[i], 0) * toRoot[i - 1];
    into = local(layer, frame) * toRoot.last();
    for (int i = 0; i < chain.size(); ++i) {
        Layer const& bone = scene.layers[chain[i]];
        joints.push_back(toRoot[i].map(bone.pivot));
        bend.push_back(bone.poseAt(frame).rotation - bone.poseAt(0).rotation);
    }
    Layer const& last = scene.layers[chain.last()];
    joints.push_back(toRoot.last().map(last.pivot + QPointF(last.boneLength, 0.0)));
    return true;
}

}  // namespace

QVector<QPointF> gameBoneJoints(Scene const& scene, int layer, int frame)
{
    QVector<int> chain;
    QVector<QPointF> joints;
    QVector<double> bend;
    QTransform into;
    if (layer < 0 || layer >= scene.layers.size() || !gameChain(scene, layer, frame, chain, joints, bend, into) || !into.isInvertible())
        return {};
    QTransform const back = into.inverted();
    for (QPointF& j : joints)
        j = back.map(j);
    return joints;
}

Drawing bentOnBones(Scene const& scene, int layer, int frame, Drawing const& drawing)
{
    if (layer < 0 || layer >= scene.layers.size() || !scene.layers[layer].bendOnBones || scene.turned(layer, frame))
        return drawing;
    QVector<int> chain;
    QVector<QPointF> joints;
    QVector<double> bend;
    QTransform into;
    if (!gameChain(scene, layer, frame, chain, joints, bend, into) || !isBent(joints, bend))
        return drawing;
    int const root = chain.first();
    // The weights painted on the drawing, where the drawing is taken to.
    QVector<SkinDab> skin = scene.layers[layer].skin;
    double const grow = std::sqrt(std::abs(into.determinant()));
    for (SkinDab& d : skin) {
        d.at = into.map(d.at);
        d.radius *= grow;
    }
    // The first bone where it is on this frame, but turned as on frame 0: its turning is in the bending.
    PoseOverride over;
    over.layer = root;
    over.frame = frame;
    over.pose = scene.layers[root].poseAt(frame);
    over.pose.rotation = scene.layers[root].poseAt(0).rotation;
    QTransform const rootNow = scene.worldTransform(root, frame, &over), here = scene.worldTransform(layer, frame);
    if (!here.isInvertible())
        return drawing;
    Drawing out = drawing;
    transformAll(out, into);
    out = deform(out, joints, bend, scene.layers[layer].jointSmoothness, skin);
    transformAll(out, rootNow * here.inverted());
    return out;
}

Drawing ridden(Scene const& scene, int layer, int frame, Drawing const& drawing)
{
    if (layer < 0 || layer >= scene.layers.size())
        return drawing;
    Drawing out = bentOnBones(scene, layer, frame, drawing);
    QTransform const own = scene.worldTransform(layer, frame);
    int guard = 0;
    for (int a = scene.parentIndex(layer); a >= 0 && guard < 64; a = scene.parentIndex(a), ++guard) {
        Layer const& up = scene.layers[a];
        if (!up.hasWarp())
            continue;
        Pose const pose = up.poseAt(frame);
        if (!isWarped(up.warp, up.warpClosed, pose.warp))
            continue;
        // Into the coordinates of that layer, where its deformer lies; reshaped there; and back.
        QTransform const there = scene.worldTransform(a, frame);
        if (!there.isInvertible())
            continue;
        QTransform const into = own * there.inverted();
        if (!into.isInvertible())
            continue;
        transformAll(out, into);
        out = warp(out, up.warp, up.warpClosed, pose.warp);
        transformAll(out, into.inverted());
    }
    return out;
}

Drawing shown(Drawing const& drawing, Layer const& layer, Pose const& pose)
{
    if (!drawing.model.isEmpty()) {
        // A 3D model: a picture of it, turned as the layer is, under whatever is drawn over it.
        Drawing out = drawing;
        out.model.clear();
        if (Picture picture; Model3D::picture(drawing.model, pose.rotX, pose.rotY, picture)) {
            picture.transform(QTransform::fromScale(drawing.modelScale, drawing.modelScale)
                              * QTransform::fromTranslate(drawing.modelCentre.x(), drawing.modelCentre.y()));
            out.pictures.prepend(picture);
        }
        return shown(out, layer, pose);
    }
    bool const warped = isWarped(layer.warp, layer.warpClosed, pose.warp), bent = isBent(layer.bones, pose.bend);
    if (!warped && !bent)
        return drawing;
    if (!bent)
        return warp(drawing, layer.warp, layer.warpClosed, pose.warp);
    if (!warped)
        return deform(drawing, layer.bones, pose.bend, layer.jointSmoothness, layer.skin);
    return deform(warp(drawing, layer.warp, layer.warpClosed, pose.warp), layer.bones, pose.bend, layer.jointSmoothness, layer.skin);
}

}  // namespace Deform
