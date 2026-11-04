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
