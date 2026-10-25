// Eclipse - the document: synced, cloned and expanded layers; locking, sheets, bitmap, solo, collapsed, colour; duplicating.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

#include <algorithm>
#include <cmath>
#include <numbers>

int Document::addSyncedLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return -1;
    int at = -1;
    apply(tr("Add Synced Drawing Layer"), [&](Scene& s) { at = s.addSyncedLayer(layer); });
    return at;
}

void Document::syncLayerWith(int layer, int other)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return;
    if (other < 0 && m_scene.layers[layer].syncGroup == 0)
        return;
    apply(other < 0 ? tr("Remove Sync") : tr("Sync Layer"), [&](Scene& s) { s.syncLayerWith(layer, other); });
}

int Document::cloneLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings())
        return -1;
    int at = -1;
    apply(tr("Clone Layer"), [&](Scene& s) { at = s.cloneLayer(layer); });
    return at;
}

int Document::expandSymbol(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].showsSymbol() || !m_scene.findSymbol(m_scene.layers[layer].symbolId))
        return -1;
    Scene expanded = m_scene;
    int const at = expanded.expandSymbol(layer);
    if (at < 0)
        return -1;
    apply(tr("Expand Symbol"), [&](Scene& s) { s = expanded; });
    return at;
}

void Document::removeLayers(QVector<int> const& layers)
{
    QVector<int> gone;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !gone.contains(l))
            gone.push_back(l);
    if (gone.isEmpty() || gone.size() >= m_scene.layers.size())
        return;  // a scene keeps one layer at least
    std::sort(gone.begin(), gone.end());
    apply(gone.size() == 1 ? tr("Delete Layer") : tr("Delete Layers"), [&](Scene& s) {
        for (int i = int(gone.size()) - 1; i >= 0; --i)
            s.removeLayer(gone[i]);
    });
}

void Document::setLayerLocked(int layer, bool locked)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].locked == locked)
        return;
    apply(locked ? tr("Lock Layer") : tr("Unlock Layer"), [&](Scene& s) { s.layers[layer].locked = locked; });
}

void Document::setLayerSheetShown(int layer, int sheet, bool shown)
{
    if (layer < 0 || layer >= m_scene.layers.size() || sheet < 0 || sheet >= Drawing::SheetCount || m_scene.layers[layer].sheetShown(sheet) == shown)
        return;
    apply(shown ? tr("Show Art Layer") : tr("Hide Art Layer"), [&](Scene& s) {
        if (shown)
            s.layers[layer].hiddenSheets &= ~(1 << sheet);
        else
            s.layers[layer].hiddenSheets |= 1 << sheet;
    });
}

void Document::setLayerBitmap(int layer, bool bitmap)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].bitmap == bitmap)
        return;
    apply(bitmap ? tr("Make Bitmap Layer") : tr("Make Vector Layer"), [&](Scene& s) { s.layers[layer].bitmap = bitmap; });
}

void Document::setLayerSolo(int layer, bool solo)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].solo == solo)
        return;
    apply(solo ? tr("Solo Layer") : tr("Solo Off"), [&](Scene& s) { s.layers[layer].solo = solo; });
}

void Document::setLayerCollapsed(int layer, bool collapsed)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].collapsed == collapsed)
        return;
    apply(collapsed ? tr("Collapse") : tr("Expand"), [&](Scene& s) { s.layers[layer].collapsed = collapsed; });
}

void Document::setLayerColor(int layer, QColor const& color)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].trackColor == color)
        return;
    apply(color.isValid() ? tr("Change Track Colour") : tr("Default Track Colour"), [&](Scene& s) { s.layers[layer].trackColor = color; });
}

void Document::setAllLocked(bool locked, int except)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (l != except && m_scene.layers[l].locked != locked);
    if (!any)
        return;
    apply(locked ? tr("Lock Layers") : tr("Unlock Layers"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            if (l != except)
                s.layers[l].locked = locked;
    });
}

void Document::setAllVisible(bool visible, int except)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (l != except && m_scene.layers[l].visible != visible);
    if (!any)
        return;
    apply(visible ? tr("Enable Layers") : tr("Disable Layers"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            if (l != except)
                s.layers[l].visible = visible;
    });
}

void Document::setAllCollapsed(bool collapsed)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (m_scene.hasChildren(l) && m_scene.layers[l].collapsed != collapsed);
    if (!any)
        return;
    apply(collapsed ? tr("Collapse All") : tr("Expand All"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            s.layers[l].collapsed = collapsed && s.hasChildren(l);
    });
}

void Document::clearSolo()
{
    bool any = false;
    for (Layer const& l : m_scene.layers)
        any = any || l.solo;
    if (!any)
        return;
    apply(tr("Solo Off"), [&](Scene& s) {
        for (Layer& l : s.layers)
            l.solo = false;
    });
}

int Document::duplicateLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int made = -1;
    apply(tr("Duplicate Layer"), [&](Scene& s) { made = s.duplicateLayer(layer); });
    return made;
}
