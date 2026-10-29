// Eclipse - the Camera view: layers hidden from the views only (Hide Selected / Show Hidden).
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

void Canvas::setViewHidden(QSet<int> const& ids)
{
    if (ids == m_viewHidden)
        return;
    m_viewHidden = ids;
    update();
    emit viewHiddenChanged();
}

bool Canvas::hiddenInView(int layer) const
{
    if (m_viewHidden.isEmpty())
        return false;
    Scene const& scene = m_document->scene();
    for (int guard = 0; layer >= 0 && layer < scene.layers.size() && guard < 256; ++guard, layer = scene.parentIndex(layer))
        if (m_viewHidden.contains(scene.layers[layer].id))
            return true;
    return false;
}
