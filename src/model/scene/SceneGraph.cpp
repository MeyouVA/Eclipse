// Eclipse - the scene: the node graph and controllers; drawings' joints and order; effects made.
#include "model/scene/Scene.h"
#include <QRegularExpression>
#include <QHash>

#include "model/geometry/Deform.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>

// The keyframe value on a frame: straight between the keyframes either side; held before the first and after the last.
template <typename T>
static T keyedAt(QMap<int, T> const& keys, int frame, T const& none)
{
    if (keys.isEmpty())
        return none;
    auto after = keys.lowerBound(frame);
    if (after != keys.cend() && after.key() == frame)
        return after.value();
    if (after == keys.cbegin())
        return after.value();
    auto before = std::prev(after);
    if (after == keys.cend())
        return before.value();
    double const t = double(frame - before.key()) / double(after.key() - before.key());
    return before.value() + (after.value() - before.value()) * t;
}

double GraphNode::amountAt(int frame) const
{
    return keyedAt(amountKeys, frame, effect.amount);
}

double Scene::Controller::valueAt(int frame) const
{
    return keyedAt(keys, frame, value);
}

QPointF Scene::Controller::whereAt(int frame) const
{
    return keyedAt(pointKeys, frame, where);
}

int GraphNode::inputs() const
{
    switch (kind) {
    case LayerNode:
    case GroupIn:
    case Note:
        return 0;
    case EffectNode:
        if (effect.type == Effect::ImageSwitch)
            return std::max(1, ports);
        return takesMatte(effect.type) || effect.type == Effect::GenericComposite || effect.type == Effect::DifferenceKeyer ? 2 : 1;
    case Display:
    case Waypoint:
        return 1;
    case Composite:
    case Group:
    case GroupOut:
    default:
        return std::max(0, ports);
    }
}

int GraphNode::outputs() const
{
    switch (kind) {
    case GroupOut:
    case Display:
    case Note:
        return 0;
    case Group:
    case GroupIn:
        return std::max(0, outPorts);
    default:
        return 1;
    }
}

bool GraphNode::accepts(int port, QString const& output) const
{
    QString const filter = portFilters.value(port).trimmed();
    if (filter.isEmpty())
        return true;
    QRegularExpression const pattern(QRegularExpression::wildcardToRegularExpression(filter, QRegularExpression::NonPathWildcardConversion),
                                     QRegularExpression::CaseInsensitiveOption);
    return pattern.match(output.trimmed()).hasMatch();
}

bool GraphNode::takesMatte(Effect::Type type)
{
    return type == Effect::Cutter || type == Effect::Tone || type == Effect::Highlight || type == Effect::MatteBlur || type == Effect::Blending
           || type == Effect::VariableBlur || type == Effect::Glue;
}

int NodeGraph::nodeIndex(int id) const
{
    for (int i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id)
            return i;
    return -1;
}

GraphNode const* NodeGraph::node(int id) const
{
    int const i = nodeIndex(id);
    return i >= 0 ? &nodes[i] : nullptr;
}

GraphCable const* NodeGraph::into(int id, int port) const
{
    for (GraphCable const& c : cables)
        if (c.to == id && c.toPort == port)
            return &c;
    return nullptr;
}

int NodeGraph::nextId() const
{
    int most = 0;
    for (GraphNode const& n : nodes)
        most = std::max(most, n.id);
    return most + 1;
}

int NodeGraph::display() const
{
    for (GraphNode const& n : nodes)
        if (n.kind == GraphNode::Display && n.group == 0)
            return n.id;
    return 0;
}

int NodeGraph::layerNode(int layerId) const
{
    for (GraphNode const& n : nodes)
        if (n.kind == GraphNode::LayerNode && n.layer == layerId)
            return n.id;
    return 0;
}

bool NodeGraph::feeds(int to, int from) const
{
    // Walk back from `from` along what goes into it (into a group by its output node, out of one by its input node).
    QVector<int> todo{from};
    QSet<int> seen;
    while (!todo.isEmpty()) {
        int const at = todo.takeLast();
        if (at == to)
            return true;
        if (seen.contains(at))
            continue;
        seen.insert(at);
        for (GraphCable const& c : cables)
            if (c.to == at)
                todo.push_back(c.from);
        GraphNode const* const n = node(at);
        if (n && n->kind == GraphNode::Group)
            if (int const out = groupOut(n->id))
                todo.push_back(out);
        if (n && n->kind == GraphNode::GroupIn) {
            // (What comes into a group's input node is what comes into the group - not what leaves it.)
            for (GraphCable const& c : cables)
                if (c.to == n->group)
                    todo.push_back(c.from);
        }
    }
    return false;
}

int NodeGraph::groupIn(int group) const
{
    for (GraphNode const& n : nodes)
        if (n.kind == GraphNode::GroupIn && n.group == group)
            return n.id;
    return 0;
}

int NodeGraph::groupOut(int group) const
{
    for (GraphNode const& n : nodes)
        if (n.kind == GraphNode::GroupOut && n.group == group)
            return n.id;
    return 0;
}

NodeGraph Scene::graphFromLayers() const
{
    NodeGraph g;
    GraphNode composite;
    composite.id = 1;
    composite.kind = GraphNode::Composite;
    composite.name = QStringLiteral("Composite");
    GraphNode screen;
    screen.id = 2;
    screen.kind = GraphNode::Display;
    screen.name = QStringLiteral("Display");
    // (In the composite, the first input is in front: the layers go in from the top of the stack down.)
    QVector<int> shown;
    for (int l = 0; l < layers.size(); ++l)
        if (layers[l].kind != Layer::Peg && layers[l].kind != Layer::Camera && layers[l].kind != Layer::Sound && layers[l].kind != Layer::Group
            && layers[l].kind != Layer::Bone && layers[l].kind != Layer::Annotation)
            shown.push_back(l);
    // (The pegs and groups above the rest, cabled to nothing.)
    double px = 0.0;
    for (int l = int(layers.size()) - 1; l >= 0; --l) {
        if (layers[l].kind != Layer::Peg && layers[l].kind != Layer::Group)
            continue;
        GraphNode peg;
        peg.id = g.nextId() + 2;
        peg.kind = GraphNode::LayerNode;
        peg.layer = layers[l].id;
        peg.name = layers[l].name;
        peg.at = QPointF(px, -120.0);
        px += 140.0;
        g.nodes.push_back(peg);
    }
    double x = 0.0;
    for (int i = int(shown.size()) - 1; i >= 0; --i) {
        GraphNode n;
        n.id = g.nextId() + 2;
        n.kind = GraphNode::LayerNode;
        n.layer = layers[shown[i]].id;
        n.name = layers[shown[i]].name;
        n.at = QPointF(x, 0.0);
        x += 140.0;
        g.nodes.push_back(n);
        g.cables.push_back({n.id, 0, composite.id, int(shown.size()) - 1 - i});
    }
    composite.ports = std::max(1, int(shown.size()));
    composite.at = QPointF(std::max(0.0, x - 140.0) / 2.0, 120.0);
    screen.at = composite.at + QPointF(0.0, 90.0);
    g.nodes.push_back(composite);
    g.nodes.push_back(screen);
    g.cables.push_back({composite.id, 0, screen.id, 0});
    return g;
}

QVector<double> Scene::Controller::weightsAt(QPointF spot) const
{
    QVector<double> weights(states.size(), 0.0);
    if (states.isEmpty())
        return weights;
    // Each state by one over its distance squared from the point: smooth between them, and exactly a state on it.
    double total = 0.0;
    for (int i = 0; i < states.size(); ++i) {
        QPointF const d = states[i].at - spot;
        double const distance2 = d.x() * d.x() + d.y() * d.y();
        if (distance2 < 1e-12) {
            std::fill(weights.begin(), weights.end(), 0.0);
            weights[i] = 1.0;
            return weights;
        }
        weights[i] = 1.0 / distance2;
        total += weights[i];
    }
    for (double& w : weights)
        w /= total;
    return weights;
}

void Drawing::keepJoints(Drawing const& before)
{
    for (int sheet = 0; sheet < SheetCount; ++sheet) {
        QVector<Stroke>& lines = strokesOf(sheet);
        // Where each joint was; where it went (the first of its points that moved), and how many points it has now.
        QHash<int, QPointF> was, went;
        QHash<int, int> count;
        for (Stroke const& line : before.strokesOf(sheet))
            for (StrokePoint const& point : line.points)
                if (point.joint != 0 && !was.contains(point.joint))
                    was.insert(point.joint, point.pos);
        for (Stroke const& line : std::as_const(lines))
            for (StrokePoint const& point : line.points) {
                if (point.joint == 0)
                    continue;
                ++count[point.joint];
                if (!went.contains(point.joint) && (!was.contains(point.joint) || point.pos != was.value(point.joint)))
                    went.insert(point.joint, point.pos);
            }
        for (Stroke& line : lines)
            for (StrokePoint& point : line.points) {
                if (point.joint == 0)
                    continue;
                if (count.value(point.joint) < 2)
                    point.joint = 0;
                else if (went.contains(point.joint))
                    point.pos = went.value(point.joint);
            }
    }
}

int Drawing::nextJoint() const
{
    int most = 0;
    for (int sheet = 0; sheet < SheetCount; ++sheet)
        for (Stroke const& line : strokesOf(sheet))
            for (StrokePoint const& point : line.points)
                most = std::max(most, point.joint);
    return most + 1;
}

void Drawing::freshJoints(QVector<Stroke>& strokes, Drawing const& into)
{
    int next = into.nextJoint();
    QHash<int, int> renamed;
    for (Stroke& line : strokes)
        for (StrokePoint& point : line.points)
            if (point.joint != 0) {
                if (!renamed.contains(point.joint))
                    renamed.insert(point.joint, next++);
                point.joint = renamed.value(point.joint);
            }
}

int Drawing::topOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes)
{
    int top = 0;
    bool any = false;
    for (Fill const& f : fills) {
        top = any ? std::max(top, f.order) : f.order;
        any = true;
    }
    for (Stroke const& s : strokes) {
        top = any ? std::max(top, s.order) : s.order;
        any = true;
    }
    return top;
}

int Drawing::bottomOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes)
{
    int bottom = 0;
    bool any = false;
    for (Fill const& f : fills) {
        bottom = any ? std::min(bottom, f.order) : f.order;
        any = true;
    }
    for (Stroke const& s : strokes) {
        bottom = any ? std::min(bottom, s.order) : s.order;
        any = true;
    }
    return bottom;
}

int Drawing::orderOnTop(QVector<Fill> const& fills, QVector<Stroke> const& strokes)
{
    // On a par with the highest unless a line is there (lines lie over areas of the same number).
    int const top = topOrder(fills, strokes);
    for (Stroke const& s : strokes)
        if (s.order == top)
            return top + 1;
    return top;
}

Effect Effect::made(Type type)
{
    Effect e;
    e.type = type;
    e.amount = type == Tint ? 0.5 : type == Shadow ? 6.0 : type == Particles ? 2.0 : type == Transparency ? 0.5
             : type == HueSaturation || type == BrightnessContrast || type == ColourScale ? 1.0 : type == DirectionalBlur ? 12.0
             : type == Grain ? 0.3 : type == RadialBlur ? 0.15 : type == Negate ? 1.0 : type == Median ? 2.0 : type == Tone || type == Highlight ? 2.0
             : type == Bloom ? 12.0 : type == Pixelate ? 10.0 : type == Turbulence ? 6.0 : type == MatteGenerator ? 30.0
             : type == BlackWhite || type == Gradient ? 1.0 : type == MatteResize ? 4.0 : type == RemoveTransparency ? 0.5
             : type == VariableBlur ? 12.0 : type == MotionBlur ? 4.0 : type == FieldChart ? 1.0 : type == ColourCurves ? 1.0 : type == Focus ? 4.0 : type == ApplyPeg ? 1.0 : type == ScaleOutput ? 0.5 : type == Glue ? 10.0 : type == Script || type == External || type == ApplyCamera ? 1.0
             : type == GenericComposite || type == LayerSelector || type == Visibility ? 1.0 : type == ChromaKey ? 0.3 : type == DifferenceKeyer ? 0.1
             : type == LuminanceThreshold ? 0.5 : type == ImageSwitch ? 0.0 : 8.0;
    e.color = type == Shadow ? QColor(0, 0, 0, 150) : type == Glow ? QColor(255, 240, 160) : type == ColourScale ? QColor(255, 200, 150)
            : type == Tone ? QColor(120, 110, 170) : type == Highlight ? QColor(255, 245, 210) : type == MatteGenerator ? QColor(255, 255, 255)
            : type == FieldChart ? QColor(30, 30, 30)
            : QColor(40, 110, 200);
    e.offset = type == Shadow ? QPointF(10, 10) : QPointF();
    if (type == Script)
        e.text = QStringLiteral("// The picture, each frame: image.blur(4), image.tint(\"#ff8000\", 0.5), image.negate(1), ...\n"
                                "image.blur(amount * 4);\n");
    if (type == External)
        e.text = QStringLiteral("magick {in} -charcoal 2 {out}");
    if (type == DirectionalBlur || type == Focus)
        e.direction = 0.0;
    if (type == Ripple)
        e.direction = 60.0;
    if (type == Turbulence)
        e.direction = 40.0;
    if (type == MatteGenerator)
        e.direction = 0.0;
    if (type == Gradient) {
        e.direction = 90.0;
        e.color = QColor(255, 210, 120);
        e.endColor = QColor(200, 60, 140);
    }
    if (type == HueSaturation)
        e.hue = 30.0;
    if (type == BrightnessContrast)
        e.brightness = 20.0;
    if (type == ChromaKey) {
        e.color = QColor(0, 177, 64);
        e.softness = 0.1;
        e.spill = 0.5;
    }
    if (type == DifferenceKeyer || type == LuminanceThreshold)
        e.softness = 0.05;
    return e;
}
