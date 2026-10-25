// Eclipse - framing the selected sheet in the Perspective view.
#include "ui/views/PerspectiveView.h"

#include <algorithm>
#include <cmath>

void PerspectiveView::onActionFrameSelected()
{
    QPolygonF const quad = layerQuad(m_layer);
    if (quad.isEmpty())
        return;
    QRectF const box = quad.boundingRect();
    double const factor = std::min(width() * 0.7 / std::max(box.width(), 1.0), height() * 0.7 / std::max(box.height(), 1.0));
    double const zoom = std::clamp(m_zoom * factor, 0.1, 20.0);
    m_pan = (m_pan - box.center() + QPointF(width() / 2.0, height() / 2.0)) * (zoom / m_zoom);
    m_zoom = zoom;
    update();
}
