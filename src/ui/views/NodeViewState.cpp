#include "ui/views/NodeView.h"
#include "model/document/Commands.h"
#include <QJsonArray>

void NodeView::onActionNavigator(QString const& position)
{
    if (position == QLatin1String("hide")) {
        setNavigatorShown(false);
        return;
    }
    bool okay = false;
    int const corner = position.toInt(&okay);
    if (okay) {
        setNavigatorCorner(corner);
        setNavigatorShown(true);
    }
}

void NodeView::onActionCableStyle(QString const& style)
{
    setCableType(style == QLatin1String("line") ? 1 : style == QLatin1String("straight") ? 2 : 0);
}

void NodeView::setNavigatorCorner(int corner)
{
    m_navigatorCorner = std::clamp(corner, 0, 3);
    update();
}

QJsonObject NodeView::viewState() const
{
    QJsonArray layers, nodes;
    for (int id : m_layerPicked) layers.append(id);
    for (int id : m_graphPicked) nodes.append(id);
    return {{"zoom", m_zoom}, {"x", m_pan.x()}, {"y", m_pan.y()}, {"group", m_groupId}, {"graphGroup", m_graphGroup},
            {"navigator", m_navigator}, {"navigatorCorner", m_navigatorCorner}, {"straightCables", m_options.cableType == 1}, {"cableType", m_options.cableType},
            {"portFiltering", m_showFilters}, {"thumbnailPattern", m_thumbPattern},
            {"selectedLayers", layers}, {"selectedNodes", nodes}, {"activeNode", m_graphNode}};
}

void NodeView::restoreViewState(QJsonObject const& state)
{
    if (state.isEmpty())
        return;
    int const group = state["group"].toInt();
    int const groupIndex = m_document->scene().layerIndex(group);
    m_groupId = groupIndex >= 0 && m_document->scene().layers[groupIndex].kind == Layer::Group ? group : 0;
    GraphNode const* const node = m_document->scene().graph.node(state["graphGroup"].toInt());
    m_graphGroup = node && node->kind == GraphNode::Group ? node->id : 0;
    deselectAll();
    for (QJsonValue const& value : state["selectedLayers"].toArray())
        if (int const index = m_document->scene().layerIndex(value.toInt()); index >= 0 && nodeShown(index) && !m_layerPicked.contains(value.toInt()))
            m_layerPicked << value.toInt();
    for (QJsonValue const& value : state["selectedNodes"].toArray())
        if (GraphNode const* selected = m_document->scene().graph.node(value.toInt()); selected && selected->group == m_graphGroup && !m_graphPicked.contains(selected->id))
            m_graphPicked << selected->id;
    m_graphNode = m_graphPicked.contains(state["activeNode"].toInt()) ? state["activeNode"].toInt() : m_graphPicked.isEmpty() ? 0 : m_graphPicked.last();
    setNavigatorShown(state["navigator"].toBool(m_navigator));
    setNavigatorCorner(state["navigatorCorner"].toInt(m_navigatorCorner));
    setCableType(state.contains(QStringLiteral("cableType")) ? state["cableType"].toInt() : state["straightCables"].toBool(m_options.cableType == 1) ? 1 : 0);
    m_showFilters = state["portFiltering"].toBool(m_showFilters);
    m_thumbPattern = state["thumbnailPattern"].toBool(m_thumbPattern);
    restoreView(state["zoom"].toDouble(1), QPointF(state["x"].toDouble(), state["y"].toDouble()));
}
