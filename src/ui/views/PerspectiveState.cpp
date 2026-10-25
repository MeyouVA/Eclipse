#include "ui/views/PerspectiveView.h"
#include <cmath>

QJsonObject PerspectiveView::viewState() const
{
    return QJsonObject{{"zoom", m_zoom}, {"x", m_pan.x()}, {"y", m_pan.y()}, {"yaw", m_yaw}, {"pitch", m_pitch},
                       {"viewX", m_viewX}, {"viewY", m_viewY}, {"viewZ", m_viewZ},
                       {"near", m_nearDistance}, {"far", m_farDistance}};
}

void PerspectiveView::restoreViewState(QJsonObject const& state)
{
    if (state.isEmpty())
        return;
    auto number = [&state](char const* key, double fallback = 0.0) {
        double const value = state[QLatin1String(key)].toDouble(fallback);
        return std::isfinite(value) ? value : fallback;
    };
    setOrbit(number("yaw", -32), number("pitch", 18));
    restoreView(number("zoom", 1), QPointF(number("x"), number("y")));
    m_viewX = number("viewX");
    m_viewY = number("viewY");
    m_viewZ = number("viewZ");
    if (!setClippingPlanes(number("near", 1.0), number("far", 100000.0)))
        setClippingPlanes(1.0, 100000.0);
    update();
}
