// Eclipse - the Node View, with the node graph wired by hand: its nodes and cables drawn and worked with.
#include "ui/views/NodeView.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

namespace {

// Which effects take a colour, and which can be turned inside out.
bool takesColour(Effect::Type t)
{
    return t == Effect::Tint || t == Effect::Glow || t == Effect::Shadow || t == Effect::ColourScale || t == Effect::Tone || t == Effect::Highlight
           || t == Effect::MatteGenerator || t == Effect::Gradient;
}

bool turnsInsideOut(Effect::Type t)
{
    return GraphNode::takesMatte(t) || t == Effect::Gradient || t == Effect::MatteResize;
}

}  // namespace

bool NodeView::graphMode() const
{
    return !m_document->scene().graph.isEmpty();
}

bool NodeView::pictureless(GraphNode const& node) const
{
    // A peg's or a group layer's node: no picture of its own to cable anywhere, only what hangs from it.
    if (node.kind != GraphNode::LayerNode)
        return false;
    Scene const& scene = m_document->scene();
    int const l = scene.layerIndex(node.layer);
    return l >= 0 && (scene.layers[l].kind == Layer::Peg || scene.layers[l].kind == Layer::Group);
}

double NodeView::graphNodeWidth(GraphNode const& node) const
{
    // (Wide enough for its ports to stand apart.)
    return std::max(kNodeWidth, 18.0 * std::max(node.inputs(), node.outputs()) + 30.0);
}

QString NodeView::graphNodeName(GraphNode const& node) const
{
    Scene const& scene = m_document->scene();
    switch (node.kind) {
    case GraphNode::LayerNode: {
        int const l = scene.layerIndex(node.layer);
        return l >= 0 ? scene.layers[l].name : tr("(layer gone)");
    }
    case GraphNode::EffectNode:
        return node.name.isEmpty() ? Effects::name(node.effect, scene).section(QStringLiteral("  "), 0, 0) : node.name;
    case GraphNode::Composite:
        return node.name.isEmpty() ? tr("Composite") : node.name;
    case GraphNode::Group:
        return node.name.isEmpty() ? tr("Group") : node.name;
    case GraphNode::GroupIn:
        return tr("Group Input");
    case GraphNode::GroupOut:
        return tr("Group Output");
    case GraphNode::Display:
        return node.name.isEmpty() ? tr("Display") : node.name;
    default:
        return QString();
    }
}

QRectF NodeView::graphNodeRect(int id) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    if (!n || n->group != m_graphGroup)
        return QRectF();
    return QRectF(toWidget(n->at), QSizeF(graphNodeWidth(*n) * m_zoom, kNodeHeight * m_zoom));
}

QPointF NodeView::graphInPort(int id, int port) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    QRectF const r = graphNodeRect(id);
    if (!n || r.isNull())
        return QPointF();
    return QPointF(r.left() + r.width() * (port + 1.0) / (n->inputs() + 1.0), r.top());
}

QPointF NodeView::graphOutPort(int id, int port) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    QRectF const r = graphNodeRect(id);
    if (!n || r.isNull())
        return QPointF();
    return QPointF(r.left() + r.width() * (port + 1.0) / (n->outputs() + 1.0), r.bottom());
}

QPointF NodeView::graphTransformIn(int id) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    QRectF const r = graphNodeRect(id);
    return n && n->kind == GraphNode::LayerNode && !r.isNull() ? QPointF(r.left(), r.center().y()) : QPointF();
}

QPointF NodeView::graphTransformOut(int id) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    QRectF const r = graphNodeRect(id);
    return n && n->kind == GraphNode::LayerNode && !r.isNull() ? QPointF(r.right(), r.center().y()) : QPointF();
}

QVector<int> NodeView::pickedGraphNodes() const
{
    return m_graphPicked;
}

void NodeView::addPickedGraphNode(int id)
{
    if (!m_document->scene().graph.node(id))
        return;
    if (m_graphPicked.contains(id))
        m_graphPicked.removeAll(id);
    else
        m_graphPicked.push_back(id);
    m_graphNode = m_graphPicked.isEmpty() ? 0 : m_graphPicked.last();
    m_graphCable = -1;
    update();
}

QRectF NodeView::graphLevelBounds() const
{
    QRectF all;
    for (GraphNode const& n : m_document->scene().graph.nodes)
        if (n.group == m_graphGroup)
            all = all.united(QRectF(n.at, QSizeF(graphNodeWidth(n), kNodeHeight)));
    return all;
}

int NodeView::groupGraphNodes(QVector<int> const& ids)
{
    NodeGraph g = m_document->scene().graph;
    QSet<int> inside;
    for (int id : ids)
        if (GraphNode const* const n = g.node(id); n && n->group == m_graphGroup && n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut
                                                    && n->kind != GraphNode::Display)
            inside.insert(id);
    if (inside.isEmpty())
        return 0;
    int const group = g.nextId();
    QPointF middle;
    for (int id : std::as_const(inside))
        middle += g.node(id)->at;
    middle /= double(inside.size());
    // Cables across the edge: those coming in become the group's inputs (one each), those going out its outputs.
    QVector<GraphCable> incoming, outgoing, kept;
    for (GraphCable const& c : std::as_const(g.cables)) {
        bool const from = inside.contains(c.from), to = inside.contains(c.to);
        if (from == to)
            kept.push_back(c);
        else if (to)
            incoming.push_back(c);
        else
            outgoing.push_back(c);
    }
    GraphNode groupNode;
    groupNode.id = group;
    groupNode.kind = GraphNode::Group;
    groupNode.group = m_graphGroup;
    groupNode.at = middle;
    groupNode.ports = std::max<int>(1, int(incoming.size()));
    groupNode.outPorts = std::max<int>(1, int(outgoing.size()));
    GraphNode in;
    in.id = group + 1;
    in.kind = GraphNode::GroupIn;
    in.group = group;
    in.outPorts = groupNode.ports;
    GraphNode out;
    out.id = group + 2;
    out.kind = GraphNode::GroupOut;
    out.group = group;
    out.ports = groupNode.outPorts;
    // (Inside, the input node above what was picked, the output node under it.)
    QRectF reach;
    for (int id : std::as_const(inside))
        reach = reach.united(QRectF(g.node(id)->at, QSizeF(kNodeWidth, kNodeHeight)));
    in.at = QPointF(reach.left(), reach.top() - 90.0);
    out.at = QPointF(reach.left(), reach.bottom() + 60.0);
    for (GraphNode& n : g.nodes)
        if (inside.contains(n.id))
            n.group = group;
    g.nodes += {groupNode, in, out};
    for (int k = 0; k < incoming.size(); ++k) {
        GraphCable const& c = incoming[k];
        kept.push_back({c.from, c.fromPort, group, k});
        kept.push_back({in.id, k, c.to, c.toPort});
    }
    for (int k = 0; k < outgoing.size(); ++k) {
        GraphCable const& c = outgoing[k];
        kept.push_back({c.from, c.fromPort, out.id, k});
        kept.push_back({group, k, c.to, c.toPort});
    }
    g.cables = kept;
    m_document->setGraph(g, tr("Group Selected Nodes"));
    m_graphPicked = {group};
    m_graphNode = group;
    update();
    return group;
}

int NodeView::graphNodeAt(QPointF widgetPos) const
{
    NodeGraph const& g = m_document->scene().graph;
    for (int i = int(g.nodes.size()) - 1; i >= 0; --i)
        if (graphNodeRect(g.nodes[i].id).contains(widgetPos))
            return g.nodes[i].id;
    return 0;
}

NodeView::GraphPort NodeView::graphPortAt(QPointF widgetPos) const
{
    NodeGraph const& g = m_document->scene().graph;
    double const reach = std::max(6.0, 8.0 * m_zoom);
    GraphPort best;
    double nearest = reach;
    for (GraphNode const& n : g.nodes) {
        if (n.group != m_graphGroup)
            continue;
        for (int i = 0; i < n.inputs(); ++i)
            if (double const d = QLineF(graphInPort(n.id, i), widgetPos).length(); d <= nearest) {
                nearest = d;
                best = {n.id, i, false};
            }
        for (int o = 0; o < n.outputs() && !pictureless(n); ++o)
            if (double const d = QLineF(graphOutPort(n.id, o), widgetPos).length(); d <= nearest) {
                nearest = d;
                best = {n.id, o, true};
            }
    }
    return best;
}

int NodeView::graphCableAt(QPointF widgetPos) const
{
    NodeGraph const& g = m_document->scene().graph;
    for (int c = 0; c < g.cables.size(); ++c) {
        GraphCable const& cable = g.cables[c];
        if (graphNodeRect(cable.from).isNull() || graphNodeRect(cable.to).isNull())
            continue;
        QVector<QPointF> const pts = cablePoints(graphOutPort(cable.from, cable.fromPort), graphInPort(cable.to, cable.toPort));
        for (int i = 0; i + 1 < pts.size(); ++i) {
            QLineF const seg(pts[i], pts[i + 1]);
            QPointF const d = seg.p2() - seg.p1();
            double const len2 = QPointF::dotProduct(d, d);
            double const t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(widgetPos - seg.p1(), d) / len2, 0.0, 1.0) : 0.0;
            if (QLineF(seg.p1() + d * t, widgetPos).length() <= 5.0)
                return c;
        }
    }
    return -1;
}

QRectF NodeView::graphPathRect() const
{
    if (!m_graphGroup)
        return QRectF();
    return QRectF(8, 8, std::min(width() - 16.0, 360.0), 24);
}

void NodeView::enterGraphGroup(int id)
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    m_graphGroup = n && n->kind == GraphNode::Group ? id : 0;
    m_graphNode = 0;
    m_graphCable = -1;
    resetGraphView();
}

void NodeView::pickGraphNode(int id)
{
    m_graphNode = id;
    m_graphCable = -1;
    m_graphPicked = id ? QVector<int>{id} : QVector<int>();
    GraphNode const* const n = m_document->scene().graph.node(id);
    if (n && n->kind == GraphNode::LayerNode)
        if (int const l = m_document->scene().layerIndex(n->layer); l >= 0)
            emit layerPicked(l);
    update();
}

void NodeView::pickGraphCable(int index)
{
    m_graphCable = index;
    m_graphNode = 0;
    m_graphPicked.clear();
    update();
}

void NodeView::resetGraphView()
{
    NodeGraph const& g = m_document->scene().graph;
    QRectF all;
    for (GraphNode const& n : g.nodes)
        if (n.group == m_graphGroup)
            all = all.united(QRectF(n.at, QSizeF(graphNodeWidth(n), kNodeHeight)));
    if (all.isNull()) {
        update();
        return;
    }
    all.adjust(-40, -40, 40, 40);
    m_zoom = std::clamp(std::min(width() / all.width(), height() / all.height()), 0.2, 1.5);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - all.center() * m_zoom;
    update();
}

void NodeView::onActionWireGraph()
{
    if (graphMode())
        return;
    m_graphGroup = 0;
    m_document->setGraph(m_document->scene().graphFromLayers(), tr("Wire the Graph by Hand"));
    resetGraphView();
}

void NodeView::onActionStackLayers()
{
    if (!graphMode())
        return;
    m_graphGroup = 0;
    m_graphNode = 0;
    m_graphCable = -1;
    m_document->setGraph(NodeGraph(), tr("Stack the Layers Again"));
    update();
}

int NodeView::addGraphNode(GraphNode node, QPointF graphPos)
{
    NodeGraph g = m_document->scene().graph;
    node.id = g.nextId();
    node.group = m_graphGroup;
    node.at = graphPos;
    g.nodes.push_back(node);
    if (node.kind == GraphNode::Group) {
        // Inside it: what comes in, and what goes out.
        GraphNode in;
        in.id = node.id + 1;
        in.kind = GraphNode::GroupIn;
        in.group = node.id;
        in.outPorts = node.ports;
        GraphNode out;
        out.id = node.id + 2;
        out.kind = GraphNode::GroupOut;
        out.group = node.id;
        out.ports = node.outPorts;
        out.at = QPointF(0.0, 180.0);
        g.nodes += {in, out};
    }
    QString const what = node.kind == GraphNode::EffectNode ? tr("Add Effect Node") : node.kind == GraphNode::Group ? tr("Add Group Node")
                         : node.kind == GraphNode::LayerNode ? tr("Add Layer Node") : tr("Add Node");
    m_document->setGraph(g, what);
    pickGraphNode(node.id);
    return node.id;
}

bool NodeView::connectGraph(int from, int fromPort, int to, int toPort)
{
    NodeGraph g = m_document->scene().graph;
    GraphNode const* const a = g.node(from);
    GraphNode const* const b = g.node(to);
    if (!a || !b || from == to || fromPort < 0 || fromPort >= a->outputs() || toPort < 0 || toPort >= b->inputs() || a->group != b->group)
        return false;
    if (g.feeds(to, from))
        return false;   // (it would go round in a ring)
    g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(), [&](GraphCable const& c) { return c.to == to && c.toPort == toPort; }), g.cables.end());
    g.cables.push_back({from, fromPort, to, toPort});
    m_document->setGraph(g, tr("Connect"));
    return true;
}

void NodeView::changeGraphPorts(int id, bool outputs, int by)
{
    NodeGraph g = m_document->scene().graph;
    int const i = g.nodeIndex(id);
    if (i < 0)
        return;
    GraphNode& n = g.nodes[i];
    bool const group = n.kind == GraphNode::Group;
    if (outputs ? !group : (n.kind != GraphNode::Composite && !group))
        return;
    int& count = outputs ? n.outPorts : n.ports;
    int const now = std::max(1, count + by);
    if (now == count)
        return;
    count = now;
    // What came into a port taken away goes; so does what left a group's output taken away.
    g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(),
                                  [&](GraphCable const& c) { return outputs ? (c.from == id && c.fromPort >= now) : (c.to == id && c.toPort >= now); }),
                   g.cables.end());
    if (group) {
        // Inside: the input node has an output for each input of the group, the output node an input for each output.
        int const inside = outputs ? g.groupOut(id) : g.groupIn(id);
        if (int const k = g.nodeIndex(inside); k >= 0) {
            (outputs ? g.nodes[k].ports : g.nodes[k].outPorts) = now;
            g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(),
                                          [&](GraphCable const& c) { return outputs ? (c.to == inside && c.toPort >= now) : (c.from == inside && c.fromPort >= now); }),
                           g.cables.end());
        }
    }
    m_document->setGraph(g, by > 0 ? (outputs ? tr("Add Output Port") : tr("Add Input Port")) : (outputs ? tr("Remove Output Port") : tr("Remove Input Port")));
}

void NodeView::deleteGraphSelection()
{
    NodeGraph g = m_document->scene().graph;
    if (m_graphCable >= 0 && m_graphCable < g.cables.size()) {
        g.cables.removeAt(m_graphCable);
        m_graphCable = -1;
        m_document->setGraph(g, tr("Disconnect"));
        return;
    }
    // The nodes picked (a group's input and output nodes go only with the group), what is in them, and every cable of those.
    QSet<int> gone;
    QVector<int> picked = m_graphPicked;
    if (picked.isEmpty() && m_graphNode)
        picked = {m_graphNode};
    for (int id : std::as_const(picked))
        if (GraphNode const* const n = g.node(id); n && n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut)
            gone.insert(id);
    if (gone.isEmpty())
        return;
    for (bool more = true; more;) {
        more = false;
        for (GraphNode const& other : std::as_const(g.nodes))
            if (other.group && gone.contains(other.group) && !gone.contains(other.id)) {
                gone.insert(other.id);
                more = true;
            }
    }
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [&](GraphNode const& x) { return gone.contains(x.id); }), g.nodes.end());
    g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(), [&](GraphCable const& c) { return gone.contains(c.from) || gone.contains(c.to); }),
                   g.cables.end());
    m_graphNode = 0;
    m_graphPicked.clear();
    // (The last node gone, the graph is let go: the layers stack as they are.)
    m_document->setGraph(g, tr("Delete Node"));
}

void NodeView::editGraphNode(int id)
{
    NodeGraph g = m_document->scene().graph;
    int const i = g.nodeIndex(id);
    if (i < 0 || g.nodes[i].kind != GraphNode::EffectNode)
        return;
    Effect e = g.nodes[i].effect;
    QDialog dialog(this);
    dialog.setWindowTitle(graphNodeName(g.nodes[i]));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const on = new QCheckBox(&dialog);
    on->setChecked(e.enabled);
    form->addRow(tr("On"), on);
    QDoubleSpinBox* const amount = new QDoubleSpinBox(&dialog);
    amount->setObjectName(QStringLiteral("GraphEffectAmount"));
    amount->setRange(0.0, 1000.0);
    amount->setDecimals(2);
    amount->setValue(g.nodes[i].amountAt(m_frame));
    form->addRow(tr("Amount"), amount);
    // Animated: the amount a keyframe of this frame (the frames between go straight from one keyframe to the next).
    QCheckBox* const keyed = new QCheckBox(tr("Keyframe on frame %1").arg(m_frame + 1), &dialog);
    keyed->setObjectName(QStringLiteral("GraphEffectKeyed"));
    keyed->setChecked(!g.nodes[i].amountKeys.isEmpty());
    form->addRow(QString(), keyed);
    QPushButton* colour = nullptr;
    if (takesColour(e.type)) {
        colour = new QPushButton(e.color.name(), &dialog);
        connect(colour, &QPushButton::clicked, &dialog, [&] {
            QColor const picked = QColorDialog::getColor(e.color, &dialog, tr("Colour"), QColorDialog::ShowAlphaChannel);
            if (picked.isValid()) {
                e.color = picked;
                colour->setText(picked.name());
            }
        });
        form->addRow(tr("Colour"), colour);
    }
    QCheckBox* invert = nullptr;
    if (turnsInsideOut(e.type)) {
        invert = new QCheckBox(&dialog);
        invert->setChecked(e.invert);
        form->addRow(GraphNode::takesMatte(e.type) ? tr("Outside the matte") : tr("Inverted"), invert);
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    e.enabled = on->isChecked();
    if (keyed->isChecked()) {
        g.nodes[i].amountKeys.insert(m_frame, amount->value());
    } else {
        g.nodes[i].amountKeys.clear();
        e.amount = amount->value();
    }
    if (invert)
        e.invert = invert->isChecked();
    g.nodes[i].effect = e;
    m_document->setGraph(g, tr("Edit Effect Node"));
}

// ---- drawing ---------------------------------------------------------------------------------------------------------------

void NodeView::paintGraphMode(QPainter& p)
{
    Scene const& scene = m_document->scene();
    NodeGraph const& g = scene.graph;
    QColor const imageCable(120, 200, 140), picked(255, 220, 90);
    auto cable = [&](QPointF from, QPointF to, QColor const& color, double width) {
        QVector<QPointF> const pts = cablePoints(from, to);
        p.setPen(QPen(color, width));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts.constData(), int(pts.size()));
    };
    // A node being dragged is drawn where the pointer has it, cables and all.
    QPointF const dragBy = m_drag == DragGraphNode && m_moved ? m_dragNow - m_pressPos : QPointF();
    auto shift = [&](int id) { return id == m_graphNode || m_graphPicked.contains(id) ? dragBy : QPointF(); };
    // ---- cables ----
    for (int c = 0; c < g.cables.size(); ++c) {
        GraphCable const& k = g.cables[c];
        if (c == m_unplugged || graphNodeRect(k.from).isNull() || graphNodeRect(k.to).isNull())
            continue;
        cable(graphOutPort(k.from, k.fromPort) + shift(k.from), graphInPort(k.to, k.toPort) + shift(k.to), c == m_graphCable ? picked : imageCable,
              c == m_graphCable ? 2.6 : 1.8);
    }
    if (m_drag == DragWire)
        cable(graphOutPort(m_wireFrom, m_wireFromPort), m_dragNow, picked, 1.8);
    // ---- the parenting of the layers: from a node's right end to the left end of each node that hangs from it ----
    {
        QPen pegPen(QColor(230, 160, 60), 1.6, Qt::DashLine);
        p.setPen(pegPen);
        p.setBrush(Qt::NoBrush);
        for (GraphNode const& n : g.nodes) {
            if (n.kind != GraphNode::LayerNode || n.group != m_graphGroup || n.id == m_pegChild)
                continue;
            int const l = scene.layerIndex(n.layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            int const from = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
            if (!from || graphNodeRect(from).isNull())
                continue;
            QPointF const a = graphTransformOut(from) + shift(from), b = graphTransformIn(n.id) + shift(n.id);
            QPainterPath path(a);
            double const bow = std::max(30.0 * m_zoom, std::abs(b.x() - a.x()) / 2.0);
            path.cubicTo(a + QPointF(bow, 0), b - QPointF(bow, 0), b);
            p.drawPath(path);
        }
        if (m_drag == DragPeg) {
            p.drawLine(graphTransformOut(m_pegFrom), m_dragNow);
        }
    }
    // ---- nodes ----
    QFont font = this->font();
    font.setPointSizeF(std::max(5.0, font.pointSizeF() * m_zoom));
    p.setFont(font);
    for (GraphNode const& n : g.nodes) {
        if (n.group != m_graphGroup)
            continue;
        QRectF const r = graphNodeRect(n.id).translated(shift(n.id));
        QColor const color = n.kind == GraphNode::LayerNode ? QColor(72, 92, 128) : n.kind == GraphNode::Composite ? QColor(96, 76, 128)
                             : n.kind == GraphNode::EffectNode ? QColor(128, 70, 110) : n.kind == GraphNode::Display ? QColor(70, 72, 78)
                                                                                                    : QColor(40, 118, 122);
        bool const off = n.kind == GraphNode::EffectNode && !n.effect.enabled;
        bool const chosen = n.id == m_graphNode || m_graphPicked.contains(n.id);
        p.setPen(QPen(chosen ? QColor(255, 255, 255) : QColor(20, 21, 23), chosen ? 2.0 : 1.0));
        p.setBrush(off ? color.darker(190) : color);
        p.drawRoundedRect(r, 4 * m_zoom, 4 * m_zoom);
        p.setPen(off ? QColor(150, 150, 150) : QColor(245, 245, 245));
        QRectF const text = r.adjusted(8 * m_zoom, 0, -(n.kind == GraphNode::Group ? 18 : 6) * m_zoom, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(graphNodeName(n), Qt::ElideRight, int(text.width())));
        if (n.kind == GraphNode::Group) {
            // A group can be gone into: a small arrow at its right end says so.
            QPointF const tip(r.right() - 10 * m_zoom, r.center().y() + 4 * m_zoom);
            p.setPen(QPen(QColor(235, 235, 235), 1.4));
            p.drawLine(tip + QPointF(0, -8 * m_zoom), tip);
            p.drawLine(tip + QPointF(-3.5 * m_zoom, -3.5 * m_zoom), tip);
            p.drawLine(tip + QPointF(3.5 * m_zoom, -3.5 * m_zoom), tip);
        }
        // Ports: in along the top (filled when something comes in), out along the bottom.
        p.setPen(QPen(QColor(20, 21, 23), 1));
        for (int i = 0; i < n.inputs(); ++i) {
            p.setBrush(g.into(n.id, i) ? QColor(210, 212, 216) : QColor(90, 92, 96));
            p.drawEllipse(graphInPort(n.id, i) + shift(n.id), 3.5 * m_zoom, 3.5 * m_zoom);
        }
        p.setBrush(QColor(210, 212, 216));
        for (int o = 0; o < n.outputs() && !pictureless(n); ++o)
            p.drawEllipse(graphOutPort(n.id, o) + shift(n.id), 4.5 * m_zoom, 4.5 * m_zoom);
        if (n.kind == GraphNode::LayerNode) {
            // Its transformation ports: small squares at its two ends.
            double const s = 3.5 * m_zoom;
            p.setBrush(QColor(230, 160, 60));
            p.drawRect(QRectF(graphTransformIn(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
            p.drawRect(QRectF(graphTransformOut(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
        }
    }
    // The map in the corner: the nodes of this level as small blocks, and what the view shows as a frame.
    if (m_navigator && width() > 320 && height() > 200) {
        QRectF const map = navigatorRect();
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(44, 44, 46, 230));
        p.drawRect(map);
        p.setClipRect(map.adjusted(1, 1, -1, -1));
        QRectF const all = graphLevelBounds().adjusted(-20, -20, 20, 20), inner = map.adjusted(5, 5, -5, -5);
        double const k = std::min(inner.width() / std::max(all.width(), 1.0), inner.height() / std::max(all.height(), 1.0));
        auto onMap = [&](QPointF graph) { return inner.center() + (graph - all.center()) * k; };
        p.setPen(Qt::NoPen);
        for (GraphNode const& n : g.nodes) {
            if (n.group != m_graphGroup)
                continue;
            p.setBrush(n.id == m_graphNode ? QColor(72, 190, 200) : QColor(150, 152, 158));
            p.drawRect(QRectF(onMap(n.at), QSizeF(std::max(3.0, graphNodeWidth(n) * k), std::max(2.0, kNodeHeight * k))));
        }
        p.setPen(QPen(QColor(235, 235, 235), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(onMap(toGraph(QPointF(0, 0))), onMap(toGraph(QPointF(width(), height())))));
        p.setClipping(false);
    }
    // Inside a group: the way here along the top; a click on it goes out one level.
    if (QRectF const path = graphPathRect(); !path.isNull()) {
        QStringList names;
        for (GraphNode const* at = g.node(m_graphGroup); at; at = g.node(at->group))
            names.prepend(graphNodeName(*at));
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(40, 118, 122));
        p.drawRoundedRect(path, 4, 4);
        p.setPen(QColor(245, 245, 245));
        p.drawText(path.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("↑  ") + tr("Top") + QStringLiteral(" / ") + names.join(QStringLiteral(" / ")));
    }
}

// ---- input -----------------------------------------------------------------------------------------------------------------

void NodeView::graphPress(QMouseEvent* event)
{
    m_pressPos = m_dragNow = event->position();
    m_moved = false;
    if (event->button() == Qt::MiddleButton) {
        m_drag = DragPan;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (graphPathRect().contains(m_pressPos)) {
        // Out one level.
        GraphNode const* const n = m_document->scene().graph.node(m_graphGroup);
        enterGraphGroup(n ? n->group : 0);
        return;
    }
    if (m_navigator && width() > 320 && height() > 200 && navigatorRect().contains(m_pressPos)) {
        // On the map: that part of the graph comes to the middle of the view.
        QRectF const all = graphLevelBounds().adjusted(-20, -20, 20, 20), inner = navigatorRect().adjusted(5, 5, -5, -5);
        double const k = std::min(inner.width() / std::max(all.width(), 1.0), inner.height() / std::max(all.height(), 1.0));
        QPointF const graph = all.center() + (m_pressPos - inner.center()) / k;
        m_pan = QPointF(width() / 2.0, height() / 2.0) - graph * m_zoom;
        update();
        return;
    }
    NodeGraph const& g = m_document->scene().graph;
    {
        // A transformation port: out of a node's right end, what is to hang from it; a cable taken off a node's left end,
        // to hang it elsewhere (or from nothing).
        Scene const& scene = m_document->scene();
        double const reach = std::max(6.0, 8.0 * m_zoom);
        for (GraphNode const& n : g.nodes) {
            if (n.kind != GraphNode::LayerNode || n.group != m_graphGroup)
                continue;
            if (QLineF(graphTransformOut(n.id), m_pressPos).length() <= reach) {
                m_drag = DragPeg;
                m_pegFrom = n.id;
                m_pegChild = 0;
                return;
            }
            int const l = scene.layerIndex(n.layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            int const from = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
            if (from && QLineF(graphTransformIn(n.id), m_pressPos).length() <= reach) {
                m_drag = DragPeg;
                m_pegFrom = from;
                m_pegChild = n.id;
                update();
                return;
            }
        }
    }
    GraphPort const port = graphPortAt(m_pressPos);
    if (port.node && port.out) {
        // Out of an output: a cable pulled to an input.
        m_drag = DragWire;
        m_wireFrom = port.node;
        m_wireFromPort = port.port;
        m_unplugged = -1;
        return;
    }
    if (port.node && !port.out) {
        // An input with a cable in it: the cable taken off, to be plugged in elsewhere (or let go of: unplugged).
        for (int c = 0; c < g.cables.size(); ++c)
            if (g.cables[c].to == port.node && g.cables[c].toPort == port.port) {
                m_drag = DragWire;
                m_wireFrom = g.cables[c].from;
                m_wireFromPort = g.cables[c].fromPort;
                m_unplugged = c;
                update();
                return;
            }
    }
    if (int const hit = graphNodeAt(m_pressPos)) {
        if (event->modifiers() & Qt::ControlModifier) {
            addPickedGraphNode(hit);
            return;
        }
        if (!m_graphPicked.contains(hit))
            pickGraphNode(hit);
        else
            m_graphNode = hit;
        m_drag = DragGraphNode;
        return;
    }
    if (int const c = graphCableAt(m_pressPos); c >= 0) {
        pickGraphCable(c);
        return;
    }
    m_graphNode = 0;
    m_graphCable = -1;
    m_graphPicked.clear();
    m_drag = DragPan;     // empty space: move the view
    update();
}

void NodeView::graphMove(QMouseEvent* event)
{
    QPointF const now = event->position();
    if (m_drag == DragPan)
        m_pan += now - m_dragNow;
    else if (m_drag == DragGraphNode)
        m_moved = m_moved || (now - m_pressPos).manhattanLength() >= 4.0;
    m_dragNow = now;
    if (m_drag != DragNone)
        update();
}

void NodeView::graphRelease(QMouseEvent* event)
{
    Drag const drag = m_drag;
    m_drag = DragNone;
    QPointF const at = event->position();
    if (drag == DragGraphNode && m_moved) {
        NodeGraph g = m_document->scene().graph;
        QVector<int> moving = m_graphPicked;
        if (!moving.contains(m_graphNode))
            moving.push_back(m_graphNode);
        for (int id : std::as_const(moving))
            if (int const i = g.nodeIndex(id); i >= 0)
                g.nodes[i].at += (at - m_pressPos) / m_zoom;
        m_document->setGraph(g, moving.size() > 1 ? tr("Move Nodes") : tr("Move Node"));
    } else if (drag == DragPeg) {
        // Onto a layer's node: that layer hangs from the one the cable came out of.  Taken off and let go of in the open: it
        // hangs from nothing.
        Scene const& scene = m_document->scene();
        NodeGraph const& g = scene.graph;
        GraphNode const* const from = g.node(m_pegFrom);
        GraphNode const* const onto = g.node(graphNodeAt(at));
        int const parent = from ? scene.layerIndex(from->layer) : -1;
        int const was = m_pegChild ? scene.layerIndex(g.node(m_pegChild)->layer) : -1;
        int const target = onto && onto->kind == GraphNode::LayerNode && onto->id != m_pegFrom ? scene.layerIndex(onto->layer) : -1;
        m_pegChild = 0;
        if (target >= 0 && parent >= 0) {
            m_document->beginMacro(tr("Attach Layer"));
            if (was >= 0 && was != target)
                m_document->setParent(was, -1);
            m_document->setParent(target, parent);
            m_document->endMacro();
        } else if (was >= 0 && !onto) {
            m_document->setParent(was, -1);
        }
    } else if (drag == DragWire) {
        NodeGraph g = m_document->scene().graph;
        int const unplugged = m_unplugged;
        m_unplugged = -1;
        if (unplugged >= 0 && unplugged < g.cables.size())
            g.cables.removeAt(unplugged);
        // Onto an input; or onto a node that can take one more input (it gets it).
        GraphPort target = graphPortAt(at);
        if (!target.node || target.out) {
            target = GraphPort();
            if (int const onto = graphNodeAt(at)) {
                GraphNode const* const n = g.node(onto);
                if (n && (n->kind == GraphNode::Composite || n->kind == GraphNode::Group || n->kind == GraphNode::GroupOut)) {
                    int free = -1;
                    for (int i = 0; i < n->inputs() && free < 0; ++i)
                        if (!g.into(onto, i))
                            free = i;
                    if (free < 0) {
                        int const k = g.nodeIndex(onto);
                        g.nodes[k].ports += 1;
                        free = g.nodes[k].ports - 1;
                        if (n->kind == GraphNode::Group)
                            if (int const inside = g.nodeIndex(g.groupIn(onto)); inside >= 0)
                                g.nodes[inside].outPorts = g.nodes[k].ports;
                        if (n->kind == GraphNode::GroupOut)
                            if (int const outer = g.nodeIndex(n->group); outer >= 0)
                                g.nodes[outer].outPorts = g.nodes[k].ports;
                    }
                    target = {onto, free, false};
                }
            }
        }
        GraphNode const* const from = g.node(m_wireFrom);
        GraphNode const* const to = g.node(target.node);
        bool const fits = from && to && target.node != m_wireFrom && from->group == to->group && !g.feeds(target.node, m_wireFrom);
        if (fits) {
            g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(), [&](GraphCable const& c) { return c.to == target.node && c.toPort == target.port; }),
                           g.cables.end());
            g.cables.push_back({m_wireFrom, m_wireFromPort, target.node, target.port});
            m_document->setGraph(g, unplugged >= 0 ? tr("Move Cable") : tr("Connect"));
        } else if (unplugged >= 0 && !to) {
            m_document->setGraph(g, tr("Disconnect"));     // let go of in the open: unplugged
        }
    }
    update();
}

void NodeView::graphDoubleClick(QMouseEvent* event)
{
    int const hit = graphNodeAt(event->position());
    GraphNode const* const n = m_document->scene().graph.node(hit);
    if (!n)
        return;
    if (n->kind == GraphNode::Group) {
        enterGraphGroup(hit);
    } else if (n->kind == GraphNode::EffectNode) {
        editGraphNode(hit);
    } else if (n->kind == GraphNode::LayerNode) {
        if (int const l = m_document->scene().layerIndex(n->layer); l >= 0) {
            if (m_doubleClickEdits)
                emit editRequested(l);
            else
                emit renameRequested(l);
        }
    } else if (n->kind == GraphNode::Composite || n->kind == GraphNode::Display) {
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Node"), tr("Name:"), QLineEdit::Normal, graphNodeName(*n), &ok);
        if (ok) {
            NodeGraph g = m_document->scene().graph;
            g.nodes[g.nodeIndex(hit)].name = name;
            m_document->setGraph(g, tr("Rename Node"));
        }
    }
}

void NodeView::graphContextMenu(QPoint globalPos, QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    NodeGraph const& g = scene.graph;
    QPointF const graphPos = toGraph(widgetPos);
    int const hit = graphNodeAt(widgetPos);
    if (hit)
        pickGraphNode(hit);
    QMenu menu(this);
    QMenu* const add = menu.addMenu(tr("Add Node"));
    auto adder = [&](QString const& text, GraphNode::Kind kind) {
        connect(add->addAction(text), &QAction::triggered, this, [this, kind, graphPos] {
            GraphNode n;
            n.kind = kind;
            addGraphNode(n, graphPos);
        });
    };
    adder(tr("Composite"), GraphNode::Composite);
    adder(tr("Group"), GraphNode::Group);
    if (m_graphGroup == 0)
        adder(tr("Display"), GraphNode::Display);
    QMenu* const effects = add->addMenu(tr("Effect"));
    for (int t = 0; t < Effect::TypeCount; ++t) {
        if (t == Effect::Particles)
            continue;   // (particles are thrown from a layer's drawing: they stay a layer effect)
        Effect const e = Effect::made(Effect::Type(t));
        connect(effects->addAction(Effects::name(e, scene).section(QStringLiteral("  "), 0, 0)), &QAction::triggered, this, [this, e, graphPos] {
            GraphNode n;
            n.kind = GraphNode::EffectNode;
            n.effect = e;
            addGraphNode(n, graphPos);
        });
    }
    QMenu* const layerMenu = add->addMenu(tr("Layer"));
    for (Layer const& layer : scene.layers) {
        if (layer.kind == Layer::Peg || layer.kind == Layer::Camera || layer.kind == Layer::Sound || layer.kind == Layer::Group || layer.kind == Layer::Bone)
            continue;
        if (g.layerNode(layer.id))
            continue;   // (a layer has one node)
        int const id = layer.id;
        connect(layerMenu->addAction(layer.name), &QAction::triggered, this, [this, id, graphPos] {
            GraphNode n;
            n.kind = GraphNode::LayerNode;
            n.layer = id;
            addGraphNode(n, graphPos);
        });
    }
    layerMenu->setEnabled(!layerMenu->isEmpty());
    if (GraphNode const* const n = g.node(hit)) {
        menu.addSeparator();
        if (n->kind == GraphNode::Composite || n->kind == GraphNode::Group) {
            connect(menu.addAction(tr("Add Input Port")), &QAction::triggered, this, [this, hit] { changeGraphPorts(hit, false, 1); });
            connect(menu.addAction(tr("Remove Input Port")), &QAction::triggered, this, [this, hit] { changeGraphPorts(hit, false, -1); });
        }
        if (n->kind == GraphNode::Group) {
            connect(menu.addAction(tr("Add Output Port")), &QAction::triggered, this, [this, hit] { changeGraphPorts(hit, true, 1); });
            connect(menu.addAction(tr("Remove Output Port")), &QAction::triggered, this, [this, hit] { changeGraphPorts(hit, true, -1); });
            connect(menu.addAction(tr("Enter Group")), &QAction::triggered, this, [this, hit] { enterGraphGroup(hit); });
        }
        if (n->kind == GraphNode::EffectNode)
            connect(menu.addAction(tr("Edit Effect...")), &QAction::triggered, this, [this, hit] { editGraphNode(hit); });
        if (n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut)
            connect(menu.addAction(tr("Delete Node")), &QAction::triggered, this, [this] { deleteGraphSelection(); });
    }
    if (!m_graphPicked.isEmpty()) {
        menu.addSeparator();
        QVector<int> const picked = m_graphPicked;
        connect(menu.addAction(tr("Group Selected Nodes")), &QAction::triggered, this, [this, picked] { groupGraphNodes(picked); });
    }
    menu.addSeparator();
    if (m_graphGroup)
        connect(menu.addAction(tr("Exit Group")), &QAction::triggered, this, [this] {
            GraphNode const* const at = m_document->scene().graph.node(m_graphGroup);
            enterGraphGroup(at ? at->group : 0);
        });
    connect(menu.addAction(tr("Reset View")), &QAction::triggered, this, [this] { resetGraphView(); });
    connect(menu.addAction(tr("Stack the Layers Again")), &QAction::triggered, this, &NodeView::onActionStackLayers);
    menu.exec(globalPos);
}
