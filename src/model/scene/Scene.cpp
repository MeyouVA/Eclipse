// Eclipse - the scene: its layers and how they hang together, its frames and markers.
#include "model/scene/Scene.h"

#include "model/geometry/Deform.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>

Scene::Scene()
{
    addLayer(QStringLiteral("Drawing"));
    addSwatch(QStringLiteral("Black"), QColor(0, 0, 0));
    addSwatch(QStringLiteral("White"), QColor(255, 255, 255));
    addSwatch(QStringLiteral("Red"), QColor(214, 48, 49));
    addSwatch(QStringLiteral("Blue"), QColor(40, 110, 200));
    addSwatch(QStringLiteral("Green"), QColor(60, 160, 80));
    addSwatch(QStringLiteral("Yellow"), QColor(245, 205, 60));
}

int Scene::markerAt(int frame) const
{
    for (int i = 0; i < markers.size(); ++i)
        if (frame >= markers[i].frame && frame < markers[i].frame + markers[i].length)
            return i;
    return -1;
}

void Scene::setFrameCount(int count)
{
    m_frameCount = std::max(1, count);
    for (Layer& layer : layers) {
        layer.cells.resize(m_frameCount, Layer::kNoDrawing);
        layer.compact();
        while (!layer.keys.isEmpty() && layer.keys.last().frame >= m_frameCount)
            layer.keys.removeLast();
    }
}

QString Scene::uniqueLayerName(QString const& base) const
{
    auto taken = [this](QString const& name) {
        for (Layer const& l : layers)
            if (l.name == name)
                return true;
        return false;
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        QString const candidate = QStringLiteral("%1_%2").arg(base).arg(n);
        if (!taken(candidate))
            return candidate;
    }
}

int Scene::layerIndex(int id) const
{
    if (id > 0)
        for (int i = 0; i < layers.size(); ++i)
            if (layers[i].id == id)
                return i;
    return -1;
}

int Scene::parentIndex(int layer) const
{
    return layer >= 0 && layer < layers.size() ? layerIndex(layers[layer].parent) : -1;
}

bool Scene::isAncestor(int ancestor, int layer) const
{
    // The guard is against loops in a damaged file; setParent never makes one.
    for (int guard = 0, l = parentIndex(layer); l >= 0 && guard < 256; ++guard, l = parentIndex(l))
        if (l == ancestor)
            return true;
    return false;
}

bool Scene::enabled(int layer) const
{
    if (layer < 0 || layer >= layers.size() || !layers[layer].visible)
        return false;
    for (int guard = 0, l = parentIndex(layer); l >= 0 && guard < 256; ++guard, l = parentIndex(l))
        if (layers[l].kind == Layer::Group && !layers[l].visible)
            return false;
    return true;
}

bool Scene::shows(int layer) const
{
    if (!enabled(layer))
        return false;
    for (Layer const& l : layers)
        if (l.solo)
            return layers[layer].solo;
    return true;
}

bool Scene::foldedAway(int layer) const
{
    for (int guard = 0, l = parentIndex(layer); l >= 0 && guard < 256; ++guard, l = parentIndex(l))
        if (layers[l].collapsed)
            return true;
    return false;
}

bool Scene::hasChildren(int layer) const
{
    if (layer < 0 || layer >= layers.size())
        return false;
    for (Layer const& l : layers)
        if (l.parent == layers[layer].id)
            return true;
    return false;
}

int Scene::duplicateLayer(int layer)
{
    if (layer < 0 || layer >= layers.size())
        return -1;
    Layer copy = layers[layer];
    copy.id = m_nextLayerId++;
    copy.name = uniqueLayerName(copy.name);
    copy.nodePlaced = false;
    layers.insert(layer + 1, copy);
    graphLayerAdded(layer + 1);
    return layer + 1;
}

int Scene::depth(int layer) const
{
    int n = 0;
    for (int l = parentIndex(layer); l >= 0 && n < 256; l = parentIndex(l))
        ++n;
    return n;
}

bool Scene::setParent(int layer, int parent)
{
    if (layer < 0 || layer >= layers.size() || parent >= layers.size())
        return false;
    if (parent >= 0 && (parent == layer || isAncestor(layer, parent)))
        return false;
    layers[layer].parent = parent >= 0 ? layers[parent].id : 0;
    // On a parent with bones, ride the bone the layer sits nearest to (judged by its pivot).
    layers[layer].parentBone = parent >= 0 ? Deform::nearestBone(layers[parent].bones, layers[layer].localTransform(0).map(layers[layer].pivot)) : -1;
    return true;
}

void Scene::removeLayer(int layer)
{
    if (layer < 0 || layer >= layers.size())
        return;
    // What was attached to it is attached to what it was attached to.
    int const id = layers[layer].id;
    int const up = layers[layer].parent;
    for (Layer& l : layers) {
        if (l.parent == id) {
            l.parent = up;
            l.parentBone = -1;
        }
    }
    graphLayerRemoved(id);
    layers.removeAt(layer);
}

int Scene::addLayer(QString const& name, int at, Layer::Kind kind)
{
    Layer layer;
    layer.id = m_nextLayerId++;
    layer.kind = kind;
    layer.pivot = QPointF(width / 2.0, height / 2.0);
    layer.name = uniqueLayerName(name);
    layer.cells.fill(Layer::kNoDrawing, m_frameCount);
    if (kind == Layer::Camera)
        layer.fov = fov;
    if (kind == Layer::Quadmap) {
        // Nothing pulled yet: the corners are those of the frame.
        layer.quadSize = QSizeF(width, height);
        layer.quad = {QPointF(0, 0), QPointF(width, 0), QPointF(width, height), QPointF(0, height)};
    }
    if (at < 0 || at > layers.size())
        at = int(layers.size());
    layers.insert(at, layer);
    graphLayerAdded(at);
    return at;
}

void Scene::graphLayerAdded(int layer)
{
    if (graph.isEmpty() || layer < 0 || layer >= layers.size())
        return;
    Layer const& added = layers[layer];
    if (added.kind == Layer::Peg || added.kind == Layer::Camera || added.kind == Layer::Sound || added.kind == Layer::Group || added.kind == Layer::Bone)
        return;     // (no picture of its own)
    GraphNode n;
    n.id = graph.nextId();
    n.kind = GraphNode::LayerNode;
    n.layer = added.id;
    n.name = added.name;
    // The composite the Display shows: the new node goes in among the layers there by where it stacks (the first input in
    // front), after those above it.
    GraphCable const* const shown = graph.display() ? graph.into(graph.display(), 0) : nullptr;
    int const composite = shown ? shown->from : 0;
    int const ci = graph.nodeIndex(composite);
    if (ci < 0 || graph.nodes[ci].kind != GraphNode::Composite) {
        n.at = QPointF(0.0, -90.0);
        graph.nodes.push_back(n);
        return;
    }
    int port = 0;
    for (GraphCable const& c : std::as_const(graph.cables)) {
        GraphNode const* const from = c.to == composite ? graph.node(c.from) : nullptr;
        if (from && from->kind == GraphNode::LayerNode && layerIndex(from->layer) > layer)
            port = std::max(port, c.toPort + 1);
    }
    for (GraphCable& c : graph.cables)
        if (c.to == composite && c.toPort >= port)
            ++c.toPort;
    graph.nodes[ci].ports += 1;
    n.at = graph.nodes[ci].at + QPointF(140.0 * (port - graph.nodes[ci].ports / 2.0), -120.0);
    graph.nodes.push_back(n);
    graph.cables.push_back({n.id, 0, composite, port});
}

void Scene::graphLayerRemoved(int layerId)
{
    int const node = graph.layerNode(layerId);
    if (!node)
        return;
    graph.cables.erase(std::remove_if(graph.cables.begin(), graph.cables.end(), [node](GraphCable const& c) { return c.from == node || c.to == node; }),
                       graph.cables.end());
    graph.nodes.removeAt(graph.nodeIndex(node));
}

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
        return 0;
    case EffectNode:
        return takesMatte(effect.type) ? 2 : 1;
    case Display:
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
        return 0;
    case Group:
    case GroupIn:
        return std::max(0, outPorts);
    default:
        return 1;
    }
}

bool GraphNode::takesMatte(Effect::Type type)
{
    return type == Effect::Cutter || type == Effect::Tone || type == Effect::Highlight || type == Effect::MatteBlur;
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
            && layers[l].kind != Layer::Bone)
            shown.push_back(l);
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
             : type == BlackWhite || type == Gradient ? 1.0 : type == MatteResize ? 4.0 : 8.0;
    e.color = type == Shadow ? QColor(0, 0, 0, 150) : type == Glow ? QColor(255, 240, 160) : type == ColourScale ? QColor(255, 200, 150)
            : type == Tone ? QColor(120, 110, 170) : type == Highlight ? QColor(255, 245, 210) : type == MatteGenerator ? QColor(255, 255, 255)
            : QColor(40, 110, 200);
    e.offset = type == Shadow ? QPointF(10, 10) : QPointF();
    if (type == DirectionalBlur)
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
    return e;
}
