// Eclipse - the range and curves shown in a project's Function view.
#include "ui/timeline/FunctionView.h"
#include "model/document/Commands.h"
#include <QJsonArray>
#include <QJsonObject>

QJsonObject FunctionGraph::viewState() const
{
    QJsonArray shown, hidden, others, columns;
    for (bool on : m_shown) shown.append(on);
    for (int layer : m_hidden) hidden.append(layer);
    for (int layer : m_others) others.append(layer);
    for (int column : m_columnsShown) columns.append(column);
    return {{"first", m_first}, {"span", m_span}, {"zoomed", m_zoomed}, {"low", m_low}, {"high", m_high},
            {"shown", shown}, {"hidden", hidden}, {"others", others}, {"columns", columns}};
}

void FunctionGraph::restoreViewState(QJsonObject const& state)
{
    if (state.isEmpty())
        return;
    m_first = state["first"].toDouble();
    m_span = std::max(0.0, state["span"].toDouble());
    m_zoomed = state["zoomed"].toBool();
    m_low = state["low"].toDouble(-1);
    m_high = std::max(m_low + 1e-9, state["high"].toDouble(1));
    QJsonArray const shown = state["shown"].toArray();
    for (int i = 0; i < Timeline::ParameterCount && i < shown.size(); ++i)
        m_shown[i] = shown[i].toBool();
    auto layers = [&](QJsonValue const& value) {
        QVector<int> out;
        for (QJsonValue const& item : value.toArray())
            if (int const index = item.toInt(-1); index >= 0 && index < m_document->scene().layers.size())
                out << index;
        return out;
    };
    m_hidden = layers(state["hidden"]);
    m_others = layers(state["others"]);
    m_columnsShown.clear();
    for (QJsonValue const& item : state["columns"].toArray())
        if (int const column = item.toInt(-1); column >= 0 && column < m_document->scene().functions.size())
            m_columnsShown << column;
    update();
}
