#include "canvas/Canvas.h"

void Canvas::cancelInput()
{
    m_drag = DragNone;
    m_mouseButton = Qt::NoButton;
    m_tabletActive = false;
    m_bending = false;
    m_bendTail = false;
    m_dragTransform = QTransform();
    m_stroke = Stroke();
    if (m_tabletTool >= 0) {
        m_tool = Tool(m_tabletTool);
        m_tabletTool = -1;
    }
    finishPolyline(false);
    closeMorphMacro();
    updateCursor();
    update();
}
