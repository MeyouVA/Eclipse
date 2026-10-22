// Eclipse - the Node View's graph: the mouse - press, drag, release, double click, right-click menu.
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
    } else if (n->kind == GraphNode::Note) {
        bool ok = false;
        QString const text = QInputDialog::getMultiLineText(this, tr("Note"), tr("What it says:"), n->name, &ok);
        if (ok) {
            NodeGraph g = m_document->scene().graph;
            g.nodes[g.nodeIndex(hit)].name = text;
            m_document->setGraph(g, tr("Edit Note"));
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
    adder(tr("Note"), GraphNode::Note);
    if (m_graphGroup == 0)
        adder(tr("Display"), GraphNode::Display);
    QMenu* const effects = add->addMenu(tr("Effect"));
    for (int t = 0; t < Effect::TypeCount; ++t) {
        if (t == Effect::Particles || t == Effect::MotionBlur)
            continue;   // (particles and motion blur work from a layer's drawings on other frames: they stay layer effects)
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
        if (layer.kind == Layer::Peg || layer.kind == Layer::Camera || layer.kind == Layer::Sound || layer.kind == Layer::Group || layer.kind == Layer::Bone || layer.kind == Layer::Annotation)
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
        if (n->kind == GraphNode::LayerNode && !pictureless(*n)) {
            // Which of the layer's art layers the node gives (the Line Art, Colour Art, Underlay and Overlay nodes).
            QMenu* const arts = menu.addMenu(tr("Art Layers"));
            QString const names[4] = {tr("Underlay"), tr("Colour Art"), tr("Line Art"), tr("Overlay")};
            for (int sheet = Drawing::SheetCount - 1; sheet >= 0; --sheet) {
                QAction* const a = arts->addAction(names[sheet]);
                a->setCheckable(true);
                a->setChecked(n->sheets & (1 << sheet));
                connect(a, &QAction::toggled, this, [this, hit, sheet](bool on) {
                    NodeGraph g = m_document->scene().graph;
                    int const i = g.nodeIndex(hit);
                    if (i < 0)
                        return;
                    g.nodes[i].sheets = on ? (g.nodes[i].sheets | (1 << sheet)) : (g.nodes[i].sheets & ~(1 << sheet));
                    m_document->setGraph(g, tr("Art Layers of Node"));
                });
            }
        }
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
