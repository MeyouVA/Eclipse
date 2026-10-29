// Eclipse - the Node View's extras: node colours and editor buttons, thumbnails, the magnifier, the cable cutter,
// the view as a picture, and the mouse on what both kinds of view share.
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"

#include <QBitmap>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"

namespace {

// The cable cutter's pointer: a pair of scissors.
QCursor cutterCursor()
{
    QPixmap picture(24, 24);
    picture.fill(Qt::transparent);
    QPainter p(&picture);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (QColor const& colour : {QColor(255, 255, 255), QColor(20, 20, 20)}) {
        double const w = colour == QColor(255, 255, 255) ? 3.2 : 1.4;
        p.setPen(QPen(colour, w));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(6, 18), 3.5, 3.5);
        p.drawEllipse(QPointF(18, 18), 3.5, 3.5);
        p.drawLine(QPointF(8.5, 15.5), QPointF(19, 2));
        p.drawLine(QPointF(15.5, 15.5), QPointF(5, 2));
    }
    p.end();
    return QCursor(picture, 12, 6);
}

}  // namespace

// ---- colours and editor buttons ------------------------------------------------------------

QColor NodeView::layerNodeColour(int l) const
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return InterfaceColours::get("nodeLight");
    Layer const& layer = scene.layers[l];
    switch (layer.kind) {
    case Layer::Peg:
    case Layer::Bone:
    case Layer::Quadmap:
        return InterfaceColours::get("nodePeg");
    case Layer::Camera:
        return InterfaceColours::get("nodeCamera");
    case Layer::Sound:
        return InterfaceColours::get("nodeSound");
    case Layer::Group:
        return InterfaceColours::get("nodeGroup");
    default:
        // (A drawing in its layer's colour, when the preferences say so and it has one.)
        return m_options.readColour && layer.trackColor.isValid() ? layer.trackColor : InterfaceColours::get("nodeDrawing");
    }
}

bool NodeView::effectGroup(int id) const
{
    NodeGraph const& g = m_document->scene().graph;
    bool any = false;
    for (GraphNode const& n : g.nodes) {
        if (n.group != id || n.kind == GraphNode::GroupIn || n.kind == GraphNode::GroupOut || n.kind == GraphNode::Waypoint)
            continue;
        if (n.kind != GraphNode::EffectNode)
            return false;
        any = true;
    }
    return any;
}

QColor NodeView::nodeBorder(GraphNode const* node, int l) const
{
    Scene const& scene = m_document->scene();
    if (l >= 0 && l < scene.layers.size()) {
        switch (scene.layers[l].kind) {
        case Layer::Camera:
            return InterfaceColours::get("nodeCameraBorder");
        case Layer::Peg:
        case Layer::Bone:
        case Layer::Quadmap:
            return InterfaceColours::get("nodePegDark");
        case Layer::Group:
            return InterfaceColours::get("nodeGroupDark");
        default:
            return InterfaceColours::get("nodeDark");
        }
    }
    if (!node)
        return InterfaceColours::get("nodeDark");
    switch (node->kind) {
    case GraphNode::Group:
        return InterfaceColours::get(effectGroup(node->id) ? "nodeGroupEffectDark" : "nodeGroupDark");
    case GraphNode::GroupIn:
    case GraphNode::GroupOut:
        return InterfaceColours::get("nodeProxyPortDark");
    case GraphNode::Display:
        return InterfaceColours::get("nodeDisplayDark");
    default:
        return InterfaceColours::get("nodeDark");
    }
}

bool NodeView::showsEditor(int l, GraphNode const* node) const
{
    // A button at a node's right end opens its editor: a layer's properties, an effect's settings.
    if (node)
        return node->kind == GraphNode::EffectNode || (node->kind == GraphNode::LayerNode && l >= 0);
    Scene const& scene = m_document->scene();
    return l >= 0 && l < scene.layers.size() && scene.layers[l].kind != Layer::Group;
}

QRectF NodeView::editorButton(QRectF const& node) const
{
    double const s = 9.0 * m_zoom;
    return QRectF(node.right() - s - 5.0 * m_zoom, node.center().y() - s / 2.0, s, s);
}

void NodeView::paintEditorButton(QPainter& p, QRectF const& node)
{
    p.save();
    p.setPen(QPen(InterfaceColours::get("nodeEditorDark"), 1));
    p.setBrush(InterfaceColours::get("nodeEditorLight"));
    p.drawRect(editorButton(node));
    p.restore();
}

QVector<int> NodeView::shownKeys() const
{
    QVector<int> keys;
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        for (GraphNode const& n : scene.graph.nodes)
            if (n.group == m_graphGroup)
                keys << n.id;
    } else {
        for (int l = 0; l < scene.layers.size(); ++l)
            if (nodeShown(l))
                keys << scene.layers[l].id;
    }
    return keys;
}

QRectF NodeView::nodeRectOf(int key) const
{
    return graphMode() ? graphNodeRect(key) : nodeRect(m_document->scene().layerIndex(key));
}

int NodeView::editorAt(QPointF widgetPos) const
{
    Scene const& scene = m_document->scene();
    for (int key : shownKeys()) {
        QRectF const r = nodeRectOf(key);
        if (r.isNull())
            continue;
        bool const shows = graphMode() ? [&] {
            GraphNode const* const n = scene.graph.node(key);
            return n && showsEditor(n->kind == GraphNode::LayerNode ? scene.layerIndex(n->layer) : -1, n);
        }()
                                       : showsEditor(scene.layerIndex(key), nullptr);
        if (shows && editorButton(r).adjusted(-2, -2, 2, 2).contains(widgetPos))
            return key;
    }
    return 0;
}

void NodeView::openEditor(int key)
{
    if (graphMode()) {
        pickGraphNode(key);
    } else {
        int const l = m_document->scene().layerIndex(key);
        if (l < 0)
            return;
        m_layerPicked = {key};
        m_layer = l;
        emit layerPicked(l);
    }
    onActionEditor();
}

// ---- thumbnails ----------------------------------------------------------------------------

bool NodeView::thumbnailShown(int key) const
{
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        GraphNode const* const n = scene.graph.node(key);
        return n && n->thumb > 0;
    }
    int const l = scene.layerIndex(key);
    return l >= 0 && scene.layers[l].nodeThumb > 0;
}

QRectF NodeView::thumbnailRect(int key) const
{
    Scene const& scene = m_document->scene();
    int height = 0;
    if (graphMode()) {
        if (GraphNode const* const n = scene.graph.node(key))
            height = n->thumb;
    } else if (int const l = scene.layerIndex(key); l >= 0) {
        height = scene.layers[l].nodeThumb;
    }
    QRectF const r = nodeRectOf(key);
    if (height <= 0 || r.isNull())
        return QRectF();
    if (m_drag == DragThumb && m_thumbDrag == key)
        height = std::clamp(int(std::lround(m_thumbStart - (m_dragNow.y() - m_pressPos.y()) / m_zoom)), 24, 2000);
    // Over the node, as wide as the frame is for its height.
    double const h = height * m_zoom, w = h * double(scene.width) / std::max(1.0, double(scene.height));
    return QRectF(r.center().x() - w / 2.0, r.top() - h - 8.0 * m_zoom, w, h);
}

QImage NodeView::thumbnailImage(int key, QSize size) const
{
    if (size.width() < 2 || size.height() < 2)
        return QImage();
    QString const name = QStringLiteral("%1:%2:%3x%4").arg(key).arg(m_frame).arg(size.width()).arg(size.height());
    if (auto const found = m_thumbCache.constFind(name); found != m_thumbCache.cend())
        return *found;
    Scene copy = m_document->scene();
    copy.background = Qt::transparent;
    copy.soloMode = false;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    double const k = std::min(size.width() / std::max(1.0, double(copy.width)), size.height() / std::max(1.0, double(copy.height)));
    p.translate((size.width() - copy.width * k) / 2.0, (size.height() - copy.height * k) / 2.0);
    p.scale(k, k);
    p.setClipRect(QRectF(0, 0, copy.width, copy.height));
    GraphNode const* const n = graphMode() ? copy.graph.node(key) : nullptr;
    if (n && n->kind != GraphNode::LayerNode) {
        // What leaves the node: the graph shown through a display of its own, fed from it.
        GraphNode display;
        display.id = copy.graph.nextId();
        display.kind = GraphNode::Display;
        copy.graph.cables.push_back({key, 0, display.id, 0});
        copy.graph.nodes.prepend(display);
        Canvas::paintFrame(p, copy, m_frame);
    } else {
        // A layer's picture alone (a group's: all in it).
        int const l = copy.layerIndex(n ? n->layer : key);
        if (l >= 0) {
            QSet<int> keep{l}, above;
            for (bool more = true; more;) {
                more = false;
                for (int c = 0; c < copy.layers.size(); ++c)
                    if (!keep.contains(c) && keep.contains(copy.parentIndex(c))) {
                        keep.insert(c);
                        more = true;
                    }
            }
            for (int up = copy.parentIndex(l), guard = 0; up >= 0 && guard < 256; up = copy.parentIndex(up), ++guard)
                above.insert(up);
            for (int c = 0; c < copy.layers.size(); ++c) {
                copy.layers[c].solo = false;
                if (!keep.contains(c))
                    copy.layers[c].visible = above.contains(c) && copy.layers[c].kind == Layer::Group && copy.layers[c].visible;
            }
            copy.graph = NodeGraph();
            Canvas::paintFrame(p, copy, m_frame);
        }
    }
    p.end();
    m_thumbCache.insert(name, image);
    return image;
}

void NodeView::paintThumbnails(QPainter& p)
{
    for (int key : shownKeys()) {
        QRectF const r = thumbnailRect(key);
        if (r.isNull())
            continue;
        p.save();
        // On the thumbnail background colour, or on a chequer pattern to see what is see-through.
        if (m_thumbPattern) {
            double const cell = std::max(3.0, 6.0 * m_zoom);
            p.fillRect(r, QColor(205, 205, 205));
            p.setClipRect(r);
            for (double y = r.top(); y < r.bottom(); y += cell)
                for (double x = r.left() + (int(std::floor((y - r.top()) / cell)) % 2) * cell; x < r.right(); x += 2 * cell)
                    p.fillRect(QRectF(x, y, cell, cell), QColor(240, 240, 240));
            p.setClipping(false);
        } else {
            p.fillRect(r, InterfaceColours::get("nodeThumbnailBackground"));
        }
        double const dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
        QImage const image = thumbnailImage(key, (r.size() * dpr).toSize());
        if (!image.isNull())
            p.drawImage(r, image);
        p.setPen(QPen(InterfaceColours::get("nodeDark"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        // The grip at its top right corner: dragged, the thumbnail grows or shrinks.
        if (!m_printing) {
            double const g = 7.0 * std::max(0.6, m_zoom);
            QPainterPath grip;
            grip.moveTo(r.topRight());
            grip.lineTo(r.topRight() + QPointF(-g, 0));
            grip.lineTo(r.topRight() + QPointF(0, g));
            grip.closeSubpath();
            p.fillPath(grip, InterfaceColours::get("backdropResize"));
        }
        p.restore();
    }
}

int NodeView::thumbGripAt(QPointF widgetPos) const
{
    for (int key : shownKeys()) {
        QRectF const r = thumbnailRect(key);
        if (!r.isNull() && QRectF(r.topRight() - QPointF(10, 4), QSizeF(14, 14)).contains(widgetPos))
            return key;
    }
    return 0;
}

void NodeView::setThumbs(QVector<int> const& keys, int height, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        NodeGraph g = scene.graph;
        bool changed = false;
        for (int key : keys)
            if (int const k = g.nodeIndex(key); k >= 0 && g.nodes[k].thumb != height) {
                GraphNode const& n = g.nodes[k];
                // (Only nodes that give a picture have one.)
                bool const gives = n.kind == GraphNode::Composite || n.kind == GraphNode::EffectNode || n.kind == GraphNode::Group
                                   || (n.kind == GraphNode::LayerNode && !pictureless(n));
                if (gives || height == 0) {
                    g.nodes[k].thumb = height;
                    changed = true;
                }
            }
        if (changed)
            m_document->setGraph(g, text);
        return;
    }
    QVector<int> layers;
    for (int key : keys)
        if (int const l = scene.layerIndex(key); l >= 0 && scene.layers[l].nodeThumb != height
                                                  && (height == 0 || scene.layers[l].holdsDrawings() || scene.layers[l].kind == Layer::Group))
            layers << l;
    if (!layers.isEmpty())
        m_document->editLayers(layers, text, [height](Layer& layer) { layer.nodeThumb = height; });
}

void NodeView::onActionShowSelectedThumbnails()
{
    setThumbs(pickedKeys(), 90, tr("Show Selected Thumbnails"));
}

void NodeView::onActionHideSelectedThumbnails()
{
    setThumbs(pickedKeys(), 0, tr("Hide Selected Thumbnails"));
}

void NodeView::onActionToggleSelectedThumbnails()
{
    QVector<int> const keys = pickedKeys();
    bool const all = !keys.isEmpty() && std::all_of(keys.cbegin(), keys.cend(), [this](int key) { return thumbnailShown(key); });
    if (all)
        onActionHideSelectedThumbnails();
    else
        onActionShowSelectedThumbnails();
}

void NodeView::onActionShowAllThumbnails()
{
    QVector<int> all;
    if (graphMode())
        for (GraphNode const& n : m_document->scene().graph.nodes)
            all << n.id;
    else
        for (Layer const& l : m_document->scene().layers)
            all << l.id;
    setThumbs(all, 90, tr("Show All Thumbnails"));
}

void NodeView::onActionHideAllThumbnails()
{
    QVector<int> all;
    if (graphMode())
        for (GraphNode const& n : m_document->scene().graph.nodes)
            all << n.id;
    else
        for (Layer const& l : m_document->scene().layers)
            all << l.id;
    setThumbs(all, 0, tr("Hide All Thumbnails"));
}

void NodeView::onActionThumbnailPattern(bool on)
{
    m_thumbPattern = on;
    update();
}

// ---- the navigator, the magnifier ----------------------------------------------------------

void NodeView::onActionShowNavigator(bool on)
{
    setNavigatorShown(on);
}

void NodeView::onActionMagnifier(bool on)
{
    m_magnifier = on;
    setMouseTracking(on);
    if (on && !rect().contains(m_hover.toPoint()))
        m_hover = mapFromGlobal(QCursor::pos());
    update();
}

QRectF NodeView::magnifierRect() const
{
    if (!m_magnifier || !rect().contains(m_hover.toPoint()))
        return QRectF();
    double const w = std::clamp(m_options.magnifierWidth, 0.1, 10.0) / 10.0 * width();
    double const h = w / std::clamp(m_options.magnifierAspect, 0.1, 10.0);
    return QRectF(m_hover.x() - w / 2.0, m_hover.y() - h / 2.0, w, h);
}

void NodeView::paintMagnifier(QPainter& p)
{
    QRectF const lens = magnifierRect();
    if (lens.isEmpty() || lens.width() < 4 || lens.height() < 4)
        return;
    // The view drawn again, larger about the pointer, into the lens.
    double const dpr = devicePixelRatioF();
    QImage inside((lens.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    inside.setDevicePixelRatio(dpr);
    inside.fill(Qt::transparent);
    double const zoom = m_zoom;
    QPointF const pan = m_pan;
    QPointF const under = toGraph(m_hover);
    m_zoom = zoom * std::clamp(m_options.magnifierScale, 1.0, 100.0);
    m_pan = m_hover - under * m_zoom - lens.topLeft();
    m_inLens = true;
    {
        QPainter lensPainter(&inside);
        paintView(lensPainter);
    }
    m_inLens = false;
    m_zoom = zoom;
    m_pan = pan;
    p.save();
    p.setOpacity(std::clamp(m_options.magnifierOpacity, 0, 100) / 100.0);
    p.drawImage(lens.topLeft(), inside);
    p.setOpacity(1.0);
    p.setPen(QPen(QColor(235, 235, 235), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(lens);
    p.restore();
}

void NodeView::leaveEvent(QEvent* event)
{
    m_hover = QPointF(-1, -1);
    if (m_magnifier)
        update();
    QWidget::leaveEvent(event);
}

// ---- the cable cutter ----------------------------------------------------------------------

bool NodeView::crosses(QVector<QPointF> const& points, QPointF from, QPointF to)
{
    QLineF const cut(from, to);
    for (qsizetype i = 1; i < points.size(); ++i)
        if (QLineF(points[i - 1], points[i]).intersects(cut, nullptr) == QLineF::BoundedIntersection)
            return true;
    return false;
}

void NodeView::onActionCableCutter(bool on)
{
    m_cutter = on;
    if (cableCutter())
        setCursor(cutterCursor());
    else if (!m_spacePan)
        unsetCursor();
    update();
}

int NodeView::cutCables(QPointF from, QPointF to)
{
    Scene const& scene = m_document->scene();
    QVector<int> detach;    // layers whose parenting cable is cut (by id)
    int count = 0;
    if (graphMode()) {
        NodeGraph g = scene.graph;
        QVector<GraphCable> kept;
        for (GraphCable const& c : std::as_const(g.cables)) {
            bool const here = !graphNodeRect(c.from).isNull() && !graphNodeRect(c.to).isNull();
            if (here && crosses(cablePoints(graphOutPort(c.from, c.fromPort), graphInPort(c.to, c.toPort)), from, to))
                ++count;
            else
                kept.push_back(c);
        }
        // The parenting cables too: from a node's right end to the left end of what hangs from it.
        for (GraphNode const& n : g.nodes) {
            if (n.kind != GraphNode::LayerNode || n.group != m_graphGroup)
                continue;
            int const l = scene.layerIndex(n.layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            int const up = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
            if (!up || graphNodeRect(up).isNull())
                continue;
            QPointF const a = graphTransformOut(up), b = graphTransformIn(n.id);
            QPainterPath path(a);
            double const bow = std::max(30.0 * m_zoom, std::abs(b.x() - a.x()) / 2.0);
            path.cubicTo(a + QPointF(bow, 0), b - QPointF(bow, 0), b);
            QVector<QPointF> along;
            for (int i = 0; i <= 24; ++i)
                along << path.pointAtPercent(i / 24.0);
            if (crosses(along, from, to))
                detach << n.layer;
        }
        if (!count && detach.isEmpty())
            return 0;
        m_document->beginMacro(tr("Cut Cables"));
        if (count) {
            g.cables = kept;
            m_document->setGraph(g, tr("Cut Cables"));
        }
    } else {
        for (int l = 0; l < scene.layers.size(); ++l) {
            int const parent = scene.parentIndex(l);
            if (parent < 0 || !nodeShown(l) || !nodeShown(parent))
                continue;
            QRectF const r = nodeRect(l);
            if (crosses(cablePoints(outPort(parent), QPointF(r.center().x(), r.top())), from, to))
                detach << scene.layers[l].id;
        }
        if (detach.isEmpty())
            return 0;
        m_document->beginMacro(tr("Cut Cables"));
    }
    for (int id : std::as_const(detach))
        if (int const l = m_document->scene().layerIndex(id); l >= 0)
            m_document->setParent(l, -1);
    m_document->endMacro();
    m_graphCable = m_cable = -1;
    return count + int(detach.size());
}

// ---- the whole view as a picture -----------------------------------------------------------

QImage NodeView::picture(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(InterfaceColours::get("nodeBackground"));
    // Everything at this level, backdrops and thumbnails too, fitted with a margin.
    QRectF all = graphMode() ? graphLevelBounds() : graphBounds();
    for (int key : shownKeys())
        if (QRectF const t = thumbnailRect(key); !t.isNull())
            all = all.united(QRectF(toGraph(t.topLeft()), toGraph(t.bottomRight())));
    for (Backdrop const& b : backdrops())
        if (b.group == (graphMode() ? m_graphGroup : m_groupId))
            all = all.united(b.rect);
    all.adjust(-30, -30, 30, 30);
    double const zoom = m_zoom;
    QPointF const pan = m_pan;
    m_zoom = std::min(size.width() / std::max(1.0, all.width()), size.height() / std::max(1.0, all.height()));
    m_pan = QPointF(size.width() / 2.0, size.height() / 2.0) - all.center() * m_zoom;
    m_printing = true;
    {
        QPainter p(&image);
        paintView(p);
    }
    m_printing = false;
    m_zoom = zoom;
    m_pan = pan;
    return image;
}

// ---- the mouse, on what both kinds of view have --------------------------------------------

bool NodeView::commonPress(QMouseEvent* event)
{
    m_hover = event->position();
    if (event->button() != Qt::LeftButton)
        return false;
    QPointF const at = event->position();
    if (cableCutter()) {
        m_pressPos = m_dragNow = at;
        m_drag = DragCut;
        return true;
    }
    if (int const key = thumbGripAt(at)) {
        m_pressPos = m_dragNow = at;
        m_drag = DragThumb;
        m_thumbDrag = key;
        m_thumbStart = int(std::lround(thumbnailRect(key).height() / m_zoom));
        return true;
    }
    if (int const key = editorAt(at)) {
        openEditor(key);
        return true;
    }
    return false;
}

bool NodeView::commonMove(QMouseEvent* event)
{
    m_hover = event->position();
    if (m_drag != DragCut && m_drag != DragThumb && m_drag != DragBackdrop) {
        if (m_magnifier)
            update();
        return false;
    }
    m_dragNow = event->position();
    update();
    return true;
}

bool NodeView::commonRelease(QMouseEvent* event)
{
    Drag const drag = m_drag;
    if (drag != DragCut && drag != DragThumb && drag != DragBackdrop)
        return false;
    m_dragNow = event->position();
    if (drag == DragCut) {
        m_drag = DragNone;
        if (QLineF(m_pressPos, m_dragNow).length() >= 3.0) {
            int const cut = cutCables(m_pressPos, m_dragNow);
            emit statusMessage(cut ? tr("%n cable(s) cut.", nullptr, cut) : tr("The line crossed no cable."));
        }
    } else if (drag == DragThumb) {
        int const height = std::clamp(int(std::lround(m_thumbStart - (m_dragNow.y() - m_pressPos.y()) / m_zoom)), 24, 2000);
        int const key = m_thumbDrag;
        m_drag = DragNone;
        m_thumbDrag = 0;
        if (height != m_thumbStart)
            setThumbs({key}, height, tr("Resize Thumbnail"));
    } else {
        // A backdrop let go: it, and the nodes it took with it, where it was put (or so large).
        int const index = m_backdropDrag;
        bool const resize = m_backdropResize;
        m_drag = DragNone;
        m_backdropDrag = -1;
        QPointF const by = (m_dragNow - m_pressPos) / m_zoom;
        if (index >= 0 && index < backdrops().size() && by.manhattanLength() > 0.5) {
            QVector<Backdrop> moved = backdrops();
            if (resize) {
                QRectF& r = moved[index].rect;
                r.setWidth(std::max(60.0, r.width() + by.x()));
                r.setHeight(std::max(40.0, r.height() + by.y()));
                setBackdrops(moved, tr("Resize Backdrop"));
            } else if (graphMode()) {
                NodeGraph g = m_document->scene().graph;
                g.backdrops[index].rect.translate(by);
                for (int key : std::as_const(m_backdropCarry))
                    if (int const k = g.nodeIndex(key); k >= 0)
                        g.nodes[k].at += by;
                m_document->setGraph(g, tr("Move Backdrop"));
            } else {
                QVector<QPointF> const at = positions();
                QVector<int> const carry = m_backdropCarry;
                m_document->editScene(tr("Move Backdrop"), [&](Scene& s) {
                    s.backdrops[index].rect.translate(by);
                    for (int key : carry)
                        if (int const l = s.layerIndex(key); l >= 0) {
                            s.layers[l].nodePos = at[l] + by;
                            s.layers[l].nodePlaced = true;
                        }
                });
            }
        }
        m_backdropCarry.clear();
    }
    update();
    return true;
}

bool NodeView::backdropPress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return false;
    QPointF const at = event->position();
    int const index = backdropAt(at);
    if (index < 0)
        return false;
    QRectF const r = backdropRect(index);
    bool const grip = QRectF(r.bottomRight() - QPointF(14, 14), QSizeF(14, 14)).contains(at);
    bool const title = at.y() <= r.top() + std::max(18.0, (backdrops()[index].titleSize + 12.0) * m_zoom);
    if (!grip && !title)
        return false;   // (the body: as the empty view, a box to pick with)
    bool const add = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (add || !m_backdropPicked.contains(index))
        pickBackdrop(index, add);
    m_pressPos = m_dragNow = at;
    m_drag = DragBackdrop;
    m_backdropDrag = index;
    m_backdropResize = grip;
    // The nodes lying wholly on it go with it.
    m_backdropCarry.clear();
    if (!grip)
        for (int key : shownKeys())
            if (QRectF const n = nodeRectOf(key); !n.isNull() && r.contains(n))
                m_backdropCarry << key;
    update();
    return true;
}

bool NodeView::backdropMenu(QPoint globalPos, QPointF widgetPos)
{
    int const index = backdropAt(widgetPos);
    if (index < 0 || (graphMode() ? graphNodeAt(widgetPos) != 0 : nodeAt(widgetPos) >= 0))
        return false;
    if (!m_backdropPicked.contains(index))
        pickBackdrop(index, false);
    QMenu menu(this);
    connect(menu.addAction(tr("Edit Backdrop...")), &QAction::triggered, this, [this, index] { editBackdrop(index); });
    QMenu* const colours = menu.addMenu(tr("Colour"));
    QVector<QColor> const offered = backdropColours();
    for (int c = 0; c < offered.size(); ++c) {
        QPixmap swatch(14, 14);
        swatch.fill(offered[c]);
        connect(colours->addAction(QIcon(swatch), tr("Colour %1").arg(c + 1)), &QAction::triggered, this, [this, index, colour = offered[c]] {
            QVector<Backdrop> changed = backdrops();
            if (index < changed.size()) {
                changed[index].color = colour;
                setBackdrops(changed, tr("Modify Backdrop"));
            }
        });
    }
    connect(menu.addAction(tr("Remove Backdrop")), &QAction::triggered, this, [this] {
        QVector<Backdrop> left = backdrops();
        QVector<int> gone = m_backdropPicked;
        std::sort(gone.begin(), gone.end(), std::greater<int>());
        for (int i : std::as_const(gone))
            if (i >= 0 && i < left.size())
                left.removeAt(i);
        m_backdropPicked.clear();
        setBackdrops(left, tr("Remove Backdrop"));
    });
    menu.exec(globalPos);
    return true;
}
