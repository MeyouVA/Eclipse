#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

#include <algorithm>

namespace {
bool hasName(QVector<BoardLayer> const& items, QString const& name, qsizetype except = -1)
{
    for (qsizetype i = 0; i < items.size(); ++i)
        if (i != except && items[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
}

void BoardDocument::appendLibraryLayer(QVector<BoardLayer>& items, BoardLayer layer)
{
    QString const base = layer.name.trimmed().left(BoardProject::maxLibraryName).trimmed();
    layer.name = base;
    for (int suffix = 2; hasName(items, layer.name); ++suffix) {
        QString const tail = QStringLiteral(" (%1)").arg(suffix);
        layer.name = base.left(BoardProject::maxLibraryName - tail.size()).trimmed() + tail;
    }
    layer.visible = true;
    layer.locked = false;
    items.append(std::move(layer));
}

bool BoardDocument::addLayerToLibrary(int shot, int panel, int layer)
{
    if (!hasLayer(shot, panel, layer) || m_project.library.size() >= BoardProject::maxLibraryItems)
        return false;
    auto const& current = m_project.shots.at(shot).panels.at(panel).layers.at(layer);
    if (!BoardProjectJson::supportedDrawing(current.drawing))
        return false;
    auto next = m_project;
    appendLibraryLayer(next.library, current);
    if (next.toBytes().isEmpty())
        return false;
    apply(std::move(next), tr("Add Layer to Library"));
    return true;
}

bool BoardDocument::placeLibraryItem(int item, int shot, int panel, int before)
{
    if (item < 0 || item >= m_project.library.size() || !hasPanel(shot, panel))
        return false;
    auto next = m_project;
    auto& layers = next.shots[shot].panels[panel].layers;
    layers.insert(std::clamp(qsizetype(before), qsizetype(0), layers.size()), m_project.library.at(item));
    if (next.toBytes().isEmpty())
        return false;
    apply(std::move(next), tr("Place Library Item"));
    return true;
}

bool BoardDocument::renameLibraryItem(int item, QString const& name)
{
    QString const trimmed = name.trimmed();
    if (item < 0 || item >= m_project.library.size() || trimmed.isEmpty() || trimmed.size() > BoardProject::maxLibraryName
        || trimmed == m_project.library.at(item).name || hasName(m_project.library, trimmed, item))
        return false;
    auto next = m_project;
    next.library[item].name = trimmed;
    if (next.toBytes().isEmpty())
        return false;
    apply(std::move(next), tr("Rename Library Item"));
    return true;
}

bool BoardDocument::removeLibraryItem(int item)
{
    if (item < 0 || item >= m_project.library.size())
        return false;
    auto next = m_project;
    next.library.removeAt(item);
    apply(std::move(next), tr("Remove Library Item"));
    return true;
}
