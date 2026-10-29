// Eclipse - the Node View's commands: nodes switched on and off, gone round, grouped, arranged, cloned, copied and pasted.
#include "ui/views/NodeView.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"

namespace {

// What Cut and Copy keep: the layers as a piece of scene and, in a graph wired by hand, the nodes and the cables among them.
struct NodeClip {
    bool any = false;
    bool graph = false;
    Scene piece;
    QVector<int> layerIds;          // the ids the piece's layers had, in the piece's order
    QHash<int, QPointF> layerAt;    // where each copied layer's node stood (layer mode), by layer id
    NodeGraph nodes;                // the nodes copied (a graph wired by hand), with the cables among them
};

NodeClip& nodeClip()
{
    static NodeClip clip;
    return clip;
}

// Names and filters of ports, copied to give to other nodes.
struct PortClip {
    bool any = false;
    QMap<int, QString> names;
    QMap<int, QString> filters;
};

PortClip& portClip()
{
    static PortClip clip;
    return clip;
}

// The keys of the Node View's own shortcuts when no window gives them (shortcut id, key).
struct OwnKey {
    char const* id;
    char const* key;
};

OwnKey const kOwnKeys[] = {
    {"shortcut.nodeEnable", "A"},
    {"shortcut.nodeDisable", "D"},
    {"shortcut.nodeCreateDisplay", ""},
    {"shortcut.nodeCentreSelection", "O"},
    {"shortcut.nodeMagnifier", "Z"},
    {"shortcut.nodeShowAll", "Shift+."},
    {"shortcut.nodeFindInsert", "Return"},
    {"shortcut.nodeEnterGroup", "Ctrl+Return"},
    {"shortcut.nodeExitGroup", "Ctrl+Shift+Return"},
    {"shortcut.nodeMoveToParent", ""},
    {"shortcut.nodeCreateGroup", "Alt+G"},
    {"shortcut.nodeGroup", "Ctrl+G"},
    {"shortcut.nodeCreateWaypoint", ""},
    {"shortcut.nodeRenameWaypoint", ""},
    {"shortcut.nodeCableCutter", ""},
    {"shortcut.nodeCableCutterOverride", ""},
    {"shortcut.nodeAddPortName", "+"},
    {"shortcut.nodeCentreParent", "Ctrl+Up"},
    {"shortcut.nodeCentreChild", "Ctrl+Down"},
    {"shortcut.nodeCopyQualifiedName", "Ctrl+Q"},
    {"shortcut.nodeCreateBackdrop", ""},
    {"shortcut.nodeToggleThumbnails", "T"},
    {"shortcut.nodeHideAllThumbnails", "Alt+T"},
    {"shortcut.nodeOrderUp", ""},
    {"shortcut.nodeOrderDown", ""},
    {"shortcut.nodeAlignHorizontally", ""},
    {"shortcut.nodeExplodeGroups", ""},
    {"shortcut.nodeShowNavigator", ""},
};

// A shifted symbol typed (">") as the key under it with Shift (".").
int unshifted(int key)
{
    switch (key) {
    case Qt::Key_Greater: return Qt::Key_Period;
    case Qt::Key_Less: return Qt::Key_Comma;
    case Qt::Key_Question: return Qt::Key_Slash;
    case Qt::Key_Colon: return Qt::Key_Semicolon;
    case Qt::Key_Underscore: return Qt::Key_Minus;
    default: return key;
    }
}

}  // namespace

bool NodeView::matchesShortcut(QKeyEvent* event, QString const& id) const
{
    QKeySequence wanted;
    if (m_shortcutLookup) {
        wanted = m_shortcutLookup(id);
    } else {
        for (OwnKey const& own : kOwnKeys)
            if (id == QLatin1String(own.id))
                wanted = QKeySequence(QString::fromLatin1(own.key));
    }
    if (wanted.isEmpty() || wanted.count() != 1)
        return false;
    int const key = event->key();
    if (key == Qt::Key_unknown || key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return false;
    Qt::KeyboardModifiers const mods = event->modifiers() & ~Qt::KeypadModifier;
    QKeyCombination const want = wanted[0];
    int const typed = key == Qt::Key_Enter ? int(Qt::Key_Return) : key;
    if (want == QKeyCombination(mods, Qt::Key(typed)))
        return true;
    // A symbol typed with Shift ("+" or ">") matches the key as written, with or without the Shift said.
    if ((mods & Qt::ShiftModifier) && want == QKeyCombination(mods & ~Qt::ShiftModifier, Qt::Key(typed)))
        return true;
    return (mods & Qt::ShiftModifier) && want == QKeyCombination(mods, Qt::Key(unshifted(typed)));
}

bool NodeView::handleOwnKeys(QKeyEvent* event, bool dryRun)
{
    using Slot = void (NodeView::*)();
    struct Bound {
        char const* id;
        Slot slot;
    };
    static Bound const bound[] = {
        {"shortcut.nodeEnable", &NodeView::onActionEnableNodes},
        {"shortcut.nodeDisable", &NodeView::onActionDisableNodes},
        {"shortcut.nodeCreateDisplay", &NodeView::onActionCreateDisplay},
        {"shortcut.nodeCentreSelection", &NodeView::onActionCentreOnSelection},
        {"shortcut.nodeShowAll", &NodeView::onActionResetView},
        {"shortcut.nodeFindInsert", &NodeView::onActionFindAndInsert},
        {"shortcut.nodeEnterGroup", &NodeView::onActionEnterGroup},
        {"shortcut.nodeExitGroup", &NodeView::onActionExitGroup},
        {"shortcut.nodeMoveToParent", &NodeView::onActionMoveToParentGroup},
        {"shortcut.nodeCreateGroup", &NodeView::onActionCreateGroup},
        {"shortcut.nodeGroup", &NodeView::onActionGroupSelection},
        {"shortcut.nodeCreateWaypoint", &NodeView::onActionCreateWaypoint},
        {"shortcut.nodeRenameWaypoint", &NodeView::onActionRenameWaypoint},
        {"shortcut.nodeAddPortName", &NodeView::onActionAddPortName},
        {"shortcut.nodeCentreParent", &NodeView::onActionCentreOnParent},
        {"shortcut.nodeCentreChild", &NodeView::onActionCentreOnChild},
        {"shortcut.nodeCopyQualifiedName", &NodeView::onActionCopyQualifiedName},
        {"shortcut.nodeCreateBackdrop", &NodeView::onActionCreateBackdrop},
        {"shortcut.nodeToggleThumbnails", &NodeView::onActionToggleSelectedThumbnails},
        {"shortcut.nodeHideAllThumbnails", &NodeView::onActionHideAllThumbnails},
        {"shortcut.nodeOrderUp", &NodeView::onActionOrderUp},
        {"shortcut.nodeOrderDown", &NodeView::onActionOrderDown},
        {"shortcut.nodeAlignHorizontally", &NodeView::onActionAlignHorizontally},
        {"shortcut.nodeExplodeGroups", &NodeView::onActionExplodeGroups},
        {"shortcut.nodeCreateComposite", &NodeView::onActionCreateComposite},
        {"shortcut.nodeGroupComposite", &NodeView::onActionGroupWithComposite},
    };
    for (Bound const& b : bound)
        if (matchesShortcut(event, QLatin1String(b.id))) {
            if (!dryRun)
                (this->*b.slot)();
            return true;
        }
    // The switches: the magnifier, the cable cutter (and, held, its override), the navigator.
    for (char const* id : {"shortcut.nodeMagnifier", "shortcut.nodeCableCutter", "shortcut.nodeShowNavigator", "shortcut.nodeCableCutterOverride"})
        if (matchesShortcut(event, QLatin1String(id))) {
            if (dryRun)
                return true;
            QString const which = QLatin1String(id);
            if (which.endsWith(QLatin1String("Magnifier")))
                onActionMagnifier(!m_magnifier);
            else if (which.endsWith(QLatin1String("Override"))) {
                if (!event->isAutoRepeat()) {
                    m_cutterHeld = true;
                    onActionCableCutter(m_cutter);
                }
            } else if (which.endsWith(QLatin1String("CableCutter")))
                onActionCableCutter(!m_cutter);
            else
                onActionShowNavigator(!m_navigator);
            return true;
        }
    return false;
}

// ---- switched on and off -------------------------------------------------------------------

bool NodeView::nodeEnabled(int id) const
{
    Scene const& scene = m_document->scene();
    if (!graphMode()) {
        int const l = scene.layerIndex(id);
        return l >= 0 && scene.layers[l].visible;
    }
    GraphNode const* const n = scene.graph.node(id);
    if (!n)
        return false;
    if (n->kind == GraphNode::LayerNode) {
        int const l = scene.layerIndex(n->layer);
        return l >= 0 && scene.layers[l].visible;
    }
    return n->kind == GraphNode::EffectNode ? n->effect.enabled : n->enabled;
}

QVector<int> NodeView::pickedKeys() const
{
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        QVector<int> keys = m_graphPicked;
        if (keys.isEmpty() && m_graphNode)
            keys = {m_graphNode};
        return keys;
    }
    QVector<int> keys;
    for (int l : pickedLayers())
        keys << scene.layers[l].id;
    if (keys.isEmpty() && m_layer >= 0 && m_layer < scene.layers.size() && nodeShown(m_layer))
        keys << scene.layers[m_layer].id;
    return keys;
}

void NodeView::switchNodes(QVector<int> const& keys, bool on, QString const& text)
{
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    NodeGraph g = scene.graph;
    bool graphChanged = false;
    for (int key : keys) {
        if (!graphMode()) {
            int const l = scene.layerIndex(key);
            if (l >= 0 && scene.layers[l].visible != on)
                layers << l;
            continue;
        }
        int const i = g.nodeIndex(key);
        if (i < 0)
            continue;
        GraphNode& n = g.nodes[i];
        if (n.kind == GraphNode::LayerNode) {
            int const l = scene.layerIndex(n.layer);
            if (l >= 0 && scene.layers[l].visible != on && !layers.contains(l))
                layers << l;
        } else if (n.kind == GraphNode::EffectNode && n.effect.enabled != on) {
            n.effect.enabled = on;
            graphChanged = true;
        } else if ((n.kind == GraphNode::Composite || n.kind == GraphNode::Group || n.kind == GraphNode::Waypoint) && n.enabled != on) {
            n.enabled = on;
            graphChanged = true;
        }
    }
    if (layers.isEmpty() && !graphChanged)
        return;
    m_document->beginMacro(text);
    if (graphChanged)
        m_document->setGraph(g, text);
    if (!layers.isEmpty())
        m_document->editLayers(layers, text, [on](Layer& layer) { layer.visible = on; });
    m_document->endMacro();
}

void NodeView::onActionEnableNodes()
{
    switchNodes(pickedKeys(), true, tr("Enable"));
}

void NodeView::onActionDisableNodes()
{
    switchNodes(pickedKeys(), false, tr("Disable"));
}

void NodeView::onActionEnableAll()
{
    QVector<int> all;
    if (graphMode())
        for (GraphNode const& n : m_document->scene().graph.nodes)
            all << n.id;
    else
        for (Layer const& l : m_document->scene().layers)
            all << l.id;
    switchNodes(all, true, tr("Enable All"));
}

void NodeView::onActionDisableAllOthers()
{
    Scene const& scene = m_document->scene();
    QVector<int> const picked = pickedKeys();
    if (picked.isEmpty())
        return;
    // Everything but what is picked, and the groups it is in (else it would go dark with them).
    QSet<int> keep(picked.cbegin(), picked.cend());
    QVector<int> others;
    if (graphMode()) {
        for (int id : picked)
            for (GraphNode const* n = scene.graph.node(id); n && n->group; n = scene.graph.node(n->group))
                keep.insert(n->group);
        for (GraphNode const& n : scene.graph.nodes)
            if (!keep.contains(n.id))
                others << n.id;
    } else {
        for (int id : picked)
            for (int up = scene.parentIndex(scene.layerIndex(id)), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
                if (scene.layers[up].kind == Layer::Group)
                    keep.insert(scene.layers[up].id);
        for (Layer const& l : scene.layers)
            if (!keep.contains(l.id))
                others << l.id;
    }
    switchNodes(others, false, tr("Disable All Others"));
}

// ---- going round the graph -----------------------------------------------------------------

int NodeView::parentOf(int node, bool skipEffects) const
{
    Scene const& scene = m_document->scene();
    if (!graphMode())
        return node >= 0 && node < scene.layers.size() ? scene.parentIndex(node) : -1;
    NodeGraph const& g = scene.graph;
    int at = node;
    for (int guard = 0; guard < 256; ++guard) {
        GraphNode const* const n = g.node(at);
        if (!n)
            return 0;
        // Up the first input that has a cable in it; a layer without one goes to the layer it hangs from.
        int up = 0, port = 0;
        for (int in = 0; in < n->inputs() && !up; ++in)
            if (GraphCable const* const c = g.into(at, in)) {
                up = c->from;
                port = c->fromPort;
            }
        if (!up && n->kind == GraphNode::LayerNode) {
            int const l = scene.layerIndex(n->layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            up = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
        }
        GraphNode const* const u = g.node(up);
        if (!u)
            return 0;
        if (u->kind == GraphNode::GroupIn) {
            // Out of the group: to what comes into that input of it.
            GraphCable const* const outer = g.into(u->group, port);
            if (!outer)
                return u->group;
            up = outer->from;
        }
        GraphNode const* const found = g.node(up);
        if (found && (found->kind == GraphNode::Waypoint || (skipEffects && found->kind == GraphNode::EffectNode))) {
            at = up;
            continue;
        }
        return up;
    }
    return 0;
}

int NodeView::childOf(int node, bool skipEffects) const
{
    Scene const& scene = m_document->scene();
    if (!graphMode()) {
        // The one attached to it that stands furthest left.
        QVector<QPointF> const at = positions();
        int best = -1;
        for (int l = 0; l < scene.layers.size(); ++l)
            if (scene.parentIndex(l) == node && (best < 0 || at[l].x() < at[best].x()))
                best = l;
        return best;
    }
    NodeGraph const& g = scene.graph;
    int at = node;
    for (int guard = 0; guard < 256; ++guard) {
        // Down the cable whose node stands furthest left; a layer cabled nowhere to the first layer hanging from it.
        GraphCable const* best = nullptr;
        for (GraphCable const& c : g.cables)
            if (c.from == at) {
                GraphNode const* const to = g.node(c.to);
                if (to && (!best || to->at.x() < g.node(best->to)->at.x()))
                    best = &c;
            }
        int down = best ? best->to : 0;
        if (!down)
            if (GraphNode const* const n = g.node(at); n && n->kind == GraphNode::LayerNode)
                for (int l = 0; l < scene.layers.size() && !down; ++l)
                    if (int const parent = scene.parentIndex(l); parent >= 0 && scene.layers[parent].id == n->layer)
                        down = g.layerNode(scene.layers[l].id);
        GraphNode const* const d = g.node(down);
        if (!d)
            return 0;
        if (d->kind == GraphNode::GroupOut && best) {
            // Out of the group: to what that output of it goes into.
            int outside = 0;
            for (GraphCable const& c : g.cables)
                if (c.from == d->group && c.fromPort == best->toPort && !outside)
                    outside = c.to;
            if (!outside)
                return d->group;
            down = outside;
        }
        GraphNode const* const found = g.node(down);
        if (found && (found->kind == GraphNode::Waypoint || (skipEffects && found->kind == GraphNode::EffectNode))) {
            at = down;
            continue;
        }
        return down;
    }
    return 0;
}

void NodeView::selectLayers(QVector<int> const& layers)
{
    Scene const& scene = m_document->scene();
    if (layers.isEmpty())
        return;
    // The level the first of them is on.
    int const first = layers.first();
    if (!nodeShown(first)) {
        int group = -1;
        for (int up = scene.parentIndex(first), guard = 0; up >= 0 && guard < 256 && group < 0; up = scene.parentIndex(up), ++guard)
            if (scene.layers[up].kind == Layer::Group)
                group = up;
        m_groupId = group >= 0 ? scene.layers[group].id : 0;
    }
    m_layerPicked.clear();
    for (int l : layers)
        if (nodeShown(l))
            m_layerPicked << scene.layers[l].id;
    m_cable = -1;
    m_layer = first;
    emit layerPicked(first);
    emitPicked();
    update();
}

// `which`: 0 parent, 1 child, 2 children, 3 previous sibling, 4 next sibling.
void NodeView::selectRelative(int which, bool skipEffects, bool centre)
{
    Scene const& scene = m_document->scene();
    if (!graphMode()) {
        QVector<int> const picked = pickedLayers();
        int const from = picked.isEmpty() ? m_layer : picked.first();
        if (from < 0 || from >= scene.layers.size())
            return;
        QVector<int> found;
        if (which == 0) {
            if (int const up = parentOf(from, skipEffects); up >= 0)
                found = {up};
        } else if (which == 1) {
            if (int const down = childOf(from, skipEffects); down >= 0)
                found = {down};
        } else if (which == 2) {
            for (int l = 0; l < scene.layers.size(); ++l)
                if (scene.parentIndex(l) == from)
                    found << l;
        } else {
            // Those hanging from the same layer (or, at the top, shown at the same level), left to right.
            QVector<QPointF> const at = positions();
            QVector<int> siblings;
            for (int l = 0; l < scene.layers.size(); ++l)
                if (scene.parentIndex(l) == scene.parentIndex(from) && nodeShown(l) == nodeShown(from))
                    siblings << l;
            std::sort(siblings.begin(), siblings.end(), [&](int a, int b) { return at[a].x() < at[b].x(); });
            int const here = int(siblings.indexOf(from));
            if (siblings.size() > 1 && here >= 0)
                found = {siblings[(here + (which == 3 ? -1 : 1) + siblings.size()) % siblings.size()]};
        }
        if (found.isEmpty()) {
            emit statusMessage(tr("There is no node that way."));
            return;
        }
        selectLayers(found);
        if (centre)
            onActionCentreOnSelection();
        return;
    }
    NodeGraph const& g = scene.graph;
    int const from = m_graphNode ? m_graphNode : (m_graphPicked.isEmpty() ? 0 : m_graphPicked.last());
    if (!g.node(from))
        return;
    QVector<int> found;
    if (which == 0) {
        if (int const up = parentOf(from, skipEffects))
            found = {up};
    } else if (which == 1) {
        if (int const down = childOf(from, skipEffects))
            found = {down};
    } else if (which == 2) {
        for (GraphCable const& c : g.cables)
            if (c.from == from && !found.contains(c.to) && g.node(c.to) && g.node(c.to)->group == g.node(from)->group)
                found << c.to;
    } else {
        // The others going into the same node, in the order of its inputs.
        GraphCable const* out = nullptr;
        for (GraphCable const& c : g.cables)
            if (c.from == from && !out)
                out = &c;
        if (out) {
            GraphNode const* const target = g.node(out->to);
            QVector<int> siblings;
            for (int in = 0; target && in < target->inputs(); ++in)
                if (GraphCable const* const c = g.into(target->id, in))
                    siblings << c->from;
            int const here = int(siblings.indexOf(from));
            if (siblings.size() > 1 && here >= 0)
                found = {siblings[(here + (which == 3 ? -1 : 1) + siblings.size()) % siblings.size()]};
        }
    }
    if (found.isEmpty()) {
        emit statusMessage(tr("There is no node that way."));
        return;
    }
    GraphNode const* const first = g.node(found.first());
    if (first->group != m_graphGroup)
        enterGraphGroup(first->group);
    pickGraphNode(found.first());
    for (int id : found.mid(1))
        if (!m_graphPicked.contains(id))
            m_graphPicked << id;
    emitPicked();
    if (centre)
        onActionCentreOnSelection();
    update();
}

void NodeView::onActionSelectParent() { selectRelative(0, false, false); }
void NodeView::onActionSelectParentSkippingEffects() { selectRelative(0, true, false); }
void NodeView::onActionSelectChild() { selectRelative(1, false, false); }
void NodeView::onActionSelectChildSkippingEffects() { selectRelative(1, true, false); }
void NodeView::onActionSelectChildren() { selectRelative(2, false, false); }
void NodeView::onActionSelectPreviousSibling() { selectRelative(3, false, false); }
void NodeView::onActionSelectNextSibling() { selectRelative(4, false, false); }
void NodeView::onActionCentreOnParent() { selectRelative(0, false, true); }
void NodeView::onActionCentreOnChild() { selectRelative(1, false, true); }

QString NodeView::qualifiedName(int node) const
{
    Scene const& scene = m_document->scene();
    QStringList names;
    if (graphMode()) {
        NodeGraph const& g = scene.graph;
        for (GraphNode const* n = g.node(node); n; n = g.node(n->group))
            names.prepend(graphNodeName(*n));
    } else {
        if (node < 0 || node >= scene.layers.size())
            return QString();
        names << scene.layers[node].name;
        for (int up = scene.parentIndex(node), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
            if (scene.layers[up].kind == Layer::Group)
                names.prepend(scene.layers[up].name);
    }
    if (names.isEmpty())
        return QString();
    names.prepend(tr("Top"));
    return names.join(QLatin1Char('/'));
}

void NodeView::onActionCopyQualifiedName()
{
    QVector<int> const keys = pickedKeys();
    if (keys.isEmpty())
        return;
    int const node = graphMode() ? keys.first() : m_document->scene().layerIndex(keys.first());
    QString const name = qualifiedName(node);
    if (name.isEmpty())
        return;
    QApplication::clipboard()->setText(name);
    emit statusMessage(tr("Copied: %1").arg(name));
}

// ---- groups --------------------------------------------------------------------------------

void NodeView::onActionGroupSelection()
{
    if (graphMode()) {
        if (!groupGraphNodes(pickedKeys()))
            emit statusMessage(tr("Pick the nodes to put in a group first."));
        emitPicked();
        return;
    }
    QVector<int> layers = pickedLayers();
    if (layers.isEmpty() && m_layer >= 0 && m_layer < m_document->scene().layers.size())
        layers = {m_layer};
    int const at = m_document->groupLayers(layers);
    if (at >= 0)
        selectLayers({at});
}

void NodeView::onActionCreateGroup()
{
    if (graphMode()) {
        GraphNode group;
        group.kind = GraphNode::Group;
        addGraphNode(group, toGraph(QPointF(width() / 2.0, height() / 2.0)) - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
        emitPicked();
        return;
    }
    Scene const& scene = m_document->scene();
    int const insideId = enteredGroup() >= 0 ? scene.layers[enteredGroup()].id : 0;
    m_document->beginMacro(tr("Add Group"));
    int const at = m_document->addGroup(m_layer >= 0 && m_layer < scene.layers.size() ? m_layer : int(scene.layers.size()) - 1);
    int const madeId = at >= 0 && at < m_document->scene().layers.size() ? m_document->scene().layers[at].id : 0;
    if (madeId && insideId)
        m_document->setParent(m_document->scene().layerIndex(madeId), m_document->scene().layerIndex(insideId));
    m_document->endMacro();
    if (int const made = m_document->scene().layerIndex(madeId); made >= 0)
        selectLayers({made});
}

void NodeView::moveOutOfGroup(QVector<int> const& ids, bool dissolve)
{
    NodeGraph g = m_document->scene().graph;
    if (ids.isEmpty())
        return;
    GraphNode const* const any = g.node(ids.first());
    int const group = dissolve ? ids.first() : (any ? any->group : 0);
    GraphNode const* const holder = g.node(group);
    if (!holder || holder->kind != GraphNode::Group)
        return;
    int const outer = holder->group, in = g.groupIn(group), out = g.groupOut(group);
    QPointF const groupAt = holder->at;
    QSet<int> moving;
    for (GraphNode const& n : std::as_const(g.nodes))
        if (n.group == group && n.id != in && n.id != out && (dissolve || ids.contains(n.id)))
            moving.insert(n.id);
    if (moving.isEmpty())
        return;
    int inPorts = holder->inputs(), outPorts = holder->outputs();
    QVector<GraphCable> kept;
    QSet<int> outputsTaken;     // outputs of the group now fed straight from outside it
    for (GraphCable const& c : std::as_const(g.cables)) {
        bool const fromMoving = moving.contains(c.from), toMoving = moving.contains(c.to);
        if (c.from == in && toMoving) {
            // What came in through the group's input now comes straight.
            if (GraphCable const* const feed = g.into(group, c.fromPort))
                kept.push_back({feed->from, feed->fromPort, c.to, c.toPort});
        } else if (fromMoving && c.to == out) {
            // What went out through the group's output now goes straight.
            for (GraphCable const& away : std::as_const(g.cables))
                if (away.from == group && away.fromPort == c.toPort)
                    kept.push_back({c.from, c.fromPort, away.to, away.toPort});
            outputsTaken.insert(c.toPort);
        } else if (fromMoving && !toMoving && !dissolve && c.to != out && c.to != in) {
            // From a node moved out to one left inside: in through a new input of the group.
            kept.push_back({c.from, c.fromPort, group, inPorts});
            kept.push_back({in, inPorts, c.to, c.toPort});
            ++inPorts;
        } else if (!fromMoving && toMoving && !dissolve && c.from != in && c.from != out) {
            // From a node left inside to one moved out: out through a new output of the group.
            kept.push_back({c.from, c.fromPort, out, outPorts});
            kept.push_back({group, outPorts, c.to, c.toPort});
            ++outPorts;
        } else if (dissolve && (c.from == group || c.to == group || c.from == in || c.to == in || c.from == out || c.to == out)) {
            continue;   // (the group goes, and its cables with it)
        } else {
            kept.push_back(c);
        }
    }
    if (!dissolve)
        kept.erase(std::remove_if(kept.begin(), kept.end(), [&](GraphCable const& c) { return c.from == group && outputsTaken.contains(c.fromPort) && !moving.contains(c.to); }),
                   kept.end());
    // Where they go: round where the group stands (beside it, when the group stays).
    QPointF middle;
    for (int id : std::as_const(moving))
        middle += g.node(id)->at;
    middle /= double(moving.size());
    QPointF const shift = groupAt + (dissolve ? QPointF() : QPointF(kNodeWidth + 60.0, 0.0)) - middle;
    for (GraphNode& n : g.nodes)
        if (moving.contains(n.id)) {
            n.group = outer;
            n.at += shift;
        }
    for (Backdrop& b : g.backdrops)
        if (dissolve && b.group == group) {
            b.group = outer;
            b.rect.translate(shift);
        }
    if (dissolve) {
        g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [&](GraphNode const& n) { return n.id == group || n.id == in || n.id == out; }), g.nodes.end());
    } else {
        int const k = g.nodeIndex(group), ki = g.nodeIndex(in), ko = g.nodeIndex(out);
        g.nodes[k].ports = std::max(1, inPorts);
        g.nodes[k].outPorts = std::max(1, outPorts);
        if (ki >= 0)
            g.nodes[ki].outPorts = g.nodes[k].ports;
        if (ko >= 0)
            g.nodes[ko].ports = g.nodes[k].outPorts;
    }
    g.cables = kept;
    m_document->setGraph(g, dissolve ? tr("Explode Group") : tr("Move to Parent Group"));
    enterGraphGroup(outer);
    m_graphPicked = QVector<int>(moving.cbegin(), moving.cend());
    std::sort(m_graphPicked.begin(), m_graphPicked.end());
    m_graphNode = m_graphPicked.isEmpty() ? 0 : m_graphPicked.last();
    emitPicked();
    update();
}

void NodeView::onActionMoveToParentGroup()
{
    if (graphMode()) {
        if (!m_graphGroup) {
            emit statusMessage(tr("These nodes are at the top already."));
            return;
        }
        moveOutOfGroup(pickedKeys(), false);
        return;
    }
    Scene const& scene = m_document->scene();
    int const inside = enteredGroup();
    QVector<int> layers = pickedLayers();
    if (inside < 0 || layers.isEmpty()) {
        emit statusMessage(tr("Go into a group and pick what to move out of it."));
        return;
    }
    // Out to what the group hangs from, keeping each one's place in the view.
    int const target = scene.parentIndex(inside);
    QVector<int> ids;
    for (int l : layers)
        ids << scene.layers[l].id;
    m_document->beginMacro(tr("Move to Parent Group"));
    for (int id : std::as_const(ids))
        if (int const l = m_document->scene().layerIndex(id); l >= 0)
            m_document->setParent(l, target >= 0 ? m_document->scene().layerIndex(scene.layers[target].id) : -1);
    m_document->endMacro();
    leaveGroup();
    QVector<int> moved;
    for (int id : std::as_const(ids))
        if (int const l = m_document->scene().layerIndex(id); l >= 0)
            moved << l;
    selectLayers(moved);
}

void NodeView::onActionExplodeGroups()
{
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        QVector<int> groups;
        for (int id : pickedKeys())
            if (GraphNode const* const n = scene.graph.node(id); n && n->kind == GraphNode::Group)
                groups << id;
        if (groups.isEmpty()) {
            emit statusMessage(tr("Pick the group nodes to explode."));
            return;
        }
        m_document->beginMacro(tr("Explode Selected Groups"));
        for (int id : std::as_const(groups))
            moveOutOfGroup({id}, true);
        m_document->endMacro();
        return;
    }
    QVector<int> groups;
    for (int l : pickedLayers())
        if (scene.layers[l].kind == Layer::Group)
            groups << l;
    if (groups.isEmpty()) {
        emit statusMessage(tr("Pick the group nodes to explode."));
        return;
    }
    // (What was in a group goes to what the group hung from.)
    m_layerPicked.clear();
    m_document->removeLayers(groups);
}

void NodeView::onActionSelectGroupContent()
{
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        GraphNode const* const n = scene.graph.node(m_graphNode);
        if (!n || n->kind != GraphNode::Group) {
            emit statusMessage(tr("Pick a group node first."));
            return;
        }
        enterGraphGroup(n->id);
        selectAll();
        return;
    }
    QVector<int> const picked = pickedLayers();
    int const group = picked.isEmpty() ? m_layer : picked.first();
    if (group < 0 || group >= scene.layers.size() || scene.layers[group].kind != Layer::Group) {
        emit statusMessage(tr("Pick a group node first."));
        return;
    }
    enterGroup(group);
    selectAll();
}

// ---- waypoints and ports -------------------------------------------------------------------

QString NodeView::askName(QString const& title, QString const& label, QString const& text, bool waypoint)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("NodeNameDialog"));
    dialog.setWindowTitle(title);
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const edit = new QLineEdit(text, &dialog);
    edit->setObjectName(QStringLiteral("NodeNameEdit"));
    if (waypoint) {
        // A waypoint's name: letters, digits, spaces, dots, dashes and underscores (no slash: it parts a qualified name).
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[\\w .\\-]{0,64}")), edit));
        edit->setToolTip(tr("Letters, digits, spaces, dots, dashes and underscores."));
    }
    form->addRow(label, edit);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    return edit->text().trimmed().isEmpty() ? QStringLiteral(" ") : edit->text().trimmed();
}

int NodeView::addWaypoint(QPointF graphPos, int onCable)
{
    if (!graphMode())
        return 0;
    NodeGraph g = m_document->scene().graph;
    GraphNode w;
    w.kind = GraphNode::Waypoint;
    w.id = g.nextId();
    w.group = m_graphGroup;
    w.at = graphPos - QPointF(8.0, 8.0);
    if (onCable >= 0 && onCable < g.cables.size()) {
        // Into the cable: it now runs through the waypoint, which stands half way along it.
        GraphCable const c = g.cables[onCable];
        GraphNode const* const from = g.node(c.from);
        if (from && from->group == m_graphGroup) {
            QVector<QPointF> const along = cablePoints(graphOutPort(c.from, c.fromPort), graphInPort(c.to, c.toPort));
            w.at = toGraph(along[along.size() / 2]) - QPointF(8.0, 8.0);
            g.cables.removeAt(onCable);
            g.cables.push_back({c.from, c.fromPort, w.id, 0});
            g.cables.push_back({w.id, 0, c.to, c.toPort});
        }
    }
    g.nodes.push_back(w);
    m_document->setGraph(g, tr("Create Waypoint"));
    pickGraphNode(w.id);
    return w.id;
}

void NodeView::onActionCreateWaypoint()
{
    if (!graphMode()) {
        emit statusMessage(tr("Waypoints go on the cables of a graph wired by hand (Node View > Wire the Graph by Hand)."));
        return;
    }
    QPointF const at = rect().contains(m_hover.toPoint()) ? m_hover : QPointF(width() / 2.0, height() / 2.0);
    addWaypoint(toGraph(at), m_graphCable);
}

void NodeView::onActionRenameWaypoint()
{
    GraphNode const* const n = m_document->scene().graph.node(m_graphNode);
    if (!graphMode() || !n || n->kind != GraphNode::Waypoint) {
        emit statusMessage(tr("Pick a waypoint to rename."));
        return;
    }
    int const id = n->id;
    QString const name = askName(tr("Rename Waypoint"), tr("Name:"), n->name, true);
    if (name.isNull())
        return;
    NodeGraph g = m_document->scene().graph;
    g.nodes[g.nodeIndex(id)].name = name.trimmed();
    m_document->setGraph(g, tr("Rename Waypoint"));
}

void NodeView::pickPort(PortRef port, bool add)
{
    if (!add)
        m_portsPicked.clear();
    if (m_portsPicked.contains(port))
        m_portsPicked.removeAll(port);
    else if (m_document->scene().graph.node(port.node))
        m_portsPicked << port;
    update();
}

int NodeView::namePorts(QString const& name, bool filters)
{
    if (!graphMode())
        return 0;
    NodeGraph g = m_document->scene().graph;
    QVector<PortRef> ports;
    for (PortRef const& p : std::as_const(m_portsPicked))
        if (p.out != filters)
            ports << p;
    if (ports.isEmpty())
        for (int id : pickedKeys())
            if (GraphNode const* const n = g.node(id))
                for (int k = 0; k < (filters ? n->inputs() : n->outputs()); ++k)
                    ports << PortRef{id, k, !filters};
    int count = 0;
    for (PortRef const& p : std::as_const(ports)) {
        int const i = g.nodeIndex(p.node);
        if (i < 0)
            continue;
        QMap<int, QString>& map = filters ? g.nodes[i].portFilters : g.nodes[i].portNames;
        if (name.trimmed().isEmpty())
            map.remove(p.port);
        else
            map.insert(p.port, name.trimmed());
        ++count;
    }
    if (count)
        m_document->setGraph(g, filters ? tr("Add Filter to In Port(s)") : tr("Add Name to Port(s)"));
    return count;
}

void NodeView::onActionAddPortName()
{
    if (!graphMode()) {
        emit statusMessage(tr("Ports are named in a graph wired by hand."));
        return;
    }
    QString const name = askName(tr("Add Name to Port(s)"), tr("Name:"), QString(), false);
    if (!name.isNull() && !namePorts(name, false))
        emit statusMessage(tr("Pick the output ports (or nodes) to name first."));
}

void NodeView::onActionAddPortFilter()
{
    if (!graphMode()) {
        emit statusMessage(tr("Ports are named in a graph wired by hand."));
        return;
    }
    QString const filter = askName(tr("Add Filter to In Port(s)"), tr("Takes outputs named (* and ? stand for any letters):"), QString(), false);
    if (!filter.isNull() && !namePorts(filter, true))
        emit statusMessage(tr("Pick the input ports (or nodes) to filter first."));
}

void NodeView::onActionClearPortNames()
{
    if (!graphMode())
        return;
    NodeGraph g = m_document->scene().graph;
    bool changed = false;
    auto clear = [&](int node, int port, bool out) {
        int const i = g.nodeIndex(node);
        if (i < 0)
            return;
        QMap<int, QString>& map = out ? g.nodes[i].portNames : g.nodes[i].portFilters;
        changed = map.remove(port) > 0 || changed;
    };
    if (!m_portsPicked.isEmpty()) {
        for (PortRef const& p : std::as_const(m_portsPicked))
            clear(p.node, p.port, p.out);
    } else {
        for (int id : pickedKeys())
            if (int const i = g.nodeIndex(id); i >= 0) {
                changed = changed || !g.nodes[i].portNames.isEmpty() || !g.nodes[i].portFilters.isEmpty();
                g.nodes[i].portNames.clear();
                g.nodes[i].portFilters.clear();
            }
    }
    if (changed)
        m_document->setGraph(g, tr("Clear Named Port(s)"));
}

void NodeView::onActionCopyPortNames()
{
    if (!graphMode())
        return;
    int const id = !m_portsPicked.isEmpty() ? m_portsPicked.first().node : (pickedKeys().isEmpty() ? 0 : pickedKeys().first());
    GraphNode const* const n = m_document->scene().graph.node(id);
    if (!n)
        return;
    portClip() = {true, n->portNames, n->portFilters};
    emit statusMessage(tr("The port names of %1 copied.").arg(graphNodeName(*n)));
}

void NodeView::onActionPastePortNames()
{
    if (!graphMode() || !portClip().any)
        return;
    NodeGraph g = m_document->scene().graph;
    bool changed = false;
    for (int id : pickedKeys())
        if (int const i = g.nodeIndex(id); i >= 0) {
            GraphNode& n = g.nodes[i];
            n.portNames.clear();
            n.portFilters.clear();
            for (auto it = portClip().names.cbegin(); it != portClip().names.cend(); ++it)
                if (it.key() < n.outputs())
                    n.portNames.insert(it.key(), it.value());
            for (auto it = portClip().filters.cbegin(); it != portClip().filters.cend(); ++it)
                if (it.key() < n.inputs())
                    n.portFilters.insert(it.key(), it.value());
            changed = true;
        }
    if (changed)
        m_document->setGraph(g, tr("Paste Named Port(s)"));
}

void NodeView::onActionShowPortFiltering(bool on)
{
    m_showFilters = on;
    update();
}

// ---- arranging -----------------------------------------------------------------------------

void NodeView::onActionOrderUp()
{
    if (!graphMode()) {
        QVector<int> const picked = pickedLayers();
        int const l = picked.isEmpty() ? m_layer : picked.first();
        if (l >= 0 && l + 1 < m_document->scene().layers.size())
            m_document->moveLayer(l, 1);
        return;
    }
    // Into its composite one input nearer the front (the first).
    NodeGraph g = m_document->scene().graph;
    for (GraphCable& c : g.cables) {
        GraphNode const* const to = g.node(c.to);
        if (c.from != m_graphNode || !to || to->kind != GraphNode::Composite || c.toPort == 0)
            continue;
        int const was = c.toPort;
        for (GraphCable& other : g.cables)
            if (other.to == c.to && other.toPort == was - 1)
                other.toPort = was;
        c.toPort = was - 1;
        m_document->setGraph(g, tr("Order Node Up"));
        return;
    }
}

void NodeView::onActionOrderDown()
{
    if (!graphMode()) {
        QVector<int> const picked = pickedLayers();
        int const l = picked.isEmpty() ? m_layer : picked.first();
        if (l > 0 && l < m_document->scene().layers.size())
            m_document->moveLayer(l, -1);
        return;
    }
    NodeGraph g = m_document->scene().graph;
    for (GraphCable& c : g.cables) {
        GraphNode const* const to = g.node(c.to);
        if (c.from != m_graphNode || !to || to->kind != GraphNode::Composite || c.toPort + 1 >= to->inputs())
            continue;
        int const was = c.toPort;
        for (GraphCable& other : g.cables)
            if (other.to == c.to && other.toPort == was + 1)
                other.toPort = was;
        c.toPort = was + 1;
        m_document->setGraph(g, tr("Order Node Down"));
        return;
    }
}

void NodeView::onActionAlignHorizontally()
{
    // In one row: the nodes picked at the height they have on average.
    if (graphMode()) {
        NodeGraph g = m_document->scene().graph;
        QVector<int> const keys = pickedKeys();
        if (keys.size() < 2)
            return;
        double y = 0.0;
        for (int id : keys)
            y += g.node(id)->at.y();
        y /= double(keys.size());
        for (int id : keys)
            g.nodes[g.nodeIndex(id)].at.setY(y);
        m_document->setGraph(g, tr("Align Nodes Horizontally"));
        return;
    }
    QVector<int> const layers = pickedLayers();
    if (layers.size() < 2)
        return;
    QVector<QPointF> const at = positions();
    double y = 0.0;
    for (int l : layers)
        y += at[l].y();
    y /= double(layers.size());
    m_document->beginMacro(tr("Align Nodes Horizontally"));
    for (int l : layers)
        m_document->setNodePos(l, QPointF(at[l].x(), y));
    m_document->endMacro();
}

void NodeView::onActionCloneNodes(QString const& how)
{
    Scene const& scene = m_document->scene();
    bool const duplicate = how == QLatin1String("duplicate");
    QString const text = duplicate ? tr("Duplicate Selected Nodes") : tr("Clone Selected Nodes");
    // The layers to clone (from the front one back, so the copies made do not move those still to copy) and where their nodes stood.
    QVector<int> layerIds;
    QVector<int> otherNodes;
    QHash<int, QPointF> stood;
    QVector<QPointF> const at = positions();
    if (graphMode()) {
        for (int id : pickedKeys())
            if (GraphNode const* const n = scene.graph.node(id)) {
                if (n->kind == GraphNode::LayerNode) {
                    layerIds << n->layer;
                    stood.insert(n->layer, n->at);
                } else if (n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut && n->kind != GraphNode::Group) {
                    otherNodes << id;
                }
            }
    } else {
        for (int id : pickedKeys()) {
            layerIds << id;
            stood.insert(id, at[scene.layerIndex(id)]);
        }
    }
    QVector<int> keep;
    for (int id : std::as_const(layerIds))
        if (int const l = scene.layerIndex(id); l >= 0 && (duplicate || scene.layers[l].holdsDrawings()))
            keep << id;
    if (keep.isEmpty() && otherNodes.isEmpty()) {
        emit statusMessage(duplicate ? tr("Pick the nodes to duplicate.") : tr("Pick the drawing nodes to clone."));
        return;
    }
    m_document->beginMacro(text);
    QVector<int> madeIds;
    QHash<int, int> madeFrom;   // a new layer's id -> the id it was made from
    for (int id : std::as_const(keep)) {
        int const l = m_document->scene().layerIndex(id);
        int const made = duplicate ? m_document->duplicateLayer(l) : m_document->addCloneLayer(l, how == QLatin1String("timing"));
        if (made >= 0 && made < m_document->scene().layers.size()) {
            madeIds << m_document->scene().layers[made].id;
            madeFrom.insert(m_document->scene().layers[made].id, id);
        }
    }
    // Their nodes beside those they were made from.
    if (graphMode()) {
        NodeGraph g = m_document->scene().graph;
        QVector<int> picked;
        for (int made : std::as_const(madeIds))
            if (int const k = g.nodeIndex(g.layerNode(made)); k >= 0) {
                g.nodes[k].at = stood.value(madeFrom.value(made)) + QPointF(40.0, 40.0);
                g.nodes[k].group = m_graphGroup;
                picked << g.nodes[k].id;
            }
        for (int id : std::as_const(otherNodes)) {
            GraphNode copy = *g.node(id);
            copy.id = g.nextId();
            copy.at += QPointF(40.0, 40.0);
            g.nodes.push_back(copy);
            picked << copy.id;
        }
        m_document->setGraph(g, text);
        m_graphPicked = picked;
        m_graphNode = picked.isEmpty() ? 0 : picked.last();
    } else {
        for (int made : std::as_const(madeIds))
            if (int const l = m_document->scene().layerIndex(made); l >= 0)
                m_document->setNodePos(l, stood.value(madeFrom.value(made)) + QPointF(40.0, 40.0));
        m_layerPicked = madeIds;
    }
    m_document->endMacro();
    emitPicked();
    update();
}

void NodeView::onActionEditor()
{
    Scene const& scene = m_document->scene();
    if (!graphMode()) {
        QVector<int> const picked = pickedLayers();
        int const l = picked.isEmpty() ? m_layer : picked.first();
        if (l >= 0 && l < scene.layers.size())
            emit editRequested(l);
        return;
    }
    GraphNode const* const n = scene.graph.node(m_graphNode);
    if (!n)
        return;
    switch (n->kind) {
    case GraphNode::EffectNode:
        editGraphNode(n->id);
        break;
    case GraphNode::LayerNode:
        if (int const l = scene.layerIndex(n->layer); l >= 0)
            emit editRequested(l);
        break;
    case GraphNode::Group:
        enterGraphGroup(n->id);
        break;
    case GraphNode::Waypoint:
        onActionRenameWaypoint();
        break;
    default: {
        QString const name = askName(tr("Rename Node"), tr("Name:"), n->name, false);
        if (name.isNull())
            break;
        NodeGraph g = scene.graph;
        g.nodes[g.nodeIndex(m_graphNode)].name = name.trimmed();
        m_document->setGraph(g, tr("Rename Node"));
    }
    }
}

void NodeView::onActionCreateDisplay()
{
    if (!graphMode()) {
        emit statusMessage(tr("Displays are added to a graph wired by hand (Node View > Wire the Graph by Hand)."));
        return;
    }
    if (m_graphGroup) {
        emit statusMessage(tr("A display goes at the top of the graph, outside the groups."));
        return;
    }
    GraphNode display;
    display.kind = GraphNode::Display;
    addGraphNode(display, toGraph(QPointF(width() / 2.0, height() / 2.0)) - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
}

void NodeView::onActionCreateBackdrop()
{
    addBackdrop();
}

void NodeView::onActionRecalculateSelected()
{
    for (int key : pickedKeys())
        for (auto it = m_thumbCache.begin(); it != m_thumbCache.end();)
            it = it.key().startsWith(QString::number(key) + QLatin1Char(':')) ? m_thumbCache.erase(it) : std::next(it);
    update();
    emit recalculateRequested(false);
}

void NodeView::onActionRecalculateAll()
{
    m_thumbCache.clear();
    update();
    emit recalculateRequested(true);
}

// ---- cut, copy, paste ----------------------------------------------------------------------

void NodeView::copy()
{
    Scene const& scene = m_document->scene();
    NodeClip clip;
    clip.graph = graphMode();
    QVector<int> layers;
    if (clip.graph) {
        NodeGraph const& g = scene.graph;
        // The nodes picked, with all that is inside the groups among them.
        QSet<int> taken;
        for (int id : pickedKeys())
            if (GraphNode const* const n = g.node(id); n && n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut)
                taken.insert(id);
        for (bool more = true; more;) {
            more = false;
            for (GraphNode const& n : g.nodes)
                if (n.group && taken.contains(n.group) && !taken.contains(n.id)) {
                    taken.insert(n.id);
                    more = true;
                }
        }
        for (GraphNode const& n : g.nodes)
            if (taken.contains(n.id)) {
                clip.nodes.nodes.push_back(n);
                if (n.kind == GraphNode::LayerNode)
                    if (int const l = scene.layerIndex(n.layer); l >= 0)
                        layers << l;
            }
        for (GraphCable const& c : g.cables)
            if (taken.contains(c.from) && taken.contains(c.to))
                clip.nodes.cables.push_back(c);
    } else {
        QVector<QPointF> const at = positions();
        layers = pickedLayers();
        if (layers.isEmpty() && m_layer >= 0 && m_layer < scene.layers.size())
            layers = {m_layer};
        for (int l : std::as_const(layers))
            clip.layerAt.insert(scene.layers[l].id, at[l]);
    }
    if (!layers.isEmpty()) {
        clip.piece = scene.extract(layers, 0, scene.frameCount() - 1);
        for (Layer const& l : std::as_const(clip.piece.layers))
            clip.layerIds << l.id;
    }
    clip.any = !clip.piece.layers.isEmpty() || !clip.nodes.nodes.isEmpty();
    if (clip.any)
        nodeClip() = clip;
}

void NodeView::cut()
{
    copy();
    deleteSelection();
}

void NodeView::paste()
{
    NodeClip const& clip = nodeClip();
    if (!clip.any)
        return;
    Scene const& scene = m_document->scene();
    bool const camera = std::any_of(scene.layers.cbegin(), scene.layers.cend(), [](Layer const& l) { return l.kind == Layer::Camera; });
    int const before = int(scene.layers.size());
    m_document->beginMacro(tr("Paste Nodes"));
    // The layers first: they come in at the end, in the order they were copied (a second camera left out).
    QHash<int, int> layerOf;
    if (!clip.piece.layers.isEmpty() && m_document->insertTemplate(clip.piece, 0) >= 0) {
        int next = before;
        for (Layer const& theirs : clip.piece.layers) {
            if (theirs.kind == Layer::Camera && camera)
                continue;
            if (next < m_document->scene().layers.size())
                layerOf.insert(theirs.id, m_document->scene().layers[next++].id);
        }
    }
    QVector<int> pickedLayerIds;
    if (!clip.graph || !graphMode()) {
        for (auto it = layerOf.cbegin(); it != layerOf.cend(); ++it) {
            int const l = m_document->scene().layerIndex(it.value());
            if (l < 0)
                continue;
            if (clip.layerAt.contains(it.key()))
                m_document->setNodePos(l, clip.layerAt.value(it.key()) + QPointF(30.0, 30.0));
            // Put in the group the view is inside, those that hang from nothing.
            if (enteredGroup() >= 0 && m_document->scene().parentIndex(l) < 0 && m_document->scene().layers[enteredGroup()].id != it.value())
                m_document->setParent(l, enteredGroup());
            pickedLayerIds << it.value();
        }
        if (graphMode()) {
            // (Pasted into a graph wired by hand: a node for each layer, cabled to nothing.)
            NodeGraph g = m_document->scene().graph;
            for (int id : std::as_const(pickedLayerIds))
                if (!g.layerNode(id)) {
                    GraphNode n;
                    n.id = g.nextId();
                    n.kind = GraphNode::LayerNode;
                    n.layer = id;
                    n.group = m_graphGroup;
                    n.at = toGraph(QPointF(width() / 2.0, height() / 2.0)) + QPointF(140.0 * double(pickedLayerIds.indexOf(id)), 0.0);
                    g.nodes.push_back(n);
                }
            m_document->setGraph(g, tr("Paste Nodes"));
        }
        m_document->endMacro();
        if (!graphMode()) {
            m_layerPicked = pickedLayerIds;
            emitPicked();
        }
        update();
        return;
    }
    // A graph wired by hand: the nodes, with new ids, and the cables among them; a layer's node goes with its new layer.
    NodeGraph g = m_document->scene().graph;
    QHash<int, int> idOf;
    QSet<int> copied;
    for (GraphNode const& n : clip.nodes.nodes)
        copied.insert(n.id);
    for (GraphNode const& n : clip.nodes.nodes) {
        if (n.kind == GraphNode::LayerNode) {
            int const layer = layerOf.value(n.layer, 0);
            if (!layer)
                continue;
            int node = g.layerNode(layer);
            if (node) {
                g.cables.erase(std::remove_if(g.cables.begin(), g.cables.end(), [node](GraphCable const& c) { return c.from == node || c.to == node; }),
                               g.cables.end());
            } else {
                GraphNode made;
                made.id = node = g.nextId();
                made.kind = GraphNode::LayerNode;
                made.layer = layer;
                g.nodes.push_back(made);
            }
            idOf.insert(n.id, node);
        } else {
            GraphNode made = n;
            made.id = g.nextId();
            g.nodes.push_back(made);
            idOf.insert(n.id, made.id);
        }
    }
    QVector<int> picked;
    for (GraphNode const& n : clip.nodes.nodes) {
        int const id = idOf.value(n.id, 0);
        int const k = g.nodeIndex(id);
        if (k < 0)
            continue;
        GraphNode& made = g.nodes[k];
        made.sheets = n.sheets;
        made.at = n.at + QPointF(30.0, 30.0);
        made.group = copied.contains(n.group) ? idOf.value(n.group, m_graphGroup) : m_graphGroup;
        if (made.group == m_graphGroup)
            picked << id;
    }
    for (GraphCable const& c : clip.nodes.cables)
        if (idOf.contains(c.from) && idOf.contains(c.to))
            g.cables.push_back({idOf.value(c.from), c.fromPort, idOf.value(c.to), c.toPort});
    m_document->setGraph(g, tr("Paste Nodes"));
    m_document->endMacro();
    m_graphPicked = picked;
    m_graphNode = picked.isEmpty() ? 0 : picked.last();
    emitPicked();
    update();
}

void NodeView::emitPicked()
{
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    if (graphMode()) {
        for (int id : m_graphPicked)
            if (GraphNode const* const n = scene.graph.node(id); n && n->kind == GraphNode::LayerNode)
                if (int const l = scene.layerIndex(n->layer); l >= 0)
                    layers << l;
    } else {
        layers = pickedLayers();
    }
    std::sort(layers.begin(), layers.end(), std::greater<int>());
    emit layersPicked(layers);
}
