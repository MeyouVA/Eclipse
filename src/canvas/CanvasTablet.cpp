#include "canvas/Canvas.h"
#include <QTabletEvent>

void Canvas::tabletEvent(QTabletEvent* event)
{
    if (event->type() == QEvent::TabletPress) {
        if (m_tabletActive || m_mouseButton != Qt::NoButton)
            cancelInput();
        if (m_leader)
            followSettings(*m_leader);
        if (event->pointerType() == QPointingDevice::PointerType::Eraser && (m_tool == Brush || m_tool == Pencil)) {
            m_tabletTool = int(m_tool);
            m_tool = Eraser;
            updateCursor();
        }
        m_tabletActive = true;
        setFocus();
    }
    if (!m_tabletActive && !(m_bending && event->type() == QEvent::TabletMove)) {
        event->accept();
        return;
    }
    float pressure = pressureFor(float(std::clamp(event->pressure(), 0.0, 1.0)));
    if (m_tool == Brush && m_tiltSensitivity > 0)
        pressure = tiltedPressure(pressure, event->xTilt(), event->yTilt(), m_tiltSensitivity);
    switch (event->type()) {
    case QEvent::TabletPress:
        m_tabletPressure = pressure;
        penDown(event->position(), pressure, event->button() == Qt::MiddleButton, event->modifiers());
        break;
    case QEvent::TabletMove:
        m_tabletPressure = pressure;
        if (m_drag != DragNone)
            penMove(event->position(), pressure, event->modifiers());
        break;
    case QEvent::TabletRelease:
        if (m_drag != DragNone) {
            if (event->position() != m_lastWidgetPos || m_drag == DragShape)
                penMove(event->position(), event->pressure() > 0 ? pressure : m_tabletPressure, event->modifiers());
            penUp();
        }
        closeMorphMacro();
        m_tabletActive = false;
        if (m_tabletTool >= 0) {
            m_tool = Tool(m_tabletTool);
            m_tabletTool = -1;
            updateCursor();
        }
        break;
    default: break;
    }
    event->accept();
}
