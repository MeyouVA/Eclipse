#include "canvas/Canvas.h"
#include "model/document/Commands.h"

bool Canvas::canEditDrawing() const
{
    Scene const& scene = m_document->scene();
    return !m_playing && m_layer >= 0 && m_layer < scene.layers.size() && m_frame >= 0 && m_frame < scene.frameCount()
           && scene.layers[m_layer].holdsDrawings() && !scene.layers[m_layer].showsSymbol() && !scene.layers[m_layer].locked;
}

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
