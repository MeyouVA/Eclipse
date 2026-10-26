#include "canvas/Canvas.h"

Drawing Canvas::selectionDrawing() const
{
    Drawing const* const drawing = currentDrawing();
    return drawing ? *drawing : Drawing();
}
