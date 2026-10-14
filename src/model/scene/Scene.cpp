// Eclipse - the scene: its layers and how they hang together, its frames and markers.
#include "model/scene/Scene.h"

#include "model/geometry/Deform.h"

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
    return at;
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
             : 8.0;
    e.color = type == Shadow ? QColor(0, 0, 0, 150) : type == Glow ? QColor(255, 240, 160) : type == ColourScale ? QColor(255, 200, 150)
            : type == Tone ? QColor(120, 110, 170) : type == Highlight ? QColor(255, 245, 210) : QColor(40, 110, 200);
    e.offset = type == Shadow ? QPointF(10, 10) : QPointF();
    if (type == DirectionalBlur)
        e.direction = 0.0;
    if (type == Ripple)
        e.direction = 60.0;
    if (type == HueSaturation)
        e.hue = 30.0;
    if (type == BrightnessContrast)
        e.brightness = 20.0;
    return e;
}
