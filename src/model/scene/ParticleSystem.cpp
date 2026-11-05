// Eclipse - the particle system: a Particles effect with Simulate on.  The particles are let go frame by frame and moved
// step by step through their forces (gravity, wind and friction, a vortex, an orbit, an explosion, a repulsing point,
// the pull between them, following one another, a random push), bounced off the floor, the walls and a layer's
// drawing, and killed by age or a sink; each is drawn as the layer's drawing (one of a cycle of them, or a piece of it:
// Image Fracture) or as a dot, sized, turned, faded and coloured as its age says.
//
// The same settings always give the same particles: every random choice is made from the particle's own number and
// the frame, and the simulation is run from its start (kept, so the next frame goes on from where the last one was).
#include "model/scene/Effect.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Live {
    QPointF pos, vel;
    double z = 0.0, vz = 0.0;       // (orbits about the X and Y axes go in depth too)
    double age = 0.0, life = 1.0, size = 1.0, angle = 0.0, spinRate = 0.0;
    int born = 0, id = 0, type = 0, order = 0;
    QPointF from;                   // where it was let go (the shape it bounces off is the one of that frame)
    QRectF piece;
};

struct Run {
    int next = 0;                   // the frame the simulation is at (not yet stepped)
    int ids = 0;
    QVector<Live> live;
    QHash<int, QVector<Effect::Particle>> shown;    // what it gave for the frames asked for
};

QMutex runsMutex;
QHash<QByteArray, Run> runs;
QVector<QByteArray> runOrder;

// The settings that make the simulation what it is, as a key.
QByteArray keyOf(Effect const& e, double rate, QVector<QPointF> const& places, QRectF const& art)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << e.life << e.speed << e.spread << e.direction << e.gravity << e.seed << e.endSize << e.endColor << e.wind << e.spin << e.regionWidth
        << e.regionHeight << e.regionShape << e.floor << e.bounce << e.walls << rate << art;
    for (auto v = e.values.cbegin(); v != e.values.cend(); ++v)
        out << v.key() << v.value();
    for (QPointF const& p : places)
        out << p;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
}

}  // namespace

QVector<Effect::Particle> Effect::simulate(double rate, int frame, QVector<QPointF> const& places, std::function<QPainterPath(int born)> const& shapeFor,
                                           QRectF const& art) const
{
    rate = std::clamp(rate, 0.0, 200.0);
    frame = std::max(0, frame);
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
    // A bell-shaped one (mean 0, spread 1).
    auto bell = [&](int born, int which, int what) {
        double const u = std::max(1e-9, chance(born, which, what)), v = chance(born, which, what + 1);
        return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
    };
    int const most = int(value("maxParticles"));
    int const preroll = int(value("preroll"));
    int const samples = std::max(1, int(value("samples")));
    double const probability = value("probability");
    int const sourceOffset = int(value("sourceOffset")), sourceDuration = int(value("sourceDuration"));
    int const velocityType = int(value("velocityType"));
    double const minSpeed = value("minSpeed"), maxSpeed = value("maxSpeed");
    double const windY = value("windY"), friction = value("friction");
    double const vortex = value("vortex");
    QPointF const vortexAt(value("vortexX"), value("vortexY"));
    int const orbitType = int(value("orbitType"));
    QPointF const orbitAt(value("orbitX"), value("orbitY"));
    double const aroundSpeed = value("aroundSpeed"), inSpeed = value("inSpeed");
    int const explosionFrame = int(value("explosionFrame"));
    QPointF const explosionAt(value("explosionX"), value("explosionY"));
    double const explosionStrength = value("explosionStrength"), explosionRadius = value("explosionRadius");
    double const repulse = value("repulse");
    QPointF const repulseAt(value("repulseX"), value("repulseY"));
    double const repulseRadius = value("repulseRadius");
    double const relative = value("relativeGravity"), relativeDistance = value("relativeDistance"), relativeEpsilon = value("relativeEpsilon");
    double const follow = value("follow"), randomPush = value("random");
    double const lifeSigma = value("lifeSigma"), ageAtBirth = value("ageAtBirth"), ageSigma = value("ageAtBirthSigma");
    int const killOlder = int(value("killOlder")), killYounger = int(value("killYounger"));
    int const sink = int(value("sink"));
    QRectF const sinkArea(value("sinkX") - value("sinkW") / 2.0, value("sinkY") - value("sinkH") / 2.0, value("sinkW"), value("sinkH"));
    double const startSize = value("startSize"), sizeSigma = value("sizeSigma"), globalScale = value("globalScale");
    bool const opacityByAge = flag("opacityByAge");
    int const colouring = int(value("colouring"));
    QColor const particleColour = colourValue("colour");
    double const blend = value("blend");
    double const minRotation = value("minRotation"), maxRotation = value("maxRotation");
    bool const alignDirection = flag("alignDirection");
    double const directionalScale = value("directionalScale");
    bool const dots = flag("dots");
    double const dotSize = value("dotSize");
    bool const youngestFirst = value("order") > 0.5;
    int const cycle = std::max(1, int(value("cycle")));
    int const cycleBy = int(value("cycleBy")), cycleMode = int(value("cycleMode")), typeStrategy = int(value("typeStrategy"));
    int const fracture = int(value("fracture"));
    double const bounceKept = std::clamp(bounce, 0.0, 1.0);

    QByteArray const key = keyOf(*this, rate, places, art);
    bool const keep = !shapeFor;    // (a shape to bounce off comes from another layer: not kept)
    Run run;
    if (keep) {
        QMutexLocker lock(&runsMutex);
        auto const found = runs.constFind(key);
        if (found != runs.cend()) {
            if (auto const done = found->shown.constFind(frame); done != found->shown.cend())
                return *done;
            if (found->next <= frame)
                run = *found;
        }
    }
    int const start = -preroll;
    if (run.next == 0 && run.live.isEmpty() && run.ids == 0)
        run.next = start;
    QHash<int, QPainterPath> shapes;
    auto shapeOf = [&](int born) -> QPainterPath const& {
        auto found = shapes.find(born);
        if (found == shapes.end())
            found = shapes.insert(born, shapeFor ? shapeFor(std::max(0, born)) : QPainterPath());
        return *found;
    };
    // Image Fracture: the drawing in pieces, so many across (and as many down as keeps them square).
    QVector<QRectF> pieces;
    if (fracture > 0 && art.isValid() && !art.isEmpty()) {
        int const across = fracture, down = std::max(1, int(std::lround(fracture * art.height() / std::max(1e-6, art.width()))));
        for (int r = 0; r < down; ++r)
            for (int c = 0; c < across; ++c)
                pieces.push_back(QRectF(art.left() + art.width() * c / across, art.top() + art.height() * r / down, art.width() / across, art.height() / down));
    }
    bool fractured = false;
    for (Live const& p : run.live)
        fractured = fractured || !p.piece.isEmpty();
    fractured = fractured || (run.ids > 0 && !pieces.isEmpty());

    auto letGo = [&](int f) {
        bool const emitting = f >= sourceOffset - preroll && (sourceDuration <= 0 || f < sourceOffset + sourceDuration);
        if (!emitting || chance(f, 0, 90) >= probability)
            return;
        auto make = [&](int i, QPointF from, QRectF piece) {
            Live p;
            p.born = f;
            p.id = run.ids++;
            p.order = p.id;
            double const pace = speed * (velocityType == 1 ? 1.0 : minSpeed + (maxSpeed - minSpeed) * chance(f, i, 1));
            double heading = direction;
            if (velocityType == 0)
                heading = direction + (chance(f, i, 0) - 0.5) * spread;
            else if (velocityType >= 2)
                heading = chance(f, i, 0) * 360.0;
            double const a = heading * kPi / 180.0;
            p.vel = QPointF(std::cos(a) * pace, -std::sin(a) * pace);
            if (velocityType == 3) {
                // All directions in space: some of the speed goes into depth.
                double const up = 2.0 * chance(f, i, 5) - 1.0;
                p.vel *= std::sqrt(std::max(0.0, 1.0 - up * up));
                p.vz = up * pace;
            }
            p.pos = p.from = from;
            p.piece = piece;
            p.life = std::max(1.0, life + lifeSigma * bell(f, i, 10));
            p.age = std::max(0.0, ageAtBirth + ageSigma * bell(f, i, 12));
            p.size = std::max(0.01, startSize * (1.0 + sizeSigma * bell(f, i, 14)));
            p.angle = minRotation + (maxRotation - minRotation) * chance(f, i, 16);
            p.spinRate = spin * (2.0 * chance(f, i, 2) - 1.0);
            p.type = typeStrategy == 1 ? p.id % cycle : std::min(cycle - 1, int(chance(f, i, 17) * cycle));
            run.live.push_back(p);
        };
        if (!pieces.isEmpty()) {
            // Each piece let go once, on the first frame that lets go.
            if (fractured)
                return;
            fractured = true;
            for (int i = 0; i < pieces.size(); ++i)
                make(i, pieces[i].center() - art.center(), pieces[i]);
            return;
        }
        int const count = int(std::floor(rate * (f - start + 1))) - int(std::floor(rate * (f - start)));
        for (int i = 0; i < count && run.live.size() < most; ++i) {
            QPointF from((chance(f, i, 3) - 0.5) * regionWidth, (chance(f, i, 4) - 0.5) * regionHeight);
            if (regionShape == 3) {
                from = places.isEmpty() ? QPointF() : places[std::min(int(places.size()) - 1, int(chance(f, i, 3) * places.size()))];
            } else if (regionShape != 0) {
                double const turn = chance(f, i, 3) * 2.0 * kPi;
                double const reach = regionShape == 2 ? 1.0 : std::sqrt(chance(f, i, 4));
                from = QPointF(std::cos(turn) * reach * regionWidth / 2.0, std::sin(turn) * reach * regionHeight / 2.0);
            }
            make(i, from, {});
        }
    };

    auto step = [&](int f) {
        double const dt = 1.0 / samples;
        for (int s = 0; s < samples; ++s) {
            // The pull between them (only near enough ones).
            QVector<QPointF> pulled;
            if (relative != 0.0 && run.live.size() > 1) {
                pulled.fill(QPointF(), run.live.size());
                for (int i = 0; i < run.live.size(); ++i)
                    for (int j = i + 1; j < run.live.size(); ++j) {
                        QPointF const d = run.live[j].pos - run.live[i].pos;
                        double const r2 = d.x() * d.x() + d.y() * d.y();
                        if (r2 > relativeDistance * relativeDistance)
                            continue;
                        double const pull = relative / std::pow(r2 + relativeEpsilon * relativeEpsilon, 1.5) * relativeEpsilon * relativeEpsilon;
                        pulled[i] += d * pull;
                        pulled[j] -= d * pull;
                    }
            }
            for (int i = 0; i < run.live.size(); ++i) {
                Live& p = run.live[i];
                QPointF acc(wind, gravity + windY);
                if (!pulled.isEmpty())
                    acc += pulled[i];
                if (follow > 0.0 && i > 0)
                    p.vel += (run.live[i - 1].pos - p.pos) * (follow * 0.1 * dt);
                if (randomPush > 0.0)
                    p.vel += QPointF(2.0 * chance(p.id, f, 200 + s) - 1.0, 2.0 * chance(p.id, f, 300 + s) - 1.0) * (randomPush * dt);
                if (repulse != 0.0) {
                    QPointF const d = p.pos - repulseAt;
                    double const r = std::hypot(d.x(), d.y());
                    if (r < repulseRadius && r > 1e-6)
                        acc += d / r * (repulse * (1.0 - r / repulseRadius));
                }
                if (f == explosionFrame && s == 0) {
                    QPointF const d = p.pos - explosionAt;
                    double const r = std::hypot(d.x(), d.y());
                    if (r < explosionRadius)
                        p.vel += (r > 1e-6 ? d / r : QPointF(0, -1)) * (explosionStrength * (1.0 - r / explosionRadius));
                }
                QPointF const before = p.pos;
                p.pos += p.vel * dt + acc * (0.5 * dt * dt);
                p.vel += acc * dt;
                p.z += p.vz * dt;
                if (friction > 0.0) {
                    double const kept = std::pow(1.0 - std::clamp(friction, 0.0, 1.0), dt);
                    p.vel *= kept;
                    p.vz *= kept;
                }
                if (vortex != 0.0) {
                    // Turned round the vortex's middle (the nearer, the faster it is carried round).
                    QPointF const d = p.pos - vortexAt;
                    double const a = -vortex * dt * kPi / 180.0;
                    p.pos = vortexAt + QPointF(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
                }
                if (orbitType == 1) {
                    QPointF const d = p.pos - orbitAt;
                    double const r = std::hypot(d.x(), d.y());
                    if (r > 1e-6) {
                        QPointF const out = d / r, round(-out.y(), out.x());
                        p.pos += (round * aroundSpeed - out * inSpeed) * dt;
                    }
                } else if (orbitType == 2 || orbitType == 3) {
                    // Round an axis through the point: across (X axis: up and down and in depth) or upright (Y: across and in depth).
                    double& along = orbitType == 2 ? p.pos.ry() : p.pos.rx();
                    double const centre = orbitType == 2 ? orbitAt.y() : orbitAt.x();
                    double const d = along - centre, r = std::hypot(d, p.z);
                    if (r > 1e-6) {
                        double const a = (aroundSpeed / std::max(r, 1.0)) * dt;
                        double const turned = d * std::cos(a) - p.z * std::sin(a), deep = d * std::sin(a) + p.z * std::cos(a);
                        double const shrink = std::max(0.0, 1.0 - inSpeed * dt / r);
                        along = centre + turned * shrink;
                        p.z = deep * shrink;
                    }
                }
                // Bounced off the floor, the walls and the shape it is let go against.
                if (floor > 0.0 && p.pos.y() > floor && p.vel.y() > 0.0) {
                    p.pos.setY(floor - (p.pos.y() - floor) * bounceKept);
                    p.vel.setY(-p.vel.y() * bounceKept);
                }
                if (walls > 0.0 && std::abs(p.from.x()) < walls) {
                    if (p.pos.x() > walls && p.vel.x() > 0.0) {
                        p.pos.setX(walls - (p.pos.x() - walls) * bounceKept);
                        p.vel.setX(-p.vel.x() * bounceKept);
                    } else if (p.pos.x() < -walls && p.vel.x() < 0.0) {
                        p.pos.setX(-walls + (-walls - p.pos.x()) * bounceKept);
                        p.vel.setX(-p.vel.x() * bounceKept);
                    }
                }
                if (shapeFor) {
                    QPainterPath const& shape = shapeOf(p.born);
                    if (!shape.isEmpty() && !shape.contains(p.from) && shape.contains(p.pos)) {
                        p.pos = before;
                        p.vel = -p.vel * bounceKept;
                    }
                }
                p.angle += p.spinRate * dt;
                p.age += dt;
            }
            // Killed: too old, or in (or out of) the sink.
            run.live.erase(std::remove_if(run.live.begin(), run.live.end(), [&](Live const& p) {
                if (p.age >= p.life - 1e-9 || (killOlder > 0 && p.age > killOlder + 1e-9))
                    return true;
                if (sink == 1 && sinkArea.contains(p.pos))
                    return true;
                if (sink == 2 && !sinkArea.contains(p.pos))
                    return true;
                return false;
            }), run.live.end());
        }
    };

    auto picture = [&](int f) {
        QVector<Particle> out;
        out.reserve(run.live.size());
        for (Live const& p : run.live) {
            if (killYounger > 0 && p.age < killYounger)
                continue;
            Particle q;
            double const old = std::clamp(p.age / p.life, 0.0, 1.0);
            q.offset = p.pos;
            q.born = std::max(0, p.born);
            q.opacity = opacityByAge ? 1.0 - old : 1.0;
            // (Nearer or further in depth: bigger or smaller.)
            double const depth = std::clamp(1.0 + p.z / 1000.0, 0.1, 10.0);
            q.scale = globalScale * p.size * (1.0 + (endSize - 1.0) * old) * depth;
            if (colouring == 0) {
                q.tint = endColor.isValid() ? old : 0.0;
            } else {
                q.colour = particleColour;
                q.tint = colouring == 1 ? blend : blend * old;
            }
            double const pace = std::hypot(p.vel.x(), p.vel.y());
            q.heading = std::atan2(p.vel.y(), p.vel.x()) * 180.0 / kPi;
            q.angle = p.angle + (alignDirection && pace > 1e-6 ? q.heading : 0.0);
            q.stretch = 1.0 + directionalScale * pace / 10.0;
            q.dot = dots;
            q.dotSize = dotSize;
            q.piece = p.piece;
            // Which drawing of the cycle.
            int index = cycleBy == 1 ? int(p.age) : cycleBy == 2 ? p.type : f;
            if (cycle > 1) {
                if (cycleMode == 1) {
                    int const span = 2 * cycle - 2;
                    index = ((index % span) + span) % span;
                    index = index < cycle ? index : span - index;
                } else if (cycleMode == 2) {
                    index = std::clamp(index, 0, cycle - 1);
                } else {
                    index = ((index % cycle) + cycle) % cycle;
                }
            } else {
                index = 0;
            }
            q.drawing = index;
            out.push_back(q);
        }
        if (youngestFirst)
            std::reverse(out.begin(), out.end());
        return out;
    };

    QVector<Particle> result;
    for (; run.next <= frame; ++run.next) {
        // (Those already going moved on to this frame; then the new ones let go, where they start.)
        step(run.next);
        letGo(run.next);
        if (run.next == frame)
            result = picture(frame);
    }
    if (result.isEmpty() && run.next > frame)
        result = picture(frame);
    if (keep) {
        QMutexLocker lock(&runsMutex);
        Run& kept = runs[key];
        if (!runOrder.contains(key))
            runOrder.push_back(key);
        // (Kept on from the furthest frame reached; what each frame gave, kept for going back to it.)
        if (run.next >= kept.next) {
            QHash<int, QVector<Particle>> shown = kept.shown;
            kept = run;
            kept.shown = shown;
        }
        if (kept.shown.size() > 2000)
            kept.shown.clear();
        kept.shown.insert(frame, result);
        while (runOrder.size() > 16)
            runs.remove(runOrder.takeFirst());
    }
    return result;
}
