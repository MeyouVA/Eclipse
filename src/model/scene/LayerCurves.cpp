// Eclipse - the curves of a layer's values: which keyframes each goes through, and the shape it takes between them.
#include "model/scene/Layer.h"

#include <algorithm>
#include <cmath>

#include <QMap>

namespace {

// The values of `bits` (Keyframe::Part) from one pose into another.
void takeValues(Pose& into, Pose const& from, int bits)
{
    for (int value = 0; value < Driver::ValueCount; ++value)
        if (bits & (1 << value))
            Driver::write(into, value, Driver::read(from, value));
}

// Nothing of the rest of the pose (the drawing's points, deformers, effects) that matters is set by this keyframe.
bool restIsPlain(Pose const& pose)
{
    return pose.quad.isEmpty() && pose.bend.isEmpty() && pose.warp.isEmpty() && pose.fx.isEmpty() && pose.points.isEmpty() && pose.rotX == 0.0
           && pose.rotY == 0.0;
}

void dropCurves(Keyframe& key, int bits)
{
    for (int value = 0; value < Driver::ValueCount; ++value)
        if (bits & (1 << value))
            key.dropCurve(value);
}

// (frame, bits) to bits by frame, the rest of the pose left out.
QMap<int, int> byFrame(QVector<QPair<int, int>> const& points)
{
    QMap<int, int> out;
    for (QPair<int, int> const& p : points)
        out[p.first] |= p.second & (Keyframe::AllParts & ~Keyframe::PartRest);
    return out;
}

}  // namespace

ValueCurve const* Keyframe::curveOf(int value) const
{
    for (ValueCurve const& c : curves)
        if (c.value == value)
            return &c;
    return nullptr;
}

void Keyframe::setCurve(ValueCurve const& curve)
{
    for (ValueCurve& c : curves)
        if (c.value == curve.value) {
            c = curve;
            return;
        }
    curves.push_back(curve);
    std::sort(curves.begin(), curves.end(), [](ValueCurve const& a, ValueCurve const& b) { return a.value < b.value; });
}

void Keyframe::dropCurve(int value)
{
    curves.removeIf([value](ValueCurve const& c) { return c.value == value; });
}

QVector<int> Layer::keysOf(int value) const
{
    QVector<int> all, some, rest;
    for (int k = 0; k < keys.size(); ++k) {
        all.push_back(k);
        if (keys[k].sets(1 << value))
            some.push_back(k);
        if (keys[k].sets(Keyframe::PartRest))
            rest.push_back(k);
    }
    // Set by none: it goes as the rest of the pose does.
    if (some.isEmpty())
        return rest.isEmpty() ? all : rest;
    return some;
}

void Layer::segmentHandles(int value, int from, int to, QPointF& out, QPointF& in) const
{
    Keyframe const& a = keys[from];
    Keyframe const& b = keys[to];
    double const span = b.frame - a.frame;
    double const way = Driver::read(b.pose, value) - Driver::read(a.pose, value);
    if (ValueCurve const* own = a.curveOf(value)) {
        out = own->out;
    } else {
        // (The ease: see easedProgress.)
        double const ease = std::clamp(a.easeOut, 0.0, 1.0), x = ease * 0.8;
        double const y = (ease > 0.0 || a.liftOut != 0.0 ? 0.0 : x) + a.liftOut;
        out = QPointF(x * span, y * way);
    }
    if (ValueCurve const* own = b.curveOf(value)) {
        in = own->in;
    } else {
        double const ease = std::clamp(b.easeIn, 0.0, 1.0), x = 1.0 - ease * 0.8;
        double const y = (ease > 0.0 || b.liftIn != 0.0 ? 1.0 : x) + b.liftIn;
        in = QPointF((x - 1.0) * span, (y - 1.0) * way);
    }
}

double Layer::curveValue(int value, QVector<int> const& through, double frame) const
{
    if (through.isEmpty())
        return Driver::read(Pose(), value);
    if (frame <= keys[through.first()].frame)
        return Driver::read(keys[through.first()].pose, value);
    if (frame >= keys[through.last()].frame)
        return Driver::read(keys[through.last()].pose, value);
    int i = 0;
    while (keys[through[i + 1]].frame <= frame)
        ++i;
    Keyframe const& a = keys[through[i]];
    Keyframe const& b = keys[through[i + 1]];
    double const va = Driver::read(a.pose, value), vb = Driver::read(b.pose, value);
    ValueCurve const* const own = a.curveOf(value);
    if (!a.tween || frame == a.frame || (own && own->after == ValueCurve::Held))
        return va;
    double const span = b.frame - a.frame, at = frame - a.frame;
    if (own && own->after == ValueCurve::Straight)
        return va + (vb - va) * at / span;
    QPointF out, in;
    segmentHandles(value, through[i], through[i + 1], out, in);
    // (The handles kept within the frames between, so that the curve has one value on each frame.)
    double const x1 = std::clamp(out.x(), 0.0, span), x2 = std::clamp(span + in.x(), 0.0, span);
    double const y1 = va + out.y(), y2 = vb + in.y();
    auto cubic = [](double s, double p0, double p1, double p2, double p3) {
        double const u = 1.0 - s;
        return u * u * u * p0 + 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s * p3;
    };
    double lo = 0.0, hi = 1.0;
    for (int n = 0; n < 48; ++n) {
        double const mid = (lo + hi) / 2.0;
        if (cubic(mid, 0.0, x1, x2, span) < at)
            lo = mid;
        else
            hi = mid;
    }
    return cubic((lo + hi) / 2.0, va, y1, y2, vb);
}

void Layer::spellOut(int bits)
{
    for (int value = 0; value < Driver::ValueCount; ++value)
        if (bits & (1 << value))
            for (int k : keysOf(value))
                keys[k].parts |= 1 << value;
}

bool Layer::moveValues(QVector<QPair<int, int>> const& points, int by, int last)
{
    QMap<int, int> const moving = byFrame(points);
    if (moving.isEmpty() || by == 0)
        return false;
    for (auto it = moving.cbegin(); it != moving.cend(); ++it) {
        if (keyIndex(it.key()) < 0 || it.key() + by < 0 || it.key() + by > last)
            return false;
        // Where they land: no point of those values there that stays.
        int const there = keyIndex(it.key() + by);
        if (there >= 0 && (keys[there].parts & ~moving.value(it.key() + by, 0) & it.value()))
            return false;
    }
    int all = 0;
    for (int bits : moving)
        all |= bits;
    spellOut(all);
    // Each point taken off its keyframe; a keyframe that has nothing else left goes along whole.
    QVector<Keyframe> pieces;
    for (auto it = moving.cbegin(); it != moving.cend(); ++it) {
        int const k = keyIndex(it.key());
        Keyframe& key = keys[k];
        int const left = key.parts & ~it.value();
        if (left == 0 || (left == Keyframe::PartRest && restIsPlain(key.pose))) {
            Keyframe whole = keys.takeAt(k);
            whole.frame += by;
            pieces.push_back(whole);
            continue;
        }
        Keyframe piece = key;
        piece.frame += by;
        piece.parts = it.value();
        piece.curves.removeIf([&](ValueCurve const& c) { return !(it.value() & (1 << c.value)); });
        key.parts = left;
        dropCurves(key, it.value());
        pieces.push_back(piece);
    }
    for (Keyframe const& piece : pieces) {
        int const there = keyIndex(piece.frame);
        if (there < 0) {
            int at = 0;
            while (at < keys.size() && keys[at].frame < piece.frame)
                ++at;
            keys.insert(at, piece);
            continue;
        }
        Keyframe& key = keys[there];
        int const bits = piece.parts & ~Keyframe::PartRest;
        takeValues(key.pose, piece.pose, bits);
        key.parts |= bits;
        for (ValueCurve const& c : piece.curves)
            key.setCurve(c);
    }
    return true;
}

void Layer::removeValues(QVector<QPair<int, int>> const& points)
{
    QMap<int, int> taking = byFrame(points);
    for (auto it = taking.begin(); it != taking.end();)
        it = keyIndex(it.key()) < 0 ? taking.erase(it) : std::next(it);
    if (taking.isEmpty())
        return;
    int all = 0;
    for (int bits : taking)
        all |= bits;
    spellOut(all);
    for (auto it = taking.cbegin(); it != taking.cend(); ++it) {
        int const k = keyIndex(it.key());
        Keyframe& key = keys[k];
        key.parts &= ~it.value();
        dropCurves(key, it.value());
        if (key.parts == 0 || (key.parts == Keyframe::PartRest && restIsPlain(key.pose)))
            keys.removeAt(k);
    }
}

ValueCurve Layer::curveAt(int value, int key) const
{
    ValueCurve curve;
    curve.value = value;
    QVector<int> const through = keysOf(value);
    int const at = int(through.indexOf(key));
    if (key < 0 || key >= keys.size() || at < 0)
        return curve;
    if (ValueCurve const* own = keys[key].curveOf(value))
        return *own;
    QPointF unused;
    if (at > 0)
        segmentHandles(value, through[at - 1], key, unused, curve.in);
    if (at + 1 < through.size())
        segmentHandles(value, key, through[at + 1], curve.out, unused);
    curve.corner = at > 0 && at + 1 < through.size() && (keys[key].easeIn > 0.0 || keys[key].easeOut > 0.0);
    curve.after = keys[key].tween ? ValueCurve::Curved : ValueCurve::Held;
    return curve;
}

void Layer::easeCurves(int key)
{
    if (key < 0 || key >= keys.size())
        return;
    QVector<ValueCurve> const own = keys[key].curves;
    keys[key].curves.clear();
    for (ValueCurve c : own) {
        // Curved on and smooth: nothing the ease does not do.  Straight or held on: kept, with the handles of the ease.
        if (c.after == ValueCurve::Curved)
            continue;
        ValueCurve const eased = curveAt(c.value, key);
        c.in = eased.in;
        c.out = eased.out;
        keys[key].setCurve(c);
    }
}

void Layer::shapeSegment(int value, int from, int to, QPointF leave, QPointF arrive)
{
    if (from < 0 || to <= from || to >= keys.size())
        return;
    double const span = keys[to].frame - keys[from].frame;
    double const way = Driver::read(keys[to].pose, value) - Driver::read(keys[from].pose, value);
    ValueCurve a = curveAt(value, from), b = curveAt(value, to);
    a.out = QPointF(std::clamp(leave.x(), 0.0, 1.0) * span, leave.y() * way);
    a.after = ValueCurve::Curved;
    a.corner = true;
    b.in = QPointF((std::clamp(arrive.x(), 0.0, 1.0) - 1.0) * span, (arrive.y() - 1.0) * way);
    b.corner = true;
    keys[from].setCurve(a);
    keys[to].setCurve(b);
}
