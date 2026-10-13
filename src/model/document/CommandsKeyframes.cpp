// Eclipse - the document: keyframes and what they hold, morphings, pivots.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"

void Document::setKeyframe(int layer, int frame, Pose const& pose, QString const& text)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    int const index = l.keyIndex(frame);
    if (index >= 0 && l.keys[index].pose == pose)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].setKey(frame, pose); });
}

void Document::insertKeyframe(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].keyIndex(frame) >= 0)
        return;
    Pose const pose = m_scene.layers[layer].poseAt(frame);
    apply(tr("Insert Keyframe"), [&](Scene& s) { s.layers[layer].setKey(frame, pose); });
}

void Document::removeKeyframe(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const index = m_scene.layers[layer].keyIndex(frame);
    if (index >= 0)
        apply(tr("Delete Keyframe"), [&](Scene& s) { s.layers[layer].keys.removeAt(index); });
}

bool Document::createMorph(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || !m_scene.layers[layer].holdsDrawings())
        return false;
    Layer const& now = m_scene.layers[layer];
    int const from = now.exposureStart(frame);
    if (from < 0 || now.morphIndexAt(from) >= 0)
        return false;
    int const to = now.nextDrawingFrame(from);
    if (to <= from + 1 || now.drawingAt(to) == Layer::kNoDrawing)
        return false;       // no second drawing, or no frame between the two
    apply(tr("Create Morphing"), [&](Scene& s) {
        QVector<Morph>& morphs = s.layers[layer].morphs;
        // One that was left behind on this frame (its drawings gone since) gives way.
        morphs.erase(std::remove_if(morphs.begin(), morphs.end(), [&](Morph const& m) { return m.from == from; }), morphs.end());
        Morph morph;
        morph.from = from;
        morphs.push_back(morph);
        std::sort(morphs.begin(), morphs.end(), [](Morph const& a, Morph const& b) { return a.from < b.from; });
    });
    return true;
}

bool Document::deleteMorph(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    int const index = m_scene.layers[layer].morphIndexAt(frame);
    if (index < 0)
        return false;
    apply(tr("Delete Morphing"), [&](Scene& s) { s.layers[layer].morphs.removeAt(index); });
    return true;
}

bool Document::setMorphEase(int layer, int frame, double easeIn, double easeOut)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    int const index = m_scene.layers[layer].morphIndexAt(frame);
    if (index < 0)
        return false;
    easeIn = std::clamp(easeIn, 0.0, 1.0);
    easeOut = std::clamp(easeOut, 0.0, 1.0);
    Morph const& now = m_scene.layers[layer].morphs[index];
    if (now.easeIn == easeIn && now.easeOut == easeOut)
        return false;
    apply(tr("Morphing Ease"), [&](Scene& s) {
        s.layers[layer].morphs[index].easeIn = easeIn;
        s.layers[layer].morphs[index].easeOut = easeOut;
    });
    return true;
}

bool Document::convertMorph(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    Layer const& now = m_scene.layers[layer];
    int const index = now.morphIndexAt(frame);
    if (index < 0)
        return false;
    int const from = now.morphs[index].from, to = now.morphEnd(index);
    QVector<Drawing> between;
    for (int f = from + 1; f < to; ++f)
        between.push_back(now.shownAt(f));
    apply(tr("Convert Morphing to Drawings"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.morphs.removeAt(index);
        for (int i = 0; i < between.size(); ++i) {
            l.drawings.push_back(between[i]);
            l.cells[from + 1 + i] = int(l.drawings.size()) - 1;
        }
        l.compact();
    });
    return true;
}

bool Document::moveKeyframe(int layer, int from, int to)
{
    if (!validCell(m_scene, layer, from) || !validCell(m_scene, layer, to) || from == to)
        return false;
    int const index = m_scene.layers[layer].keyIndex(from);
    if (index < 0 || m_scene.layers[layer].keyIndex(to) >= 0)
        return false;
    apply(tr("Move Keyframe"), [&](Scene& s) {
        QVector<Keyframe>& keys = s.layers[layer].keys;
        Keyframe key = keys.takeAt(index);
        key.frame = to;
        int at = 0;
        while (at < keys.size() && keys[at].frame < to)
            ++at;
        keys.insert(at, key);
    });
    return true;
}

void Document::setKeyTween(int layer, int frame, bool tween)
{
    if (!validCell(m_scene, layer, frame))
        return;
    QVector<Keyframe> const& keys = m_scene.layers[layer].keys;
    int index = -1;
    for (int i = 0; i < keys.size() && keys[i].frame <= frame; ++i)
        index = i;
    if (index < 0 || keys[index].tween == tween)
        return;
    apply(tween ? tr("Set Motion Keyframe") : tr("Set Stop-Motion Keyframe"),
          [&](Scene& s) { s.layers[layer].keys[index].tween = tween; });
}

void Document::clearKeyframes(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].keys.isEmpty())
        return;
    apply(tr("Reset Transformation"), [&](Scene& s) { s.layers[layer].keys.clear(); });
}

void Document::setKeyEase(int layer, int frame, double easeIn, double easeOut)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const index = m_scene.layers[layer].keyIndex(frame);
    easeIn = std::clamp(easeIn, 0.0, 1.0);
    easeOut = std::clamp(easeOut, 0.0, 1.0);
    if (index < 0 || (m_scene.layers[layer].keys[index].easeIn == easeIn && m_scene.layers[layer].keys[index].easeOut == easeOut))
        return;
    apply(tr("Set Ease"), [&](Scene& s) {
        s.layers[layer].keys[index].easeIn = easeIn;
        s.layers[layer].keys[index].easeOut = easeOut;
    });
}

void Document::setEffectAmount(int layer, int frame, int index, double amount)
{
    if (!validCell(m_scene, layer, frame) || index < 0 || index >= m_scene.layers[layer].effects.size())
        return;
    Layer const& l = m_scene.layers[layer];
    Pose pose = l.poseAt(frame);
    // A keyframe carries the amounts of all the effects up to this one, as they are now.
    QVector<double> fx;
    for (int i = 0; i < std::max(int(pose.fx.size()), index + 1); ++i)
        fx.push_back(i < pose.fx.size() ? pose.fx[i] : l.effects[i].amount);
    fx[index] = std::max(0.0, amount);
    pose.fx = fx;
    setKeyframe(layer, frame, pose, tr("Change Effect"));
}

void Document::setFov(int layer, double degrees, int frame)
{
    degrees = std::clamp(degrees, 1.0, 170.0);
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Camera || m_scene.layers[layer].fovAt(frame) == degrees)
        return;
    apply(tr("Change Field of View"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        if (l.keys.isEmpty()) {
            l.fov = degrees;
            return;
        }
        // The keyframes that did not say are given the angle they stood for, so that the
        // angle glides from the one keyframe to the next.
        Pose pose = l.poseAt(frame);
        for (Keyframe& k : l.keys)
            if (k.pose.fov <= 0.0)
                k.pose.fov = l.fov;
        pose.fov = degrees;
        l.setKey(frame, pose);
    });
}

void Document::setPivot(int layer, QPointF pivot)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].pivot == pivot)
        return;
    apply(tr("Move Pivot"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        // Each keyframe keeps the artwork where it was: its offset absorbs the difference
        // between turning about the old pivot and about the new one.
        for (Keyframe& k : l.keys) {
            QTransform const before = Layer::matrix(k.pose, l.pivot);
            QTransform const after = Layer::matrix(k.pose, pivot);
            QPointF const shift = before.map(QPointF(0, 0)) - after.map(QPointF(0, 0));
            k.pose.x += shift.x();
            k.pose.y += shift.y();
        }
        l.pivot = pivot;
    });
}

void Document::setQuad(int layer, QVector<QPointF> const& corners, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size() || corners.size() != 4)
        return;
    Layer const& now = m_scene.layers[layer];
    if (now.quad.size() != 4 || now.quadAt(now.poseAt(frame)) == corners)
        return;
    apply(tr("Move Quadmap Corner"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        if (l.keys.isEmpty()) {
            l.quad = corners;
            return;
        }
        Pose pose = l.poseAt(frame);
        pose.quad.resize(4);
        for (int i = 0; i < 4; ++i)
            pose.quad[i] = corners[i] - l.quad[i];
        l.setKey(frame, pose);
    });
}

void Document::shiftPoses(int layer, int frame, Pose const& from, Pose const& to, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || from == to)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        if (l.keys.isEmpty()) {
            l.setKey(frame, to);    // one keyframe: the same pose on every frame
            return;
        }
        auto ratio = [](double was, double is) { return std::abs(was) > 1e-9 ? is / was : 1.0; };
        for (Keyframe& k : l.keys) {
            Pose& p = k.pose;
            p.x += to.x - from.x;
            p.y += to.y - from.y;
            p.z += to.z - from.z;
            p.rotation += to.rotation - from.rotation;
            p.skew += to.skew - from.skew;
            p.rotX += to.rotX - from.rotX;
            p.rotY += to.rotY - from.rotY;
            p.scaleX *= ratio(from.scaleX, to.scaleX);
            p.scaleY *= ratio(from.scaleY, to.scaleY);
            p.opacity = std::clamp(p.opacity + to.opacity - from.opacity, 0.0, 1.0);
            for (int i = 0; i < to.bend.size(); ++i) {
                if (p.bend.size() <= i)
                    p.bend.resize(i + 1);
                p.bend[i] += to.bend[i] - (i < from.bend.size() ? from.bend[i] : 0.0);
            }
            for (int i = 0; i < to.warp.size(); ++i) {
                if (p.warp.size() <= i)
                    p.warp.resize(i + 1);
                p.warp[i] += to.warp[i] - (i < from.warp.size() ? from.warp[i] : QPointF());
            }
        }
    });
}
