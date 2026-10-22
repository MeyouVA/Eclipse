// Eclipse - the Camera view: the Zoom tool (click or drag), and zoom as a share of the frame's real size.
#include "canvas/Canvas.h"

#include <cmath>

#include "model/document/Commands.h"

double Canvas::zoomUnit() const
{
    // Paper zoom: at 100 % the frame (twelve fields across) is twelve inches wide on the screen.
    double const across = m_document->scene().width;
    return m_paperZoom && across > 0.0 ? 12.0 * m_screenPpi / across : 1.0;
}

double Canvas::zoomPercent() const
{
    return m_zoom / zoomUnit() * 100.0;
}

void Canvas::setZoomPercent(double percent)
{
    double const wanted = percent / 100.0 * zoomUnit();
    if (wanted > 0.0)
        zoomBy(wanted / m_zoom);
}

void Canvas::zoomToolDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers)
{
    // A click zooms in (out with Alt, or the other way when Tool Properties says Zoom Out); a drag up or down zooms as it goes.
    m_zoomFrom = widgetPos;
    m_zoomStart = m_zoom;
    m_zoomMoved = false;
    m_zoomOutClick = bool(modifiers & Qt::AltModifier) != m_zoomToolOut;
    m_drag = DragZoom;
}

void Canvas::zoomToolMove(QPointF widgetPos)
{
    double const dy = widgetPos.y() - m_zoomFrom.y();
    if (!m_zoomMoved && std::abs(dy) < 4.0)
        return;
    m_zoomMoved = true;
    double const wanted = std::clamp(m_zoomStart * std::exp(-dy / 150.0), m_zoomMin, m_zoomMax);
    zoomAt(m_zoomFrom, wanted / m_zoom);
}

void Canvas::zoomToolUp()
{
    m_drag = DragNone;
    if (!m_zoomMoved)
        zoomAt(m_zoomFrom, m_zoomOutClick ? 1.0 / 1.5 : 1.5);
}
