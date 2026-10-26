#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

Drawing Canvas::selectionDrawing() const
{
    Drawing const* const current = currentDrawing();
    if (!current)
        return {};
    Drawing view = *current;
    if (m_tool != Select && m_tool != Cutter && m_tool != Reposition)
        return view;
    Layer const& layer = m_document->scene().layers[m_layer];
    int const index = layer.drawingAt(m_frame);
    if (index < 0 || layer.morphing(m_frame) || !Deform::movesPoints(layer.poseAt(m_frame).points, index))
        return view;
    Drawing const& posed = *pointViewOf(layer.drawings[index], index);
    view.strokes.clear();
    view.fills.clear();
    if (bothArtsNow()) {
        for (int sheet : {int(Drawing::ColourSheet), int(Drawing::LineSheet)}) {
            for (Stroke stroke : posed.strokesOf(sheet)) {
                stroke.sheet = sheet;
                view.strokes.push_back(stroke);
            }
            for (Fill fill : posed.fillsOf(sheet)) {
                fill.sheet = sheet;
                view.fills.push_back(fill);
            }
        }
    } else {
        view.strokes = posed.strokesOf(sheetOf(m_art));
        view.fills = posed.fillsOf(sheetOf(m_art));
    }
    return view;
}
