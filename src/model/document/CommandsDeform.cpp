// Eclipse - the document: deformers' resting position, and a peg that follows a bone (Kinematic Output).
#include "model/document/Commands.h"
#include "model/document/CommandsPrivate.h"

#include "model/geometry/Deform.h"

bool Document::copyPoseToRest(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return false;
    Layer const& now = m_scene.layers[layer];
    Pose const pose = now.poseAt(frame);
    bool const bones = Deform::isBent(now.bones, pose.bend);
    bool const warped = Deform::isWarped(now.warp, now.warpClosed, pose.warp);
    if (!bones && !warped)
        return false;
    apply(tr("Copy Current Position to Resting Position"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        // The chain as it is posed here becomes the chain at rest; every keyframe keeps its pose (its turns taken from the new rest).
        if (bones) {
            l.bones = Deform::posedJoints(l.bones, pose.bend);
            for (Keyframe& k : l.keys) {
                if (!k.sets(Keyframe::PartRest))
                    continue;
                k.pose.bend.resize(std::max(k.pose.bend.size(), pose.bend.size()));
                for (int i = 0; i < pose.bend.size(); ++i)
                    k.pose.bend[i] -= pose.bend[i];
            }
        }
        if (warped) {
            l.warp = Deform::posedWarp(l.warp, pose.warp);
            for (Keyframe& k : l.keys) {
                if (!k.sets(Keyframe::PartRest))
                    continue;
                k.pose.warp.resize(std::max(k.pose.warp.size(), pose.warp.size()));
                for (int i = 0; i < pose.warp.size(); ++i)
                    k.pose.warp[i] -= pose.warp[i];
            }
        }
    });
    return true;
}

bool Document::copyRestToPose(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return false;
    Layer const& now = m_scene.layers[layer];
    if (now.bones.size() < 2 && !now.hasWarp())
        return false;
    Pose pose = now.poseAt(frame);
    pose.bend.fill(0.0);
    pose.warp.clear();
    setKeyframe(layer, frame, pose, tr("Copy Resting Position to Current Position"));
    return true;
}

bool Document::setDeformEnabled(int layer, int frame, double amount)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return false;
    Pose pose = m_scene.layers[layer].poseAt(frame);
    pose.deform = std::clamp(amount, 0.0, 1.0);
    setKeyframe(layer, frame, pose, amount > 0.0 ? tr("Enable Deformation") : tr("Disable Deformation"));
    return true;
}

int Document::addKinematicOutput(int layer, int bone)
{
    if (layer < 0 || layer >= m_scene.layers.size() || bone < 0 || bone + 1 >= m_scene.layers[layer].bones.size())
        return -1;
    int made = -1;
    QString const name = m_scene.layers[layer].name + tr("-Output");
    apply(tr("Create Kinematic Output"), [&](Scene& s) {
        made = s.addLayer(name, layer + 1, Layer::Peg);
        Layer& out = s.layers[made];
        out.parent = s.layers[layer].id;
        out.parentBone = bone;
        // Its pivot where the bone ends: what hangs from it turns there.
        out.pivot = s.layers[layer].bones[bone + 1];
    });
    return made;
}

int Document::addPointKinematicOutput(int layer, QVector<QPointF> const& trackers, int sampling)
{
    if (layer < 0 || layer >= m_scene.layers.size() || trackers.isEmpty())
        return -1;
    int made = -1;
    QString const name = m_scene.layers[layer].name + tr("-Point Output");
    apply(tr("Create Kinematic Output"), [&](Scene& s) {
        made = s.addLayer(name, layer + 1, Layer::Peg);
        Layer& out = s.layers[made];
        out.parent = s.layers[layer].id;
        out.parentBone = -1;
        out.trackers = trackers.mid(0, 3);
        out.trackerSampling = std::clamp(sampling, 0, 3);
        out.pivot = trackers.first();
    });
    return made;
}

void Document::setTrackers(int layer, QVector<QPointF> const& trackers, int sampling)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    apply(tr("Change Point Kinematic Output Handle"), [&](Scene& s) {
        s.layers[layer].trackers = trackers.mid(0, 3);
        s.layers[layer].trackerSampling = std::clamp(sampling, 0, 3);
    });
}

int Document::setDrawingTransformation(int layer, int frame, bool own, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return -1;
    };
    if (layer < 0 || layer >= m_scene.layers.size())
        return fail(tr("No element node."));
    Layer const& l = m_scene.layers[layer];
    if (!l.holdsDrawings() || l.showsSymbol())
        return fail(tr("The selected element node is not a drawing layer."));
    int const index = l.drawingAt(frame);
    if (index < 0 || index >= l.drawings.size())
        return fail(l.bones.size() >= 2 ? tr("The selected element node has a deformation attached to it but there is no exposure at the current frame.")
                                         : tr("The selected element node has no exposure at the current frame."));
    bool const has = l.drawings[index].rig.size() >= 2;
    if (own && has)
        return fail(tr("The selected element node has already a deformation for that pose."));
    if (!own && !has)
        return fail(tr("This drawing uses the layer's deformation already (Default Transformation)."));
    if (own && l.bones.size() < 2)
        return fail(tr("The selected element node has no deformation to copy: draw bones with the Rigging tool first."));
    apply(own ? tr("Set Transformation") : tr("Default Transformation"), [&](Scene& s) {
        s.layers[layer].drawings[index].rig = own ? s.layers[layer].bones : QVector<QPointF>();
    });
    return index;
}

int Document::convertToNewDrawingWithChain(int layer, int frame, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return -1;
    };
    if (layer < 0 || layer >= m_scene.layers.size())
        return fail(tr("No element node."));
    Layer const& l = m_scene.layers[layer];
    if (!l.holdsDrawings() || l.showsSymbol())
        return fail(tr("The selected element node is not a drawing layer."));
    int const index = l.drawingAt(frame);
    if (index < 0 || index >= l.drawings.size())
        return fail(tr("The selected element node has no exposure at the current frame."));
    QVector<QPointF> const& chain = l.bonesAt(frame);
    if (chain.size() < 2)
        return fail(tr("The deformation structure for this drawing has not been created: draw bones with the Rigging tool first."));
    Pose const pose = l.poseAt(frame);
    // The drawing as it is bent here, kept as a new drawing with the chain as posed here for its own rest: it shows the same,
    // and it can be bent on from there.
    Drawing made = Deform::shown(l.drawings[index], l, pose, index);
    if (made.strokes.isEmpty() && made.fills.isEmpty() && made.pictures.isEmpty() && !l.drawings[index].isEmpty())
        return fail(tr("The conversion failed to create a new drawing."));
    made.rig = Deform::posedJoints(chain, pose.bend);
    made.name.clear();
    int at = -1;
    apply(tr("Convert to New Drawing and Add Deformation Chain"), [&](Scene& s) {
        Layer& edited = s.layers[layer];
        edited.drawings.push_back(made);
        at = int(edited.drawings.size()) - 1;
        // Exposed where the old one was, from this frame to the end of that exposure.
        for (int f = frame; f < edited.cells.size() && edited.cells[f] == index; ++f)
            edited.cells[f] = at;
        // Its pose there: the chain at rest (the bend is in the new drawing's rest).
        Pose rest = edited.poseAt(frame);
        rest.bend.fill(0.0);
        edited.setKey(frame, rest);
    });
    if (at < 0)
        return fail(tr("The conversion failed to create the new pose."));
    return at;
}

void Document::setPointDeformer(int layer, PointDeformer const& deformer, QString const& text, int removed)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].pointDeformer == deformer)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        bool const reshaped = l.pointDeformer.kind != deformer.kind || l.pointDeformer.cols != deformer.cols || l.pointDeformer.rows != deformer.rows
                              || l.pointDeformer.area != deformer.area;
        l.pointDeformer = deformer;
        for (Keyframe& k : l.keys) {
            if (reshaped || !deformer.any())
                k.pose.handles.clear();      // (another grid: the moves of the old one mean nothing to it)
            else if (removed >= 0 && removed < k.pose.handles.size())
                k.pose.handles.removeAt(removed);
            if (k.pose.handles.size() > deformer.points.size())
                k.pose.handles.resize(deformer.points.size());
        }
    });
}

bool Document::applyDeformation(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return false;
    Layer const& l = m_scene.layers[layer];
    int const index = l.drawingAt(frame);
    if (!l.holdsDrawings() || index < 0 || index >= l.drawings.size())
        return false;
    Pose const pose = l.poseAt(frame);
    bool const bent = pose.deform > 0.0
                      && (Deform::isBent(l.bonesAt(frame), pose.bend) || Deform::isWarped(l.warp, l.warpClosed, pose.warp)
                          || Deform::isHandled(l.pointDeformer, pose.handles) || (!l.chainScale.plain() && l.bonesAt(frame).size() >= 2));
    if (!bent)
        return false;
    Drawing made = Deform::shown(l.drawings[index], l, pose, index);
    // (What made it a drawing of its own: its name, its pin, its chain.)
    made.name = l.drawings[index].name;
    made.pinned = l.drawings[index].pinned;
    made.rig = l.drawings[index].rig;
    made.editIdentity = l.drawings[index].editIdentity;
    apply(tr("Apply Deformation"), [&](Scene& s) {
        Layer& edited = s.layers[layer];
        edited.drawings[index] = made;
        Pose rest = edited.poseAt(frame);
        rest.bend.fill(0.0);
        rest.warp.clear();
        rest.handles.clear();
        edited.setKey(frame, rest);
    });
    return true;
}

int Document::pasteDeformationOnNext(int layer, int frame, Pose const& pose, int count, double easeIn, double easeOut)
{
    if (layer < 0 || layer >= m_scene.layers.size() || count < 1)
        return 0;
    Layer const& l = m_scene.layers[layer];
    // The next drawings: each change of drawing after this frame, once each.
    QVector<int> drawings;
    int shown = l.drawingAt(frame);
    for (int f = frame + 1; f < l.cells.size() && drawings.size() < count; ++f) {
        int const index = l.cells[f];
        if (index == shown || index == Layer::kNoDrawing || index >= l.drawings.size())
            continue;
        shown = index;
        if (!drawings.contains(index))
            drawings.push_back(index);
    }
    if (drawings.isEmpty())
        return 0;
    QVector<Drawing> made;
    for (int k = 0; k < drawings.size(); ++k) {
        Pose part = pose;
        part.points.clear();
        part.deform = easedProgress(double(k + 1) / drawings.size(), std::clamp(easeOut, 0.0, 1.0), std::clamp(easeIn, 0.0, 1.0));
        Drawing d = Deform::shown(l.drawings[drawings[k]], l, part, drawings[k]);
        d.name = l.drawings[drawings[k]].name;
        d.pinned = l.drawings[drawings[k]].pinned;
        d.rig = l.drawings[drawings[k]].rig;
        d.editIdentity = l.drawings[drawings[k]].editIdentity;
        made.push_back(d);
    }
    apply(tr("Paste on Next Drawings"), [&](Scene& s) {
        for (int k = 0; k < drawings.size(); ++k)
            s.layers[layer].drawings[drawings[k]] = made[k];
    });
    return int(drawings.size());
}

bool Document::transformWarpPose(int layer, int frame, double degrees, QPointF by)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].hasWarp())
        return false;
    Layer const& l = m_scene.layers[layer];
    Pose pose = l.poseAt(frame);
    QVector<QPointF> const posed = Deform::posedWarp(l.warp, pose.warp);
    // About the middle of its points as posed: each point turned and moved, its handles turned with it.
    QPointF centre;
    int const n = int(posed.size()) / 3;
    for (int i = 0; i < n; ++i)
        centre += posed[i * 3];
    centre /= std::max(1, n);
    QTransform turn;
    turn.rotate(degrees);
    QVector<QPointF> moved(l.warp.size());
    for (int i = 0; i < n; ++i) {
        QPointF const at = turn.map(posed[i * 3] - centre) + centre + by;
        moved[i * 3] = at - l.warp[i * 3];
        for (int h : {1, 2})
            moved[i * 3 + h] = turn.map(posed[i * 3 + h]) - l.warp[i * 3 + h];
    }
    pose.warp = moved;
    setKeyframe(layer, frame, pose, degrees != 0.0 ? tr("Changed Deformation Path Rotation") : tr("Changed Deformation Path Translation"));
    return true;
}
