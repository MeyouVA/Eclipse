// Eclipse - the Camera view: the thickness of pencil lines, apart from their points - the Pencil
// Editor's thickness points, and the thickness stencils of the Line tool and the Polyline.
#include "canvas/Canvas.h"

#include <algorithm>
#include <cmath>

#include <QLineF>

void Canvas::stencilLine(Stroke& line) const
{
    if (m_tool != Line && m_tool != Polyline)
        return;
    int const stencil = thicknessStencilOf(m_tool);
    QVector<float> const& own = m_customStencils[stencilSlot(m_tool)];
    bool const custom = stencil == StencilCount && !own.isEmpty();
    if (line.width <= 0.0f || line.closed || line.points.size() < 2 || (!custom && (stencil == StencilUniform || stencil >= StencilCount)))
        return;
    // The stencil, place by place along the line, as its thickness there (Curved Above / Below: all to the one side).
    double const lean = stencil == StencilCurvedAbove ? 1.0 : stencil == StencilCurvedBelow ? -1.0 : 0.0;
    QVector<QPair<double, ThicknessPoint>> byLength;
    for (int k = 0; k <= 32; ++k) {
        double const f = k / 32.0;
        double const share = custom ? profileThickness(own, f) : stencilThickness(stencil, f);
        byLength.push_back({f, {0.0, float(share * (1.0 + lean)), float(share * (1.0 - lean))}});
    }
    line.setThicknessByLength(byLength);
    line.simplifyThickness(0.005);
}

void Canvas::thicknessPointDown(QPointF p, Qt::KeyboardModifiers modifiers)
{
    Drawing const* const d = currentDrawing();
    // The pencil line under the pointer: on it, or a few pixels off it.
    double const off = 8.0 / std::max(m_zoom, 1e-6);
    if (d)
        for (int i = int(d->strokes.size()) - 1; i >= 0 && m_thickStroke < 0; --i) {
            Stroke const& stroke = d->strokes[i];
            if (stroke.width > 0.0f && stroke.points.size() >= 2 && stroke.hit(p, off))
                m_thickStroke = i;
        }
    if (m_thickStroke < 0) {
        emit message(tr("Press on a pencil line and pull: a thickness point is put there (or the one near is taken) - away from the line makes it "
                        "thicker there, towards it thinner. With Alt only the side pulled on moves; Alt and a click on a thickness point takes it away."));
        return;
    }
    m_eraseArt = *d;
    Stroke& line = m_eraseArt.strokes[m_thickStroke];
    if (m_opacityMode) {
        // Adjust Opacity: the point of the line nearest the press, dragged up (more solid) or down (fainter).
        QTransform const view = layerViewTransform();
        int key = 0;
        double best = 1e300;
        for (int k = 0; k < line.points.size(); ++k) {
            double const away = QLineF(view.map(line.points[k].pos), view.map(p)).length();
            if (away < best) {
                best = away;
                key = k;
            }
        }
        m_thickKey = key;
        m_opacityStart = line.points[key].opacity;
        m_thickMoved = false;
        m_thickOneSide = false;
        m_thickExisting = true;
        m_thickPress = p;
        m_drag = DragThickness;
        update();
        return;
    }
    line.ownThickness();
    double const at = line.placeAt(p);
    // A thickness point a few pixels of the screen from there along the line is taken; otherwise one is put there.
    QTransform const view = layerViewTransform();
    QPointF const here = view.map(line.pointAt(at));
    int key = -1;
    double best = 8.0;
    for (int k = 0; k < line.thickness.size(); ++k) {
        double const away = QLineF(view.map(line.pointAt(line.thickness[k].at)), here).length();
        if (away <= best) {
            best = away;
            key = k;
        }
    }
    m_thickExisting = key >= 0;
    if (key < 0)
        key = line.thicknessPointAt(at);
    m_thickKey = key;
    double const place = line.thickness[key].at;
    m_thickPos0 = line.pointAt(place);
    QPointF const way = line.directionAt(place);
    m_thickAcross = QPointF(way.y(), -way.x());     // to the left as the line goes
    m_thickOneSide = modifiers & Qt::AltModifier;
    m_thickMoved = false;
    m_thickPress = p;
    m_drag = DragThickness;
    update();
}

void Canvas::setPencilEditorOpacity(bool on)
{
    m_opacityMode = on;
    emit message(on ? tr("Adjust Opacity: the tool works only on the opacity of the pencil lines (Up/Down arrows: Adjust Thickness).")
                    : tr("Adjust Thickness: the tool works on the thickness of the pencil lines (Up/Down arrows: Adjust Opacity)."));
    emit toolSettingsChanged();
}
