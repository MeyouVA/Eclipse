// Eclipse - the Node View's graph: nodes, ports, cables, groups - finding, picking, adding, connecting, deleting.
#include "ui/views/NodeView.h"

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

#include "model/document/Commands.h"
#include "ui/panels/EffectsPanel.h"

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
    if (node.kind == GraphNode::Waypoint)
        return 16.0;
    // (Wide enough for its ports to stand apart.)
    return std::max(kNodeWidth, 18.0 * std::max(node.inputs(), node.outputs()) + 30.0);
}

QString NodeView::graphNodeName(GraphNode const& node) const
{
    Scene const& scene = m_document->scene();
    switch (node.kind) {
    case GraphNode::LayerNode: {
        int const l = scene.layerIndex(node.layer);
        QString name = l >= 0 ? scene.layers[l].name : tr("(layer gone)");
        if ((node.sheets & 0xf) != 0xf) {
            // (Only some of its art layers.)
            QStringList parts;
            QString const names[4] = {tr("Underlay"), tr("Colour Art"), tr("Line Art"), tr("Overlay")};
            for (int sheet = 0; sheet < 4; ++sheet)
                if (node.sheets & (1 << sheet))
                    parts << names[sheet];
            name += QStringLiteral(" (") + (parts.isEmpty() ? tr("nothing") : parts.join(QStringLiteral(", "))) + QLatin1Char(')');
        }
        return name;
    }
    case GraphNode::Note:
        return node.name.isEmpty() ? tr("Note") : node.name;
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
    case GraphNode::Waypoint:
        return node.name.isEmpty() ? tr("Waypoint") : node.name;
    default:
        return QString();
    }
}

QRectF NodeView::graphNodeRect(int id) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    if (!n || n->group != m_graphGroup)
        return QRectF();
    return QRectF(toWidget(n->at), QSizeF(graphNodeWidth(*n) * m_zoom, (n->kind == GraphNode::Waypoint ? 16.0 : kNodeHeight) * m_zoom));
}

QPointF NodeView::graphInPort(int id, int port) const
{
    GraphNode const* const n = m_document->scene().graph.node(id);
    QRectF const r = graphNodeRect(id);
    if (!n || r.isNull())
        return QPointF();
    // (A composite's inputs may run from the right, as the preferences say.)
    int const slot = m_options.portsRightToLeft && n->kind == GraphNode::Composite ? n->inputs() - 1 - port : port;
    return QPointF(r.left() + r.width() * (slot + 1.0) / (n->inputs() + 1.0), r.top());
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
    // (One output for each picture leaving, however many nodes outside take it.)
    QVector<QPair<int, int>> leaving;
    for (GraphCable const& c : std::as_const(outgoing))
        if (!leaving.contains(qMakePair(c.from, c.fromPort)))
            leaving.push_back(qMakePair(c.from, c.fromPort));
    GraphNode groupNode;
    groupNode.id = group;
    groupNode.kind = GraphNode::Group;
    groupNode.group = m_graphGroup;
    groupNode.at = middle;
    groupNode.ports = std::max<int>(1, int(incoming.size()));
    groupNode.outPorts = std::max<int>(1, int(leaving.size()));
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
    for (int k = 0; k < leaving.size(); ++k)
        kept.push_back({leaving[k].first, leaving[k].second, out.id, k});
    for (GraphCable const& c : std::as_const(outgoing))
        kept.push_back({group, int(leaving.indexOf(qMakePair(c.from, c.fromPort))), c.to, c.toPort});
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
        if (cableContains(pts, widgetPos, 5.0))
            return c;
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
    m_graphPicked.clear();
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
    m_graphPicked.clear();
    m_document->setGraph(NodeGraph(), tr("Stack the Layers Again"));
    update();
}

void NodeView::onActionGlueSelection()
{
    QVector<int> const picked = pickedGraphNodes();
    NodeGraph g = m_document->scene().graph;
    GraphNode const* const first = picked.size() == 2 ? g.node(picked[0]) : nullptr;
    GraphNode const* const second = picked.size() == 2 ? g.node(picked[1]) : nullptr;
    if (!graphMode() || !first || !second || first->group != second->group || first->outputs() < 1 || second->outputs() < 1) {
        emit statusMessage(tr("No selection to glue: pick two nodes in the same group."));
        return;
    }
    GraphNode glue;
    glue.id = g.nextId();
    glue.kind = GraphNode::EffectNode;
    glue.effect = Effect::made(Effect::Glue);
    glue.group = first->group;
    glue.at = (first->at + second->at) / 2.0 + QPointF(0.0, 90.0);
    int const a = first->id, b = second->id;
    // What the second fed now comes from the glue; the first goes into the glue only.
    for (GraphCable& c : g.cables)
        if (c.from == b && c.fromPort == 0)
            c.from = glue.id;
    g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(), [a](GraphCable const& c) { return c.from == a && c.fromPort == 0; }), g.cables.end());
    g.nodes.push_back(glue);
    g.cables.push_back({a, 0, glue.id, 0});
    g.cables.push_back({b, 0, glue.id, 1});
    m_document->setGraph(g, tr("Glue Selection"));
    pickGraphNode(glue.id);
    update();
}

int NodeView::addGraphNode(GraphNode node, QPointF graphPos)
{
    NodeGraph g = m_document->scene().graph;
    node.id = g.nextId();
    node.group = m_graphGroup;
    node.at = graphPos;
    if (node.kind == GraphNode::Composite && m_options.compositePassThrough)
        node.passThrough = true;
    if (node.kind == GraphNode::EffectNode && node.effect.type == Effect::ImageSwitch)
        node.ports = std::max(2, node.ports);
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
    if (!b->accepts(toPort, a->portNames.value(fromPort))) {
        emit statusMessage(tr("That input takes only outputs named %1.").arg(b->portFilters.value(toPort)));
        return false;
    }
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
    bool const switcher = n.kind == GraphNode::EffectNode && n.effect.type == Effect::ImageSwitch;
    if (outputs ? !group : (n.kind != GraphNode::Composite && !group && !switcher))
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
