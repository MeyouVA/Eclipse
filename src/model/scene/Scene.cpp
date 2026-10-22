// Eclipse - the scene: made, markers, frames, layers - names, ids, parents, what shows, added and removed.
#include "model/scene/Scene.h"
#include <QHash>

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
        if (l.solo && soloMode)
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
    if (added.kind == Layer::Camera || added.kind == Layer::Sound || added.kind == Layer::Bone || added.kind == Layer::Annotation)
        return;     // (no picture of its own, nor anything to hang from it in the graph)
    GraphNode n;
    n.id = graph.nextId();
    n.kind = GraphNode::LayerNode;
    n.layer = added.id;
    n.name = added.name;
    if (added.kind == Layer::Peg || added.kind == Layer::Group) {
        // A peg or a group: a node, cabled to nothing (what hangs from it is shown by the parenting cables).
        n.at = QPointF(0.0, -210.0);
        graph.nodes.push_back(n);
        return;
    }
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
