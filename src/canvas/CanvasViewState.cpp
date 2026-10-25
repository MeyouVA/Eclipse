// Eclipse - project view settings independent of animation edits.
#include "canvas/Canvas.h"
#include <QJsonObject>
#include <cmath>

QJsonObject Canvas::viewState() const
{
    return {{"zoom", m_zoom}, {"panX", m_pan.x()}, {"panY", m_pan.y()}, {"rotation", m_viewRotation},
            {"mirrored", m_mirror}, {"drawing", m_drawingView}, {"otherSet", m_otherViewSet},
            {"otherZoom", m_otherZoom}, {"otherX", m_otherPan.x()}, {"otherY", m_otherPan.y()}, {"otherRotation", m_otherRotation},
            {"mode", int(m_viewMode)}, {"art", int(m_art)}, {"allArt", m_previewAllArt}, {"onion", m_onionSkin},
            {"grid", m_grid}, {"gridKind", m_gridKind}, {"backlight", m_backlight}};
}

void Canvas::restoreViewState(QJsonObject const& state)
{
    if (state.isEmpty())
        return;
    auto number = [&](char const* name, double fallback) {
        double const value = state[name].toDouble(fallback);
        return std::isfinite(value) ? value : fallback;
    };
    setDrawingView(state["drawing"].toBool());
    m_zoom = std::clamp(number("zoom", 1.0), m_zoomMin, m_zoomMax);
    m_pan = QPointF(number("panX", 0), number("panY", 0));
    m_viewRotation = number("rotation", 0);
    m_mirror = state["mirrored"].toBool();
    m_otherViewSet = state["otherSet"].toBool();
    m_otherZoom = std::clamp(number("otherZoom", 1.0), m_zoomMin, m_zoomMax);
    m_otherPan = QPointF(number("otherX", 0), number("otherY", 0));
    m_otherRotation = number("otherRotation", 0);
    m_viewMode = ViewMode(std::clamp(state["mode"].toInt(), 0, 3));
    m_art = Art(std::clamp(state["art"].toInt(), 0, 3));
    m_previewAllArt = state["allArt"].toBool(true);
    m_onionSkin = state["onion"].toBool(true);
    m_grid = state["grid"].toBool();
    m_gridKind = state["gridKind"].toInt();
    m_backlight = state["backlight"].toBool();
    m_fitted = false;
    settingChanged();
    emit artChanged(int(m_art));
    emit onionChanged();
    emit zoomChanged();
}
