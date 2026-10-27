#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include <QPainter>
#include <QPainterPath>

void Canvas::paintLockedOutline(QPainter& painter, int index, Drawing const& drawing) const
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[index];
    painter.save();
    if (!m_drawingView && scene.cameraIndex() >= 0 && !drawing.isEmpty()) {
        QRectF const bounds = drawing.bounds().adjusted(-2, -2, 2, 2);
        QPolygonF const clipped = scene.cameraClip(index, m_frame, bounds);
        if (clipped.isEmpty()) {
            painter.restore();
            return;
        }
        if (clipped != QPolygonF(bounds)) {
            QPainterPath area;
            area.addPolygon(clipped);
            painter.setClipPath(area, Qt::IntersectClip);
        }
    }
    QPen edge(layer.trackColor.isValid() ? layer.trackColor : QColor(200, 70, 60));
    edge.setCosmetic(true);
    painter.setPen(edge);
    painter.setBrush(Qt::NoBrush);
    auto shownSheet = [&](int sheet) {
        if (layer.showsSymbol())
            return true;
        if (index == m_layer && !m_previewAllArt)
            return sheet == sheetOf(m_art) || (bothArtsNow() && (sheet == Drawing::LineSheet || sheet == Drawing::ColourSheet));
        return layer.sheetShown(sheet);
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        if (!shownSheet(sheet))
            continue;
        for (Fill const& fill : drawing.fillsOf(sheet))
            for (QPolygonF const& outline : fill.outlines)
                painter.drawPolygon(outline);
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            QPolygonF line;
            for (StrokePoint const& point : stroke.path())
                line << point.pos;
            if (stroke.closed)
                painter.drawPolygon(line);
            else
                painter.drawPolyline(line);
        }
    }
    for (Picture const& picture : drawing.pictures) {
        if ((picture.painted && !shownSheet(picture.sheet)) || (!picture.painted && !shownSheet(Drawing::LineSheet)))
            continue;
        painter.save();
        painter.setTransform(picture.placement, true);
        if (picture.clip.isEmpty())
            painter.drawRect(QRectF(QPointF(), QSizeF(picture.size)));
        else
            for (QPolygonF const& outline : picture.clip)
                painter.drawPolygon(outline);
        painter.restore();
    }
    painter.restore();
}
