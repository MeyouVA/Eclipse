// Eclipse - the scene: poses and keyframes, and a layer - its exposure sheet, morphings, matrices.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---- Pose ---------------------------------------------------------------------------------

// Where something is after `t` that starts at `x` going `v` and gathering `a` each frame, between two walls it bounces off
// (keeping `keep` of its speed each time).
static double between(double x, double v, double a, double lo, double hi, double t, double keep)
{
    for (int hits = 0; hits < 64; ++hits) {
        // The soonest of the two walls it reaches: x + v s + a s^2 / 2 = wall.
        double hit = -1.0;
        for (double const wall : {lo, hi}) {
            double const qa = 0.5 * a, qb = v, qc = x - wall;
            QVector<double> roots;
            if (std::abs(qa) < 1e-12) {
                if (std::abs(qb) > 1e-12)
                    roots << -qc / qb;
            } else if (double const d = qb * qb - 4.0 * qa * qc; d >= 0.0) {
                roots << (-qb - std::sqrt(d)) / (2.0 * qa) << (-qb + std::sqrt(d)) / (2.0 * qa);
            }
            for (double s : roots)
                if (s > 1e-9 && (hit < 0.0 || s < hit))
                    hit = s;
        }
        if (hit < 0.0 || hit > t)
            return std::clamp(x + v * t + 0.5 * a * t * t, lo, hi);
        x = x + v * hit + 0.5 * a * hit * hit;
        x = std::clamp(x, lo, hi);
        v = -(v + a * hit) * keep;
        t -= hit;
    }
    return std::clamp(x, lo, hi);
}

// Where something is after `age` frames that starts at `at` going `v` and gathering `a` each frame, when it bounces off
// `shape` (and the floor and walls, when there are some), keeping `keep` of its speed each time.  Taken a quarter of a
// frame at a time; on the way it goes exactly as it would with nothing in the way.
static QPointF throughShape(QPointF at, QPointF v, QPointF a, double age, double floor, double walls, double keep, QPainterPath const& shape)
{
    QRectF const box = shape.boundingRect();
    int const steps = int(std::ceil(age * 4.0));
    double const dt = steps > 0 ? age / steps : 0.0;
    for (int k = 0; k < steps; ++k) {
        QPointF next = at + v * dt + a * (0.5 * dt * dt);
        QPointF onward = v + a * dt;
        if (floor > 0.0 && next.y() > floor) {
            next.setY(floor);
            onward.setY(-onward.y() * keep);
        }
        if (walls > 0.0 && std::abs(next.x()) > walls) {
            next.setX(std::clamp(next.x(), -walls, walls));
            onward.setX(-onward.x() * keep);
        }
        if (box.contains(next) && shape.contains(next)) {
            // Which way the edge faces here: towards where, round about, it is outside the shape.
            double const reach = std::max(2.0, 1.5 * std::hypot(onward.x(), onward.y()) * dt);
            QPointF out;
            for (int d = 0; d < 16; ++d) {
                double const turn = d * 3.14159265358979323846 / 8.0;
                QPointF const dir(std::cos(turn), std::sin(turn));
                if (!shape.contains(next + dir * reach))
                    out += dir;
            }
            double length = std::hypot(out.x(), out.y());
            if (length < 1e-9) {
                out = -onward;      // deep in it: straight back
                length = std::hypot(out.x(), out.y());
            }
            if (length > 1e-9) {
                out /= length;
                double const into = onward.x() * out.x() + onward.y() * out.y();
                if (into < 0.0)
                    onward -= out * (2.0 * into);
            }
            onward *= keep;
            next = at;              // it stays where it was, outside
        }
        at = next;
        v = onward;
    }
    return at;
}

QVector<Effect::Particle> Effect::particles(double rate, int frame, QVector<QPointF> const& places, std::function<QPainterPath(int born)> const& shapeFor) const
{
    QVector<Particle> out;
    int const lasts = std::max(1, life);
    rate = std::clamp(rate, 0.0, 50.0);
    // A number between 0 and 1 that depends on nothing but what it is asked for.
    auto chance = [this](int born, int which, int what) {
        quint32 h = quint32(seed) * 2654435761u ^ quint32(born) * 2246822519u ^ quint32(which) * 3266489917u ^ quint32(what) * 668265263u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        h *= 3266489917u;
        h ^= h >> 16;
        return double(h & 0xffffff) / double(0x1000000);
    };
    for (int born = std::max(0, frame - lasts + 1); born <= frame && out.size() < 600; ++born) {
        // (A rate that is not a whole number: now one more, now one fewer.)
        int const count = int(std::floor(rate * (born + 1))) - int(std::floor(rate * born));
        double const age = frame - born;
        QPainterPath const shape = count > 0 && shapeFor ? shapeFor(born) : QPainterPath();
        for (int i = 0; i < count; ++i) {
            double const angle = (direction + (chance(born, i, 0) - 0.5) * spread) * 3.14159265358979323846 / 180.0;
            double const pace = speed * (0.6 + 0.8 * chance(born, i, 1));
            Particle p;
            QPointF from((chance(born, i, 3) - 0.5) * regionWidth, (chance(born, i, 4) - 0.5) * regionHeight);
            if (regionShape == 3) {
                from = places.isEmpty() ? QPointF() : places[std::min(int(places.size()) - 1, int(chance(born, i, 3) * places.size()))];
            } else if (regionShape != 0) {
                // Round: evenly over the inside of the ellipse, or along its edge.
                double const turn = chance(born, i, 3) * 2.0 * 3.14159265358979323846;
                double const reach = regionShape == 2 ? 1.0 : std::sqrt(chance(born, i, 4));
                from = QPointF(std::cos(turn) * reach * regionWidth / 2.0, std::sin(turn) * reach * regionHeight / 2.0);
            }
            double y = -std::sin(angle) * pace * age + 0.5 * gravity * age * age;
            if (floor > 0.0) {
                // Off the floor: from one bounce to the next, each time with what is kept of its speed, going up again.
                double y0 = from.y(), v = -std::sin(angle) * pace, left = age;
                y = from.y() + v * left + 0.5 * gravity * left * left - from.y();
                for (int hits = 0; hits < 64; ++hits) {
                    // When it next reaches the floor: y0 + v t + g t^2 / 2 = floor.
                    double hit = -1.0;
                    double const a = 0.5 * gravity, b = v, c = y0 - floor;
                    if (std::abs(a) < 1e-12) {
                        if (b > 1e-12)
                            hit = -c / b;
                    } else {
                        double const d = b * b - 4.0 * a * c;
                        if (d >= 0.0) {
                            double const r = std::sqrt(d);
                            double const t1 = (-b - r) / (2.0 * a), t2 = (-b + r) / (2.0 * a);
                            hit = t1 > 1e-9 ? t1 : t2 > 1e-9 ? t2 : -1.0;
                        }
                    }
                    if (hit < 0.0 || hit > left) {
                        y = y0 + v * left + 0.5 * gravity * left * left - from.y();
                        break;
                    }
                    left -= hit;
                    v = -(v + gravity * hit) * std::clamp(bounce, 0.0, 1.0);
                    y0 = floor;
                    if (std::abs(v) < 0.05 && gravity >= 0.0) {
                        y = floor - from.y();       // come to rest on it
                        break;
                    }
                }
            }
            p.offset = from + QPointF(std::cos(angle) * pace * age + 0.5 * wind * age * age, y);
            if (walls > 0.0 && std::abs(from.x()) < walls)
                p.offset.setX(between(from.x(), std::cos(angle) * pace, wind, -walls, walls, age, std::clamp(bounce, 0.0, 1.0)));
            if (!shape.isEmpty() && !shape.contains(from))
                p.offset = throughShape(from, QPointF(std::cos(angle) * pace, -std::sin(angle) * pace), QPointF(wind, gravity), age, floor,
                                        std::abs(from.x()) < walls ? walls : 0.0, std::clamp(bounce, 0.0, 1.0), shape);
            p.opacity = 1.0 - age / lasts;
            p.scale = 1.0 + (endSize - 1.0) * age / lasts;
            p.tint = endColor.isValid() ? age / lasts : 0.0;
            p.angle = spin * (2.0 * chance(born, i, 2) - 1.0) * age;
            p.born = born;
            out.push_back(p);
        }
    }
    return out;
}

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

double easedProgress(double t, double easeOut, double easeIn, double liftOut, double liftIn)
{
    easeOut = std::clamp(easeOut, 0.0, 1.0);
    easeIn = std::clamp(easeIn, 0.0, 1.0);
    if (easeOut <= 0.0 && easeIn <= 0.0 && liftOut == 0.0 && liftIn == 0.0)
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
    // (A handle that was pulled up or down stands that much off its level.)
    double const y1 = (easeOut > 0.0 || liftOut != 0.0 ? 0.0 : x1) + liftOut, y2 = (easeIn > 0.0 || liftIn != 0.0 ? 1.0 : x2) + liftIn;
    return bezier(s, y1, y2);
}

// ---- Layer --------------------------------------------------------------------------------

double Layer::effectAmount(int index, Pose const& pose) const
{
    if (index < 0 || index >= effects.size())
        return 0.0;
    return effects[index].animated && index < pose.fx.size() ? pose.fx[index] : effects[index].amount;
}

// The pose on `frame` of keyframes that set all of it (in order of frame).
static Pose poseOfKeys(QVector<Keyframe> const& keys, int frame);

Pose Layer::poseAt(int frame) const
{
    bool whole = true;
    for (Keyframe const& k : keys)
        whole = whole && k.parts == Keyframe::AllParts;
    if (whole)
        return poseOfKeys(keys, frame);
    // Keyframes that set only some of the pose: each part goes from one keyframe that sets it to the next.
    auto setting = [this](int part) {
        QVector<Keyframe> some;
        for (Keyframe const& k : keys)
            if (k.sets(part))
                some.push_back(k);
        return some;
    };
    QVector<Keyframe> rest = setting(Keyframe::PartRest);
    Pose pose = poseOfKeys(rest.isEmpty() ? keys : rest, frame);
    for (int bit = 0; bit < 9; ++bit) {
        QVector<Keyframe> const some = setting(1 << bit);
        if (some.isEmpty() || some.size() == rest.size())
            continue;   // (set by none: what the rest has; by just the keyframes that set the rest: as they have it)
        Pose const own = poseOfKeys(some, frame);
        switch (1 << bit) {
        case Keyframe::PartX: pose.x = own.x; break;
        case Keyframe::PartY: pose.y = own.y; break;
        case Keyframe::PartScaleX: pose.scaleX = own.scaleX; break;
        case Keyframe::PartScaleY: pose.scaleY = own.scaleY; break;
        case Keyframe::PartAngle: pose.rotation = own.rotation; break;
        case Keyframe::PartOpacity: pose.opacity = own.opacity; break;
        case Keyframe::PartZ: pose.z = own.z; break;
        case Keyframe::PartSkew: pose.skew = own.skew; break;
        default: pose.fov = own.fov; break;
        }
    }
    return pose;
}

static Pose poseOfKeys(QVector<Keyframe> const& keys, int frame)
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
    double const along = easedProgress(t, a.easeOut, b.easeIn, a.liftOut, b.liftIn);
    Pose pose = Pose::lerp(a.pose, b.pose, along);
    if (!a.controls.isEmpty()) {
        // The way there bends through the control points.
        QPointF const at = a.pathPoint(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), along);
        pose.x = at.x();
        pose.y = at.y();
        if (!a.controlsZ.isEmpty()) {
            // And in depth: through the depths of the control points.
            QVector<double> placed;
            a.pathLine(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), &placed);
            pose.z = a.pathDepth(a.pose.z, b.pose.z, along, placed);
        }
    }
    return pose;
}

double Keyframe::pathDepth(double fromZ, double toZ, double along, QVector<double> const& controlsAt) const
{
    // Straight from one depth to the next between the points: the keyframes at 0 and 1, the control points where they lie.
    double lastAt = 0.0, lastZ = fromZ;
    for (int i = 0; i < controlsAt.size(); ++i) {
        double const at = controlsAt[i];
        double const z = i < controlsZ.size() ? controlsZ[i] : fromZ + (toZ - fromZ) * at;
        if (along <= at) {
            double const span = at - lastAt;
            return span > 1e-12 ? lastZ + (z - lastZ) * (along - lastAt) / span : z;
        }
        lastAt = at;
        lastZ = z;
    }
    double const span = 1.0 - lastAt;
    return span > 1e-12 ? lastZ + (toZ - lastZ) * (along - lastAt) / span : toZ;
}

QVector<QPointF> Keyframe::pathLine(QPointF from, QPointF to, QVector<double>* controlsAt) const
{
    QVector<QPointF> points;
    points << from;
    points += controls;
    points << to;
    int const n = int(points.size());
    // The direction the path leaves each point in, and the one it arrives in (Kochanek-Bartels).
    QVector<QPointF> leaves(n), arrives(n);
    double const t = std::clamp(tension, -1.0, 1.0), c = std::clamp(continuity, -1.0, 1.0), b = std::clamp(bias, -1.0, 1.0);
    for (int i = 0; i < n; ++i) {
        QPointF const before = points[i] - points[std::max(0, i - 1)], after = points[std::min(n - 1, i + 1)] - points[i];
        leaves[i] = before * ((1 - t) * (1 + b) * (1 + c) / 2.0) + after * ((1 - t) * (1 - b) * (1 - c) / 2.0);
        arrives[i] = before * ((1 - t) * (1 + b) * (1 - c) / 2.0) + after * ((1 - t) * (1 - b) * (1 + c) / 2.0);
    }
    int const steps = 24;
    QVector<QPointF> line;
    line << from;
    QVector<int> marks;         // where in `line` each control point is
    for (int i = 0; i + 1 < n; ++i) {
        for (int s = 1; s <= steps; ++s) {
            double const u = double(s) / steps, u2 = u * u, u3 = u2 * u;
            line << points[i] * (2 * u3 - 3 * u2 + 1) + leaves[i] * (u3 - 2 * u2 + u) + points[i + 1] * (-2 * u3 + 3 * u2) + arrives[i + 1] * (u3 - u2);
        }
        if (i + 2 < n)
            marks << int(line.size()) - 1;
    }
    if (controlsAt) {
        QVector<double> length(line.size(), 0.0);
        for (int i = 1; i < line.size(); ++i)
            length[i] = length[i - 1] + std::hypot(line[i].x() - line[i - 1].x(), line[i].y() - line[i - 1].y());
        controlsAt->clear();
        for (int mark : std::as_const(marks))
            controlsAt->push_back(length.last() > 0.0 ? length[mark] / length.last() : 0.0);
    }
    return line;
}

QPointF Keyframe::pathPoint(QPointF from, QPointF to, double along) const
{
    QVector<QPointF> const line = pathLine(from, to);
    double total = 0.0;
    for (int i = 1; i < line.size(); ++i)
        total += std::hypot(line[i].x() - line[i - 1].x(), line[i].y() - line[i - 1].y());
    if (total <= 0.0)
        return from;
    // (Past either end - an ease pulled beyond it - the path goes straight on.)
    if (along <= 0.0)
        return from + (line[1] - line[0]) * (along * total / std::max(1e-9, std::hypot(line[1].x() - line[0].x(), line[1].y() - line[0].y())));
    double want = along * total, gone = 0.0;
    for (int i = 1; i < line.size(); ++i) {
        double const piece = std::hypot(line[i].x() - line[i - 1].x(), line[i].y() - line[i - 1].y());
        if (gone + piece >= want && piece > 0.0)
            return line[i - 1] + (line[i] - line[i - 1]) * ((want - gone) / piece);
        gone += piece;
    }
    QPointF const last = line.last() - line[line.size() - 2];
    double const lastLength = std::max(1e-9, std::hypot(last.x(), last.y()));
    return line.last() + last * ((want - total) / lastLength);
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
        keys[i].parts = Keyframe::AllParts;     // all of the pose given: all of it set here
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
    return d != kNoDrawing && (frame == 0 || drawingAt(frame - 1) != d || keyExposures.contains(frame));
}

void Layer::tidyKeyExposures()
{
    if (keyExposures.isEmpty())
        return;
    QVector<int> kept;
    for (int frame : std::as_const(keyExposures))
        if (frame > 0 && frame < cells.size() && cells[frame] != kNoDrawing && cells[frame - 1] == cells[frame] && !kept.contains(frame))
            kept.push_back(frame);
    std::sort(kept.begin(), kept.end());
    keyExposures = kept;
}

void Layer::shiftKeyExposures(int at, int delta)
{
    if (keyExposures.isEmpty() || delta == 0)
        return;
    QVector<int> moved;
    for (int frame : std::as_const(keyExposures)) {
        if (frame < at || (delta < 0 && frame == at))
            moved.push_back(frame);                 // before what changed (or the first cell taken out: what comes after it takes its place)
        else if (delta > 0)
            moved.push_back(frame + delta);
        else if (frame >= at - delta)
            moved.push_back(frame + delta);         // after the cells taken out
        // (a mark on a cell that was taken out goes with it)
    }
    keyExposures = moved;
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

double Morph::progress(double t) const
{
    t = std::clamp(t, 0.0, 1.0);
    if (velocity.isEmpty())
        return easedProgress(t, easeOut, easeIn);
    // A smooth curve through the ends and the points: each piece a cubic whose slopes at its ends are those of the
    // straight line between the points either side.
    QVector<QPointF> p{QPointF(0.0, 0.0)};
    for (QPointF const& v : velocity)
        if (v.x() > p.last().x() + 1e-6 && v.x() < 1.0 - 1e-6)
            p.push_back(v);
    p.push_back(QPointF(1.0, 1.0));
    int i = 0;
    while (i + 2 < p.size() && t > p[i + 1].x())
        ++i;
    auto slope = [&](int k) {
        int const a = std::max(0, k - 1), b = std::min(int(p.size()) - 1, k + 1);
        return (p[b].y() - p[a].y()) / (p[b].x() - p[a].x());
    };
    double const h = p[i + 1].x() - p[i].x(), s = (t - p[i].x()) / h;
    double const s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * p[i].y() + (s3 - 2 * s2 + s) * h * slope(i) + (-2 * s3 + 3 * s2) * p[i + 1].y()
           + (s3 - s2) * h * slope(i + 1);
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
    double const t = morphs[index].progress(double(frame - from) / double(to - from));
    return Morphing::between(drawings[drawing], drawings[other], t, morphs[index].hints);
}

int Layer::exposureStart(int frame) const
{
    int const d = drawingAt(frame);
    if (d == kNoDrawing)
        return -1;
    while (frame > 0 && cells[frame - 1] == d && !keyExposures.contains(frame))
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
    while (f < cells.size() && (cells[f] == kNoDrawing || (cells[f] == d && !keyExposures.contains(f))))
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
