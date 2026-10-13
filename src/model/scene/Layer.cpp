// Eclipse - the scene: poses and keyframes, and a layer - its exposure sheet, morphings, matrices.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---- Pose ---------------------------------------------------------------------------------

Pose Pose::lerp(Pose const& a, Pose const& b, double t)
{
    Pose p;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.scaleX = a.scaleX + (b.scaleX - a.scaleX) * t;
    p.scaleY = a.scaleY + (b.scaleY - a.scaleY) * t;
    // Angles are not wrapped: a keyframe at 720 degrees means two full turns.
    p.rotation = a.rotation + (b.rotation - a.rotation) * t;
    p.skew = a.skew + (b.skew - a.skew) * t;
    p.z = a.z + (b.z - a.z) * t;
    p.rotX = a.rotX + (b.rotX - a.rotX) * t;
    p.rotY = a.rotY + (b.rotY - a.rotY) * t;
    // An angle only one of the two keyframes gives is held.
    p.fov = a.fov > 0.0 && b.fov > 0.0 ? a.fov + (b.fov - a.fov) * t : std::max(a.fov, b.fov);
    p.opacity = a.opacity + (b.opacity - a.opacity) * t;
    // An amount only one of the two keyframes has is held, not faded from nothing.
    for (int i = 0; i < std::max(a.fx.size(), b.fx.size()); ++i) {
        double const from = i < a.fx.size() ? a.fx[i] : b.fx[i];
        double const to = i < b.fx.size() ? b.fx[i] : a.fx[i];
        p.fx.push_back(from + (to - from) * t);
    }
    for (int i = 0; i < std::max(a.bend.size(), b.bend.size()); ++i) {
        double const from = i < a.bend.size() ? a.bend[i] : 0.0;
        double const to = i < b.bend.size() ? b.bend[i] : 0.0;
        p.bend.push_back(from + (to - from) * t);
    }
    for (int i = 0; i < std::max(a.quad.size(), b.quad.size()); ++i) {
        QPointF const from = i < a.quad.size() ? a.quad[i] : QPointF();
        QPointF const to = i < b.quad.size() ? b.quad[i] : QPointF();
        p.quad.push_back(from + (to - from) * t);
    }
    for (int i = 0; i < std::max(a.warp.size(), b.warp.size()); ++i) {
        QPointF const from = i < a.warp.size() ? a.warp[i] : QPointF();
        QPointF const to = i < b.warp.size() ? b.warp[i] : QPointF();
        p.warp.push_back(from + (to - from) * t);
    }
    return p;
}

double easedProgress(double t, double easeOut, double easeIn)
{
    easeOut = std::clamp(easeOut, 0.0, 1.0);
    easeIn = std::clamp(easeIn, 0.0, 1.0);
    if (easeOut <= 0.0 && easeIn <= 0.0)
        return t;
    // A cubic curve from (0,0) to (1,1) in (time, progress).  Its handles lie flat along
    // the time axis: the longer a handle, the longer the motion stays slow at that end.
    double const x1 = easeOut * 0.8, x2 = 1.0 - easeIn * 0.8;
    auto bezier = [](double s, double a, double b) {
        double const u = 1.0 - s;
        return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
    };
    // Find the curve parameter whose time is t (time grows steadily along the curve).
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        double const mid = (lo + hi) / 2.0;
        if (bezier(mid, x1, x2) < t)
            lo = mid;
        else
            hi = mid;
    }
    double const s = (lo + hi) / 2.0;
    // Progress: flat handles at height 0 and 1 where there is ease, on the diagonal where not.
    double const y1 = easeOut > 0.0 ? 0.0 : x1, y2 = easeIn > 0.0 ? 1.0 : x2;
    return bezier(s, y1, y2);
}

// ---- Layer --------------------------------------------------------------------------------

double Layer::effectAmount(int index, Pose const& pose) const
{
    if (index < 0 || index >= effects.size())
        return 0.0;
    return effects[index].animated && index < pose.fx.size() ? pose.fx[index] : effects[index].amount;
}

Pose Layer::poseAt(int frame) const
{
    if (keys.isEmpty())
        return Pose();
    if (frame <= keys.first().frame)
        return keys.first().pose;
    if (frame >= keys.last().frame)
        return keys.last().pose;
    int i = 0;
    while (keys[i + 1].frame <= frame)
        ++i;
    Keyframe const& a = keys[i];
    Keyframe const& b = keys[i + 1];
    if (!a.tween || frame == a.frame)
        return a.pose;
    double const t = double(frame - a.frame) / double(b.frame - a.frame);
    return Pose::lerp(a.pose, b.pose, easedProgress(t, a.easeOut, b.easeIn));
}

int Layer::keyIndex(int frame) const
{
    for (int i = 0; i < keys.size(); ++i)
        if (keys[i].frame == frame)
            return i;
    return -1;
}

int Layer::previousKeyFrame(int frame) const
{
    int found = -1;
    for (Keyframe const& k : keys)
        if (k.frame < frame)
            found = k.frame;
    return found;
}

int Layer::nextKeyFrame(int frame) const
{
    for (Keyframe const& k : keys)
        if (k.frame > frame)
            return k.frame;
    return -1;
}

void Layer::setKey(int frame, Pose const& pose)
{
    int i = 0;
    while (i < keys.size() && keys[i].frame < frame)
        ++i;
    if (i < keys.size() && keys[i].frame == frame) {
        keys[i].pose = pose;
        return;
    }
    Keyframe k;
    k.frame = frame;
    k.pose = pose;
    // A new keyframe continues what the one before it was doing.
    k.tween = i > 0 ? keys[i - 1].tween : true;
    keys.insert(i, k);
}

QTransform Layer::matrix(Pose const& pose, QPointF pivot)
{
    // Read from the bottom: a point is brought to the pivot, scaled, turned, and put back
    // where the pivot is, moved by the offset.
    QTransform m;
    m.translate(pivot.x() + pose.x, pivot.y() + pose.y);
    m.rotate(pose.rotation);
    if (pose.skew != 0.0)
        m.shear(-std::tan(std::clamp(pose.skew, -89.0, 89.0) * 3.14159265358979323846 / 180.0), 0.0);     // (down the picture is up the numbers)
    m.scale(pose.scaleX, pose.scaleY);
    m.translate(-pivot.x(), -pivot.y());
    return m;
}

QTransform Layer::quadWarp(QSizeF size, QVector<QPointF> const& quad)
{
    QTransform warp;
    if (quad.size() != 4 || size.isEmpty())
        return warp;
    QPolygonF const from({QPointF(0, 0), QPointF(size.width(), 0), QPointF(size.width(), size.height()), QPointF(0, size.height())});
    // Corners that cannot be reached (three in a line) leave it flat: a pull that cannot be
    // undone would lose the pointer, which is found by going back through it.
    if (!QTransform::quadToQuad(from, QPolygonF(quad), warp) || !warp.isInvertible())
        return QTransform();
    return warp;
}

QVector<QPointF> Layer::quadAt(Pose const& pose) const
{
    QVector<QPointF> corners = quad;
    for (int i = 0; i < corners.size() && i < pose.quad.size(); ++i)
        corners[i] += pose.quad[i];
    return corners;
}

QTransform Layer::matrixFor(Pose const& pose) const
{
    if (kind == Group || kind == ColourCard)
        return QTransform();
    if (kind == Quadmap)
        return quadWarp(quadSize, quadAt(pose)) * matrix(pose, pivot);
    return matrix(pose, pivot);
}

bool Layer::isKey(int frame) const
{
    int const d = drawingAt(frame);
    return d != kNoDrawing && (frame == 0 || drawingAt(frame - 1) != d);
}

int Layer::morphEnd(int index) const
{
    if (index < 0 || index >= morphs.size())
        return -1;
    int const from = morphs[index].from;
    if (drawingAt(from) == kNoDrawing || exposureStart(from) != from)
        return -1;      // its first drawing does not start there any more
    int const next = nextDrawingFrame(from);
    return next > from && drawingAt(next) != kNoDrawing ? next : -1;
}

int Layer::morphIndexAt(int frame) const
{
    int const start = exposureStart(frame);
    if (start < 0)
        return -1;
    for (int i = 0; i < morphs.size(); ++i)
        if (morphs[i].from == start && morphEnd(i) > frame)
            return i;
    return -1;
}

bool Layer::morphing(int frame) const
{
    int const index = morphIndexAt(frame);
    return index >= 0 && frame > morphs[index].from;
}

Drawing Layer::shownAt(int frame) const
{
    int const drawing = drawingAt(frame);
    if (drawing == kNoDrawing || drawing >= drawings.size())
        return Drawing();
    int const index = morphIndexAt(frame);
    if (index < 0 || frame <= morphs[index].from)
        return drawings[drawing];
    int const from = morphs[index].from, to = morphEnd(index);
    int const other = drawingAt(to);
    if (other < 0 || other >= drawings.size())
        return drawings[drawing];
    double const t = easedProgress(double(frame - from) / double(to - from), morphs[index].easeOut, morphs[index].easeIn);
    return Morphing::between(drawings[drawing], drawings[other], t);
}

int Layer::exposureStart(int frame) const
{
    int const d = drawingAt(frame);
    if (d == kNoDrawing)
        return -1;
    while (frame > 0 && cells[frame - 1] == d)
        --frame;
    return frame;
}

int Layer::previousDrawingFrame(int frame) const
{
    int const start = exposureStart(frame);
    int f = (start >= 0 ? start : frame) - 1;
    while (f >= 0 && cells[f] == kNoDrawing)
        --f;
    return f >= 0 ? exposureStart(f) : -1;
}

int Layer::nextDrawingFrame(int frame) const
{
    int const d = drawingAt(frame);
    int f = frame + 1;
    while (f < cells.size() && (cells[f] == kNoDrawing || cells[f] == d))
        ++f;
    return f < cells.size() ? f : -1;
}

void Layer::substitute(int frame, int drawing)
{
    if (frame < 0 || frame >= cells.size() || drawing < 0 || drawing >= drawings.size())
        return;
    int const old = cells[frame];
    int end = frame + 1;
    if (old != kNoDrawing) {
        while (end < cells.size() && cells[end] == old)
            ++end;
        drawings[old].pinned = true;
    }
    drawings[drawing].pinned = true;
    for (int f = frame; f < end; ++f)
        cells[f] = drawing;
}

// Two drawings of a layer cannot go by one name (pasted cells bring theirs along): the later one loses it.
static void oneNameEach(QVector<Drawing>& drawings)
{
    for (int i = 1; i < drawings.size(); ++i) {
        if (drawings[i].name.isEmpty())
            continue;
        for (int k = 0; k < i; ++k) {
            if (drawings[k].name == drawings[i].name) {
                drawings[i].name.clear();
                break;
            }
        }
    }
}

void Layer::compact()
{
    QVector<int> remap(drawings.size(), kNoDrawing);
    QVector<Drawing> kept;
    bool bank = false;
    for (Drawing const& d : drawings)
        bank = bank || d.pinned;
    if (bank) {
        // A layer with a bank of drawings keeps them in the order they were made, so that
        // "next drawing" means the same thing whatever the frames show.
        QVector<bool> used(drawings.size(), false);
        for (int cell : cells)
            if (cell != kNoDrawing)
                used[cell] = true;
        for (int i = 0; i < drawings.size(); ++i) {
            if (used[i] || drawings[i].pinned) {
                remap[i] = int(kept.size());
                kept.push_back(drawings[i]);
            }
        }
        for (int& cell : cells)
            if (cell != kNoDrawing)
                cell = remap[cell];
        drawings = kept;
        oneNameEach(drawings);
        return;
    }
    for (int& cell : cells) {
        if (cell == kNoDrawing)
            continue;
        if (remap[cell] == kNoDrawing) {
            remap[cell] = int(kept.size());
            kept.push_back(drawings[cell]);
        }
        cell = remap[cell];
    }
    drawings = kept;
    oneNameEach(drawings);
}
