// Eclipse - the main window: compositing commands (grouping with a composite, the matte view, effects inserted).
#include "ui/window/MainWindow.h"

#include <QComboBox>
#include <QStatusBar>

#include <algorithm>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/NodeView.h"

void MainWindow::onActionGroupSelectionWithComposite()
{
    Scene const& scene = m_document->scene();
    if (scene.graph.isEmpty() || !m_nodeView) {
        // (Without a graph wired by hand a group layer puts what it holds together itself.)
        onActionGroupSelection();
        return;
    }
    QVector<int> nodes;
    for (int l : m_timeline->selectedLayers())
        if (l >= 0 && l < scene.layers.size())
            if (int const n = scene.graph.layerNode(scene.layers[l].id))
                nodes.push_back(n);
    if (nodes.isEmpty() || !m_nodeView->groupGraphNodesWithComposite(nodes))
        statusBar()->showMessage(tr("Could not group selection with composite"), 6000);
}

void MainWindow::onActionMatteView(bool on)
{
    // (Through the view mode box at the foot of the Camera view, so the two agree.)
    if (QComboBox* const mode = findChild<QComboBox*>(QStringLiteral("ViewMode")))
        mode->setCurrentIndex(on ? int(Canvas::ViewMatte) : int(Canvas::ViewNormal));
    else
        m_canvas->setViewMode(on ? Canvas::ViewMatte : Canvas::ViewNormal);
}

void MainWindow::onActionToggleImageMatte()
{
    onActionMatteView(m_canvas->viewMode() != Canvas::ViewMatte);
}

void MainWindow::onActionInsertEffect(QString const& type)
{
    bool okay = false;
    int const kind = type.toInt(&okay);
    if (!okay || kind < 0 || kind >= Effect::TypeCount)
        return;
    Scene const& scene = m_document->scene();
    Effect const e = Effect::made(Effect::Type(kind));
    QString const name = Effects::name(e, scene).section(QStringLiteral("  "), 0, 0);
    if (!scene.graph.isEmpty()) {
        // A graph wired by hand: a node of its own, between the current layer's node and what that fed.
        NodeGraph g = scene.graph;
        int const from = layer() >= 0 && layer() < scene.layers.size() ? g.layerNode(scene.layers[layer()].id) : 0;
        GraphNode const* const source = g.node(from);
        GraphNode node;
        node.id = g.nextId();
        node.kind = GraphNode::EffectNode;
        node.effect = e;
        node.group = source ? source->group : (m_nodeView ? m_nodeView->graphGroup() : 0);
        node.at = source ? source->at + QPointF(0.0, 60.0) : QPointF();
        if (e.type == Effect::ImageSwitch)
            node.ports = 2;
        if (source) {
            for (GraphCable& c : g.cables)
                if (c.from == from && c.fromPort == 0)
                    c.from = node.id;
            g.cables.push_back({from, 0, node.id, 0});
        }
        g.nodes.push_back(node);
        m_document->setGraph(g, tr("Insert %1").arg(name));
        return;
    }
    if (Effect::graphOnly(e.type) || e.type == Effect::Blending) {
        statusBar()->showMessage(tr("%1 works only as a node of a graph wired by hand (Node View > Wire the Graph by Hand).").arg(name), 8000);
        return;
    }
    if (layer() < 0 || layer() >= scene.layers.size() || !scene.layers[layer()].holdsDrawings()) {
        statusBar()->showMessage(tr("An effect goes on a drawing layer: select one first."), 6000);
        return;
    }
    QVector<Effect> effects = scene.layers[layer()].effects;
    effects.push_back(e);
    m_document->setEffects(layer(), effects, tr("Insert %1").arg(name));
}
