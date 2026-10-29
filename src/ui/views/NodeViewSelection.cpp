#include "ui/views/NodeView.h"
#include <QHelpEvent>
#include <QToolTip>
#include "model/document/Commands.h"
#include "actions/theme/InterfaceColours.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <cmath>

QVector<int> NodeView::pickedLayers() const
{
    QVector<int> layers;
    for (int id : m_layerPicked)
        if (int const layer = m_document->scene().layerIndex(id); layer >= 0 && nodeShown(layer))
            layers << layer;
    return layers;
}

void NodeView::startBoxSelection(QMouseEvent* event)
{
    m_pressPos = m_dragNow = event->position();
    m_drag = DragBox;
    m_boxModifiers = event->modifiers();
    m_boxBefore = graphMode() ? m_graphPicked : m_layerPicked;
    if (!(m_boxModifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)))
        deselectAll();
}

void NodeView::updateBoxSelection()
{
    QRectF const box = QRectF(m_pressPos, m_dragNow).normalized();
    QVector<int> picked = m_boxModifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier) ? m_boxBefore : QVector<int>();
    auto include = [&](int id, QRectF const& node) {
        if (node.isEmpty() || !box.intersects(node))
            return;
        if ((m_boxModifiers & Qt::AltModifier) || ((m_boxModifiers & Qt::ControlModifier) && m_boxBefore.contains(id)))
            picked.removeAll(id);
        else if (!picked.contains(id))
            picked << id;
    };
    if (graphMode()) {
        for (GraphNode const& node : m_document->scene().graph.nodes)
            if (node.group == m_graphGroup)
                include(node.id, graphNodeRect(node.id));
        m_graphPicked = picked;
        m_graphNode = picked.isEmpty() ? 0 : picked.last();
        m_graphCable = -1;
    } else {
        Scene const& scene = m_document->scene();
        for (int layer = 0; layer < scene.layers.size(); ++layer)
            if (nodeShown(layer))
                include(scene.layers[layer].id, nodeRect(layer));
        m_layerPicked = picked;
        m_cable = -1;
    }
    update();
}

void NodeView::paintBoxSelection(QPainter& painter)
{
    if (m_drag != DragBox)
        return;
    painter.save();
    QColor colour = InterfaceColours::get("nodeSelected");
    painter.setPen(QPen(colour, 1, Qt::DashLine));
    colour.setAlpha(35);
    painter.setBrush(colour);
    painter.drawRect(QRectF(m_pressPos, m_dragNow).normalized());
    painter.restore();
}

void NodeView::selectAll()
{
    deselectAll();
    if (graphMode()) {
        for (GraphNode const& node : m_document->scene().graph.nodes)
            if (node.group == m_graphGroup)
                m_graphPicked << node.id;
        m_graphNode = m_graphPicked.isEmpty() ? 0 : m_graphPicked.last();
    } else {
        Scene const& scene = m_document->scene();
        for (int layer = 0; layer < scene.layers.size(); ++layer)
            if (nodeShown(layer))
                m_layerPicked << scene.layers[layer].id;
    }
    update();
}

void NodeView::deselectAll()
{
    m_graphPicked.clear();
    m_layerPicked.clear();
    m_graphNode = 0;
    m_graphCable = m_cable = -1;
    update();
}

void NodeView::onActionInvertSelection()
{
    QVector<int> const before = graphMode() ? m_graphPicked : m_layerPicked;
    selectAll();
    QVector<int>& picked = graphMode() ? m_graphPicked : m_layerPicked;
    picked.removeIf([&](int id) { return before.contains(id); });
    if (graphMode())
        m_graphNode = picked.isEmpty() ? 0 : picked.last();
    update();
}

void NodeView::onActionCentreOnSelection()
{
    QRectF selected;
    if (graphMode()) {
        for (int id : m_graphPicked)
            selected = selected.united(graphNodeRect(id));
        if (selected.isEmpty() && m_layer >= 0 && m_layer < m_document->scene().layers.size()) {
            int const id = m_document->scene().graph.layerNode(m_document->scene().layers[m_layer].id);
            if (GraphNode const* node = m_document->scene().graph.node(id)) {
                if (node->group != m_graphGroup)
                    enterGraphGroup(node->group);
                pickGraphNode(id);
                selected = graphNodeRect(id);
            }
        }
    } else {
        for (int layer : pickedLayers())
            selected = selected.united(nodeRect(layer));
        if (selected.isEmpty() && nodeShown(m_layer))
            selected = nodeRect(m_layer);
    }
    if (!selected.isEmpty()) {
        m_pan += QPointF(width() / 2.0, height() / 2.0) - selected.center();
        m_centred = true;
        update();
    }
}

QPointF NodeView::nodeDragDelta(QPointF widgetPos) const
{
    QPointF delta = (widgetPos - m_pressPos) / m_zoom;
    double bestX = 8.0, bestY = 8.0, adjustX = 0.0, adjustY = 0.0;
    auto consider = [&](double correction, double& best, double& adjustment) {
        double const distance = std::abs(correction * m_zoom);
        if (distance < best) { best = distance; adjustment = correction; }
    };
    Scene const& scene = m_document->scene();
    if (graphMode()) {
        NodeGraph const& graph = scene.graph;
        QSet<int> moving(m_graphPicked.cbegin(), m_graphPicked.cend());
        if (m_graphNode) moving.insert(m_graphNode);
        QVector<GraphNode const*> selected, stationary;
        for (GraphNode const& node : graph.nodes)
            if (node.group == m_graphGroup)
                (moving.contains(node.id) ? selected : stationary).push_back(&node);
        for (GraphNode const* from : selected)
            for (GraphNode const* other : stationary)
                consider(other->at.y() - (from->at.y() + delta.y()), bestY, adjustY);
        for (GraphCable const& cable : graph.cables) {
            bool const fromMoving = moving.contains(cable.from), toMoving = moving.contains(cable.to);
            if (fromMoving == toMoving || graphNodeRect(cable.from).isNull() || graphNodeRect(cable.to).isNull()) continue;
            double const fromX = graphOutPort(cable.from, cable.fromPort).x();
            double const toX = graphInPort(cable.to, cable.toPort).x();
            consider((fromMoving ? toX - fromX : fromX - toX) / m_zoom - delta.x(), bestX, adjustX);
        }
    } else {
        QVector<int> const picked = pickedLayers();
        QSet<int> const moving(picked.cbegin(), picked.cend());
        QVector<int> stationary;
        for (int other = 0; other < scene.layers.size(); ++other)
            if (nodeShown(other) && !moving.contains(other)) stationary.push_back(other);
        QVector<QPointF> const at = positions();
        for (int layer : picked) {
            for (int other : stationary) {
                consider(at[other].y() - (at[layer].y() + delta.y()), bestY, adjustY);
                if (scene.parentIndex(layer) == other || scene.parentIndex(other) == layer)
                    consider((nodeRect(other).center().x() - nodeRect(layer).center().x()) / m_zoom - delta.x(), bestX, adjustX);
            }
        }
    }
    return delta + QPointF(adjustX, adjustY);
}

void NodeView::movePickedNodes(QPointF delta)
{
    if (graphMode()) {
        NodeGraph graph = m_document->scene().graph;
        bool changed = false;
        for (int id : m_graphPicked)
            if (int const index = graph.nodeIndex(id); index >= 0 && graph.nodes[index].group == m_graphGroup) {
                graph.nodes[index].at += delta;
                changed = true;
            }
        if (changed)
            m_document->setGraph(graph, tr("Move Nodes"));
    } else {
        QVector<int> const layers = pickedLayers();
        if (layers.isEmpty())
            return;
        QVector<QPointF> const starts = positions();
        m_document->beginMacro(tr("Move Nodes"));
        for (int layer : layers)
            m_document->setNodePos(layer, starts[layer] + delta);
        m_document->endMacro();
    }
}

bool NodeView::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip && graphMode()) {
        // The port under the pointer: what it is for.
        QPoint const at = static_cast<QHelpEvent*>(event)->pos();
        for (GraphNode const& n : m_document->scene().graph.nodes) {
            if (n.group != m_graphGroup || n.kind == GraphNode::Note)
                continue;
            for (int input = 0; input < 2; ++input)
                for (int port = 0; port < (input ? n.inputs() : n.outputs()); ++port) {
                    QPointF const place = input ? graphInPort(n.id, port) : graphOutPort(n.id, port);
                    if (QLineF(place, QPointF(at)).length() <= 8.0) {
                        QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), graphPortName(n, port, input), this);
                        return true;
                    }
                }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        int const code = key->key();
        bool const nudge = (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier)
            && (code == Qt::Key_Left || code == Qt::Key_Right || code == Qt::Key_Up || code == Qt::Key_Down);
        if (code == Qt::Key_Space || code == Qt::Key_Escape || nudge || (code == Qt::Key_O && key->modifiers() == Qt::NoModifier)) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void NodeView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        m_spacePan = true;
        setCursor(Qt::OpenHandCursor);
    } else if (event->key() == Qt::Key_Escape) {
        if (m_drag == DragBox) {
            (graphMode() ? m_graphPicked : m_layerPicked) = m_boxBefore;
            m_graphNode = m_graphPicked.isEmpty() ? 0 : m_graphPicked.last();
        }
        m_drag = DragNone;
        m_unplugged = -1;
        update();
    } else if (event->key() == Qt::Key_O && event->modifiers() == Qt::NoModifier) {
        onActionCentreOnSelection();
    } else {
        if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier) {
            QWidget::keyPressEvent(event);
            return;
        }
        QPointF delta;
        switch (event->key()) {
        case Qt::Key_Left: delta.setX(-1); break;
        case Qt::Key_Right: delta.setX(1); break;
        case Qt::Key_Up: delta.setY(-1); break;
        case Qt::Key_Down: delta.setY(1); break;
        default: QWidget::keyPressEvent(event); return;
        }
        movePickedNodes(delta * (event->modifiers() & Qt::ShiftModifier ? 10 : 1));
    }
    event->accept();
}

void NodeView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan = false;
        unsetCursor();
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void NodeView::focusOutEvent(QFocusEvent* event)
{
    m_spacePan = false;
    unsetCursor();
    QWidget::focusOutEvent(event);
}
