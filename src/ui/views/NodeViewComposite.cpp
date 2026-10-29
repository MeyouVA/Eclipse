// Eclipse - the Node View's composites: made, grouped with, cabled into, and their settings.
#include "ui/views/NodeView.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSet>
#include <QSpinBox>

#include <algorithm>

#include "model/document/Commands.h"

void NodeView::onActionCreateComposite()
{
    if (!graphMode()) {
        emit statusMessage(tr("Composites are added to a graph wired by hand (Node View > Wire the Graph by Hand)."));
        return;
    }
    // Under the nodes picked, which are cabled into it; with none picked, in the middle of the view.
    QVector<int> const picked = pickedKeys();
    NodeGraph const& g = m_document->scene().graph;
    QRectF reach;
    for (int id : picked)
        if (GraphNode const* const n = g.node(id); n && n->group == m_graphGroup)
            reach = reach.united(QRectF(n->at, QSizeF(kNodeWidth, kNodeHeight)));
    QPointF const at = reach.isNull() ? toGraph(QPointF(width() / 2.0, height() / 2.0)) - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0)
                                      : QPointF(reach.center().x() - kNodeWidth / 2.0, reach.bottom() + 60.0);
    m_document->beginMacro(tr("Create Composite"));
    GraphNode composite;
    composite.kind = GraphNode::Composite;
    int const made = addGraphNode(composite, at);
    if (!picked.isEmpty())
        connectToComposite(made, picked);
    m_document->endMacro();
    pickGraphNode(made);
    emitPicked();
}

int NodeView::connectToComposite(int composite, QVector<int> const& ids)
{
    NodeGraph g = m_document->scene().graph;
    int const k = g.nodeIndex(composite);
    if (k < 0 || g.nodes[k].kind != GraphNode::Composite)
        return 0;
    QString const name = graphNodeName(g.nodes[k]);
    // Left to right as they stand, so the ports follow the layout.
    QVector<int> order;
    for (int id : ids)
        if (id != composite && g.node(id))
            order.push_back(id);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g.node(a)->at.x() < g.node(b)->at.x(); });
    int linked = 0;
    for (int id : std::as_const(order)) {
        GraphNode const* const n = g.node(id);
        if (pictureless(*n)) {
            emit statusMessage(tr("Cannot connect pegs to composite %1").arg(name));
            continue;
        }
        if (n->group != g.nodes[k].group || n->outputs() == 0 || n->kind == GraphNode::Display || g.feeds(composite, id))
            continue;
        bool already = false;
        for (GraphCable const& c : std::as_const(g.cables))
            already = already || (c.from == id && c.to == composite);
        if (already)
            continue;
        // Its first free input; a new one when all are taken.
        int port = 0;
        while (port < g.nodes[k].inputs() && g.into(composite, port))
            ++port;
        if (port >= g.nodes[k].inputs())
            g.nodes[k].ports = port + 1;
        g.cables.push_back({id, 0, composite, port});
        ++linked;
    }
    if (linked)
        m_document->setGraph(g, tr("Connect to Composite"));
    update();
    return linked;
}

void NodeView::onActionConnectToComposite()
{
    if (!graphMode())
        return;
    NodeGraph const& g = m_document->scene().graph;
    QVector<int> const picked = pickedKeys();
    // The composite: the node clicked last, or else the first composite picked.
    int target = 0;
    if (GraphNode const* const n = g.node(m_graphNode); n && n->kind == GraphNode::Composite)
        target = m_graphNode;
    for (int id : picked)
        if (GraphNode const* const n = g.node(id); !target && n && n->kind == GraphNode::Composite)
            target = id;
    if (!target) {
        emit statusMessage(tr("Pick a composite with the nodes: they are cabled into it."));
        return;
    }
    if (!connectToComposite(target, picked))
        emit statusMessage(tr("Nothing picked could be cabled into the composite."));
}

int NodeView::groupGraphNodesWithComposite(QVector<int> const& ids)
{
    NodeGraph g = m_document->scene().graph;
    QSet<int> inside;
    for (int id : ids)
        if (GraphNode const* const n = g.node(id); n && n->group == m_graphGroup && n->kind != GraphNode::GroupIn && n->kind != GraphNode::GroupOut
                                                    && n->kind != GraphNode::Display)
            inside.insert(id);
    if (inside.isEmpty()) {
        emit statusMessage(tr("Could not group selection with composite"));
        return 0;
    }
    // What would leave the group: cables going out, and outputs cabled nowhere. Each comes into the composite instead.
    QVector<QPair<int, int>> sources;
    QVector<GraphCable> kept;
    QVector<GraphCable> leaving;
    for (GraphCable const& c : std::as_const(g.cables)) {
        if (inside.contains(c.from) && !inside.contains(c.to)) {
            leaving.push_back(c);
            if (!sources.contains(qMakePair(c.from, c.fromPort)))
                sources.push_back(qMakePair(c.from, c.fromPort));
        } else {
            kept.push_back(c);
        }
    }
    QRectF reach;
    for (int id : std::as_const(inside)) {
        GraphNode const* const n = g.node(id);
        reach = reach.united(QRectF(n->at, QSizeF(kNodeWidth, kNodeHeight)));
        if (n->kind == GraphNode::Note || pictureless(*n))
            continue;
        for (int o = 0; o < n->outputs(); ++o) {
            bool used = false;
            for (GraphCable const& c : std::as_const(g.cables))
                used = used || (c.from == id && c.fromPort == o);
            if (!used)
                sources.push_back(qMakePair(id, o));
        }
    }
    std::stable_sort(sources.begin(), sources.end(), [&](QPair<int, int> const& a, QPair<int, int> const& b) { return g.node(a.first)->at.x() < g.node(b.first)->at.x(); });
    GraphNode composite;
    composite.id = g.nextId();
    composite.kind = GraphNode::Composite;
    composite.group = m_graphGroup;
    composite.passThrough = m_options.compositePassThrough;
    composite.ports = std::max<int>(1, int(sources.size()));
    composite.at = QPointF(reach.center().x() - kNodeWidth / 2.0, reach.bottom() + 40.0);
    g.nodes.push_back(composite);
    for (int k = 0; k < sources.size(); ++k)
        kept.push_back({sources[k].first, sources[k].second, composite.id, k});
    // Each node outside that took pictures from inside now takes the composite's, once.
    for (GraphCable const& c : std::as_const(leaving))
        if (std::none_of(kept.cbegin(), kept.cend(), [&](GraphCable const& o) { return o.from == composite.id && o.to == c.to; }))
            kept.push_back({composite.id, 0, c.to, c.toPort});
    g.cables = kept;
    m_document->beginMacro(tr("Group Selection with Composite"));
    m_document->setGraph(g, tr("Add Composite"));
    QVector<int> grouped(inside.cbegin(), inside.cend());
    grouped.push_back(composite.id);
    int const group = groupGraphNodes(grouped);
    m_document->endMacro();
    if (!group)
        emit statusMessage(tr("Could not group selection with composite"));
    return group;
}

void NodeView::onActionGroupWithComposite()
{
    if (!graphMode()) {
        // (Layers: a group layer puts what it holds together itself.)
        onActionGroupSelection();
        return;
    }
    if (!groupGraphNodesWithComposite(pickedKeys()))
        emit statusMessage(tr("Pick the nodes to put in a group first."));
    emitPicked();
}

void NodeView::editComposite(int id)
{
    NodeGraph g = m_document->scene().graph;
    int const i = g.nodeIndex(id);
    if (i < 0 || g.nodes[i].kind != GraphNode::Composite)
        return;
    GraphNode& n = g.nodes[i];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CompositeDialog"));
    dialog.setWindowTitle(tr("Composite: %1").arg(graphNodeName(n)));
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const name = new QLineEdit(n.name, &dialog);
    name->setObjectName(QStringLiteral("CompositeName"));
    name->setPlaceholderText(tr("Composite"));
    form->addRow(tr("Name"), name);
    QComboBox* const mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("CompositeMode"));
    mode->addItems({tr("As Bitmap"), tr("As Vector"), tr("Pass Through")});
    mode->setItemData(0, tr("Each input made into a picture, then the pictures laid one over the other."), Qt::ToolTipRole);
    mode->setItemData(1, tr("The layers that come straight in drawn into one picture together, with no picture of each between."), Qt::ToolTipRole);
    mode->setItemData(2, tr("Its inputs go on separately into the composite it feeds, where their blend modes meet what lies under them."), Qt::ToolTipRole);
    mode->setCurrentIndex(n.passThrough ? 2 : n.asVector ? 1 : 0);
    form->addRow(tr("Mode"), mode);
    QSpinBox* const inputs = new QSpinBox(&dialog);
    inputs->setObjectName(QStringLiteral("CompositeInputs"));
    inputs->setRange(1, 256);
    inputs->setValue(std::max(1, n.ports));
    form->addRow(tr("Inputs"), inputs);
    QComboBox* const outputZ = new QComboBox(&dialog);
    outputZ->setObjectName(QStringLiteral("CompositeOutputZ"));
    outputZ->addItems({tr("Leftmost"), tr("Rightmost"), tr("Lowest (Furthest)"), tr("Highest (Nearest)"), tr("Port For Output Z")});
    outputZ->setToolTip(tr("How deep the picture it gives stands, for the composite it goes into to lay it among the others."));
    outputZ->setCurrentIndex(std::clamp(n.outputZ, 0, int(GraphNode::ZFromPort)));
    form->addRow(tr("Output Z"), outputZ);
    QSpinBox* const zPort = new QSpinBox(&dialog);
    zPort->setObjectName(QStringLiteral("CompositeZPort"));
    zPort->setRange(1, 256);
    zPort->setValue(n.zPort + 1);
    zPort->setEnabled(n.outputZ == GraphNode::ZFromPort);
    connect(outputZ, &QComboBox::currentIndexChanged, zPort, [zPort](int index) { zPort->setEnabled(index == GraphNode::ZFromPort); });
    form->addRow(tr("Port For Output Z"), zPort);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    n.name = name->text().trimmed();
    n.passThrough = mode->currentIndex() == 2;
    n.asVector = mode->currentIndex() == 1;
    n.outputZ = outputZ->currentIndex();
    n.zPort = zPort->value() - 1;
    if (inputs->value() < n.ports)
        g.cables.removeIf([&](GraphCable const& c) { return c.to == id && c.toPort >= inputs->value(); });
    n.ports = inputs->value();
    if (g != m_document->scene().graph)
        m_document->setGraph(g, tr("Change Composite"));
}
