// Eclipse - inverse kinematics over the layers: a part pulled, the layers it hangs from turned (and the top one moved) to follow.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace {

double screenAngle(QTransform const& t)
{
    return std::atan2(t.m12(), t.m11()) * 180.0 / 3.14159265358979323846;
}

}  // namespace

Scene::IkOptions Scene::s_ik;

void Scene::setIkOptions(IkOptions const& options)
{
    s_ik = options;
}

Scene::IkOptions Scene::ikOptions()
{
    return s_ik;
}

QVector<int> Scene::ikChain(int grabbed) const
{
    QVector<int> chain;
    if (grabbed < 0 || grabbed >= layers.size() || layers[grabbed].ikNail || layers[grabbed].kind == Layer::Camera)
        return chain;
    chain.push_back(grabbed);
    for (int guard = 0, l = parentIndex(grabbed); l >= 0 && guard < 64; ++guard, l = parentIndex(l)) {
        Layer const& up = layers[l];
        if (up.ikNail || up.kind == Layer::Camera || up.kind == Layer::Group)
            break;          // nailed (it and all above stay), or not a part that turns
        if (s_ik.stopAtIntersection && !s_ik.allChains) {
            // (The first layer that more than one chain hangs from - a body the arms and legs share - is where the chain stops.)
            int children = 0;
            for (Layer const& other : layers)
                children += other.parent == up.id ? 1 : 0;
            if (children > 1)
                break;
        }
        chain.push_back(l);
    }
    return chain;
}

bool Scene::solveIK(int grabbed, QPointF local, QPointF target, int frame, QVector<int>* touched)
{
    QVector<int> const chain = ikChain(grabbed);
    if (chain.isEmpty())
        return false;
    auto tip = [&] { return worldTransform(grabbed, frame).map(local); };
    // The parts holding their orientation below the top of the chain: the angle each has on screen, to keep.
    QVector<QPair<int, double>> held;
    int const top = chain.last();
    for (int l = 0; l < layers.size(); ++l) {
        if (!layers[l].ikHoldAngle)
            continue;
        bool below = l == top;
        for (int up = parentIndex(l), guard = 0; up >= 0 && guard < 64 && !below; up = parentIndex(up), ++guard)
            below = up == top;
        if (below)
            held.push_back({l, screenAngle(worldTransform(l, frame))});
    }
    QVector<int> changed;
    auto keyed = [&](int l, Pose const& pose) {
        layers[l].setKey(frame, pose);
        if (!changed.contains(l))
            changed.push_back(l);
    };
    // Turning each part, the nearest first, round and round until the tip is near enough; then, when the top may move, it
    // moves the rest of the way (as far as its holds allow).
    auto reach = [&](QPointF aim) {
        for (int round = 0; round < s_ik.iterations && QLineF(tip(), aim).length() > s_ik.error; ++round) {
            for (int l : chain) {
                Layer& part = layers[l];
                if (part.ikExcluded || part.ikHoldAngle || !part.ikCanRotate)
                    continue;
                // The point it turns about, on screen: found from how a little more turn moves it (a layer that has moved
                // turns about where its pivot has gone).
                QPointF base = worldTransform(l, frame).map(pivotOf(l, frame));
                {
                    Pose const now = part.poseAt(frame);
                    Pose nudged = now;
                    nudged.rotation += 1.0;
                    QTransform const w0 = worldTransform(l, frame);
                    part.setKey(frame, nudged);
                    QTransform const w1 = worldTransform(l, frame);
                    part.setKey(frame, now);
                    if (w0.isInvertible()) {
                        QTransform const m = w0.inverted() * w1;
                        double const a11 = 1.0 - m.m11(), a12 = -m.m21(), a21 = -m.m12(), a22 = 1.0 - m.m22();
                        double const det = a11 * a22 - a12 * a21;
                        if (std::abs(det) > 1e-12)
                            base = QPointF((m.dx() * a22 - a12 * m.dy()) / det, (a11 * m.dy() - a21 * m.dx()) / det);
                    }
                }
                QPointF const end = tip();
                double turn = (std::atan2(aim.y() - base.y(), aim.x() - base.x()) - std::atan2(end.y() - base.y(), end.x() - base.x())) * 180.0
                              / 3.14159265358979323846;
                turn = std::remainder(turn, 360.0) * (1.0 - std::clamp(part.ikStiffness, 0.0, 1.0));
                if (std::abs(turn) < 1e-4)
                    continue;
                Pose const was = part.poseAt(frame);
                Pose keep = was;
                double best = QLineF(end, aim).length();
                for (double const way : {1.0, -1.0}) {
                    Pose tried = was;
                    tried.rotation = was.rotation + way * turn;
                    if (part.ikLimited) {
                        // Past its limits only as far as the limit weight lets it (0: not at all).
                        double const low = std::min(part.ikMin, part.ikMax), high = std::max(part.ikMin, part.ikMax);
                        double const clamped = std::clamp(tried.rotation, low, high);
                        tried.rotation = clamped + (tried.rotation - clamped) * std::clamp(s_ik.limitWeight, 0.0, 1.0);
                    }
                    part.setKey(frame, tried);
                    double const off = QLineF(tip(), aim).length();
                    if (off < best - 1e-9) {
                        best = off;
                        keep = tried;
                    }
                }
                keyed(l, keep);
            }
        }
        // Where turning each part in turn stalls (an arm nearly straight): all of them turned together, each as much as moving
        // the tip that way helps (damped least squares over the turns).
        for (int round = 0; round < s_ik.iterations * 4 && QLineF(tip(), aim).length() > s_ik.error; ++round) {
            QVector<int> free;
            for (int l : chain)
                if (!layers[l].ikExcluded && !layers[l].ikHoldAngle && layers[l].ikCanRotate && layers[l].ikStiffness < 1.0)
                    free.push_back(l);
            if (free.isEmpty())
                break;
            QPointF const at = tip(), error = aim - at;
            QVector<QPointF> column(free.size());
            for (int k = 0; k < free.size(); ++k) {
                Layer& part = layers[free[k]];
                Pose const was = part.poseAt(frame);
                Pose nudged = was;
                nudged.rotation += 0.01;
                part.setKey(frame, nudged);
                column[k] = (tip() - at) / 0.01;
                part.setKey(frame, was);
            }
            // (J J^T + damping) solved for the tip's error, then each turn is its column's share of that.
            double const damping = 1e-3 + QLineF(QPointF(), error).length() * 1e-4;
            double a11 = damping, a12 = 0.0, a22 = damping;
            for (QPointF const& c : std::as_const(column)) {
                a11 += c.x() * c.x();
                a12 += c.x() * c.y();
                a22 += c.y() * c.y();
            }
            double const det = a11 * a22 - a12 * a12;
            if (std::abs(det) < 1e-18)
                break;
            QPointF const solved((a22 * error.x() - a12 * error.y()) / det, (a11 * error.y() - a12 * error.x()) / det);
            double const before = QLineF(at, aim).length();
            QVector<Pose> previous;
            for (int k = 0; k < free.size(); ++k) {
                Layer& part = layers[free[k]];
                Pose const was = part.poseAt(frame);
                previous.push_back(was);
                Pose turned = was;
                double step = (column[k].x() * solved.x() + column[k].y() * solved.y()) * (1.0 - std::clamp(part.ikStiffness, 0.0, 1.0));
                step = std::clamp(step, -20.0, 20.0);
                turned.rotation += step;
                if (part.ikLimited) {
                    double const low = std::min(part.ikMin, part.ikMax), high = std::max(part.ikMin, part.ikMax);
                    double const clamped = std::clamp(turned.rotation, low, high);
                    turned.rotation = clamped + (turned.rotation - clamped) * std::clamp(s_ik.limitWeight, 0.0, 1.0);
                }
                part.setKey(frame, turned);
            }
            if (QLineF(tip(), aim).length() >= before - 1e-9) {
                // No nearer: taken back, and no further to go this way.
                for (int k = 0; k < free.size(); ++k)
                    layers[free[k]].setKey(frame, previous[k]);
                break;
            }
            for (int l : std::as_const(free))
                if (!changed.contains(l))
                    changed.push_back(l);
        }
        Layer& head = layers[top];
        if (head.ikTranslateTop && QLineF(tip(), aim).length() > s_ik.error) {
            int const parent = parentIndex(top);
            QTransform const above = parent >= 0 ? worldTransform(parent, frame) : QTransform();
            if (above.isInvertible()) {
                QPointF const by = above.inverted().map(aim) - above.inverted().map(tip());
                Pose moved = head.poseAt(frame);
                if (!head.ikHoldX)
                    moved.x += by.x();
                if (!head.ikHoldY)
                    moved.y += by.y();
                keyed(top, moved);
            }
        }
        return QLineF(tip(), aim).length() <= s_ik.error;
    };
    bool reached = reach(target);
    // Out of reach (or not got there in the iterations): tried again from where it is, aiming a little nearer each time; of
    // all the tries, the one that brought the tip nearest the point is kept.
    if (!reached && s_ik.retryIncrement > 0.0) {
        QPointF const from = worldTransform(top, frame).map(pivotOf(top, frame));
        QVector<Layer> best = layers;
        double bestGap = QLineF(tip(), target).length();
        for (double share = 1.0; share > 0.0 && !reached; share -= s_ik.retryIncrement) {
            bool const there = reach(from + (target - from) * share);
            double const now = QLineF(tip(), target).length();
            if (now < bestGap - 1e-9) {
                bestGap = now;
                best = layers;
            }
            reached = now <= s_ik.error;
            if (there && share < 1.0)
                break;      // (as near as it can come along that line)
        }
        layers = best;
        reached = bestGap <= s_ik.error;
    }
    // Those holding their orientation turned back to the angle they had on screen (the higher ones first).
    for (auto const& h : std::as_const(held)) {
        int const l = h.first;
        double const turn = std::remainder(h.second - screenAngle(worldTransform(l, frame)), 360.0);
        if (std::abs(turn) < 1e-6)
            continue;
        Pose const pose = layers[l].poseAt(frame);
        Pose tried = pose;
        tried.rotation += turn;
        layers[l].setKey(frame, tried);
        if (std::abs(std::remainder(h.second - screenAngle(worldTransform(l, frame)), 360.0)) > 1e-3) {
            tried.rotation = pose.rotation - turn;
            layers[l].setKey(frame, tried);
        }
        if (!changed.contains(l))
            changed.push_back(l);
    }
    if (touched)
        *touched = changed;
    return reached;
}

int Scene::applyIkConstraints(int from, int to, QVector<int>* touched)
{
    // Apply IK Constraints: each part held in place (nailed, or holding X or Y) kept where it is on frame `from` over the frames
    // after it, the parts above it turned on each frame to keep it there.
    int fixed = 0;
    QVector<int> changed;
    // (Not done while the pointer waits: it can go round more often, and come nearer.)
    IkOptions const kept = s_ik;
    s_ik.iterations = std::max(s_ik.iterations, 400);
    s_ik.error = std::min(s_ik.error, 0.05);
    for (int l = 0; l < layers.size(); ++l) {
        Layer const& part = layers[l];
        if (!(part.ikNail || part.ikHoldX || part.ikHoldY) || parentIndex(l) < 0)
            continue;
        QPointF const local = pivotOf(l, from);
        QPointF const anchor = worldTransform(l, from).map(local);
        // The chain that moves it is the one above it: what it hangs from.
        int const parent = parentIndex(l);
        bool const wasNailed = layers[l].ikNail;
        layers[l].ikNail = false;
        for (int f = from + 1; f <= to && f < m_frameCount; ++f) {
            QPointF aim = anchor;
            QPointF const now = worldTransform(l, f).map(local);
            if (!wasNailed) {
                // Only the held direction is kept.
                aim = QPointF(part.ikHoldX ? anchor.x() : now.x(), part.ikHoldY ? anchor.y() : now.y());
            }
            QVector<int> those;
            QPointF const inParent = parent >= 0 && worldTransform(parent, f).isInvertible() ? worldTransform(parent, f).inverted().map(now) : now;
            solveIK(parent, inParent, aim, f, &those);
            for (int t : those)
                if (!changed.contains(t))
                    changed.push_back(t);
        }
        layers[l].ikNail = wasNailed;
        ++fixed;
    }
    s_ik = kept;
    if (touched)
        *touched = changed;
    return fixed;
}
