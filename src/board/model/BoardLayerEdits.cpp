#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

bool BoardDocument::addLayer(int shot, int panel, QString const& name, BoardLayer::Kind kind, int before)
{
    if (!hasPanel(shot, panel) || name.trimmed().isEmpty())
        return false;
    auto next = m_project;
    auto& layers = next.shots[shot].panels[panel].layers;
    layers.insert(before < 0 ? layers.size() : std::min(qsizetype(before), layers.size()),
                  {name.trimmed(), kind, true, {}});
    apply(std::move(next), tr("Add Layer"));
    return true;
}

bool BoardDocument::removeLayer(int shot, int panel, int layer)
{
    if (!hasLayer(shot, panel, layer))
        return false;
    auto next = m_project;
    next.shots[shot].panels[panel].layers.removeAt(layer);
    apply(std::move(next), tr("Remove Layer"));
    return true;
}

bool BoardDocument::duplicateLayer(int shot, int panel, int layer)
{
    if (!hasLayer(shot, panel, layer))
        return false;
    auto next = m_project;
    auto& layers = next.shots[shot].panels[panel].layers;
    BoardLayer copy = layers[layer];
    copy.name = tr("%1 Copy").arg(copy.name);
    layers.insert(layer, std::move(copy));
    apply(std::move(next), tr("Duplicate Layer"));
    return true;
}

bool BoardDocument::renameLayer(int shot, int panel, int layer, QString const& name)
{
    QString const trimmed = name.trimmed();
    if (!hasLayer(shot, panel, layer) || trimmed.isEmpty() || trimmed == m_project.shots[shot].panels[panel].layers[layer].name)
        return false;
    auto next = m_project;
    next.shots[shot].panels[panel].layers[layer].name = trimmed;
    apply(std::move(next), tr("Rename Layer"));
    return true;
}

bool BoardDocument::moveLayer(int shot, int panel, int layer, int to)
{
    if (!hasLayer(shot, panel, layer) || !hasLayer(shot, panel, to) || layer == to)
        return false;
    auto next = m_project;
    next.shots[shot].panels[panel].layers.move(layer, to);
    apply(std::move(next), tr("Reorder Layers"));
    return true;
}

bool BoardDocument::setLayerFlags(int shot, int panel, int layer, bool visible, bool locked)
{
    if (!hasLayer(shot, panel, layer))
        return false;
    auto const& current = m_project.shots[shot].panels[panel].layers[layer];
    if (visible == current.visible && locked == current.locked)
        return false;
    auto next = m_project;
    auto& changed = next.shots[shot].panels[panel].layers[layer];
    changed.visible = visible;
    changed.locked = locked;
    apply(std::move(next), tr("Change Layer Properties"));
    return true;
}

bool BoardDocument::setLayerDrawing(int shot, int panel, int layer, Drawing const& drawing, QString const& label)
{
    if (!hasLayer(shot, panel, layer) || !BoardProjectJson::supportedDrawing(drawing))
        return false;
    auto next = m_project;
    next.shots[shot].panels[panel].layers[layer].drawing = drawing;
    apply(std::move(next), label.isEmpty() ? tr("Edit Layer Artwork") : label);
    return true;
}
