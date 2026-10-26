#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>

void Canvas::pickColourAt(QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    QPointF const at = toScene(widgetPos);
    QVector<int> const order = scene.drawOrder(m_frame);
    for (int place = int(order.size()) - 1; place >= 0; --place) {
        int const l = order[place];
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(m_frame);
        if (!scene.shows(l) || !layer.holdsDrawings() || layer.showsSymbol() || index < 0 || (!m_dropperAllLayers && l != m_layer))
            continue;
        QTransform const world = worldOf(l, m_frame);
        if (!world.isInvertible()) continue;
        QPointF const p = world.inverted().map(at);
        Drawing const drawing = shown(l, m_frame, layerDrawing(l, index));
        auto seen = [&](int swatch, QColor const& color, float width) {
            return !m_dropperNoTransparency || (width > 0.0f && scene.resolve(swatch, color).alpha() > 0);
        };
        auto picture = [&](Picture const& image) {
            if (image.image.isNull() || !image.placement.isInvertible()) return false;
            QPointF const local = image.placement.inverted().map(p);
            if (!image.clip.isEmpty()) {
                QPainterPath clip;
                for (QPolygonF const& ring : image.clip) clip.addPolygon(ring);
                if (!clip.contains(local)) return false;
            }
            if (image.size.isEmpty()) return false;
            QPoint const pixel(int(std::floor(local.x() * image.image.width() / image.size.width())),
                               int(std::floor(local.y() * image.image.height() / image.size.height())));
            if (!image.image.rect().contains(pixel)) return false;
            QColor const color = image.image.pixelColor(pixel);
            if (m_dropperNoTransparency && color.alpha() == 0) return false;
            tookColour(Scene::kNoSwatch, color);
            return true;
        };
        for (int sheet = Drawing::SheetCount - 1; sheet >= 0; --sheet) {
            if (l == m_layer && !m_previewAllArt) {
                if (sheet != sheetOf(m_art) && !(bothArtsNow() && (sheet == Drawing::LineSheet || sheet == Drawing::ColourSheet)))
                    continue;
            } else if (!layer.sheetShown(sheet)) continue;
            struct Item { int order, kind, index; };
            QVector<Item> items;
            for (int f = 0; f < drawing.fillsOf(sheet).size(); ++f)
                items.push_back({drawing.fillsOf(sheet)[f].order, 0, f});
            for (int s = 0; s < drawing.strokesOf(sheet).size(); ++s)
                items.push_back({drawing.strokesOf(sheet)[s].order, 1, s});
            if (sheet == Drawing::LineSheet)
                for (int i = 0; i < drawing.pictures.size(); ++i)
                    if (!drawing.pictures[i].painted && drawing.pictures[i].order >= 0)
                        items.push_back({drawing.pictures[i].order, -1, i});
            std::sort(items.begin(), items.end(), [](Item const& a, Item const& b) {
                return a.order != b.order ? a.order > b.order : a.kind != b.kind ? a.kind > b.kind : a.index > b.index;
            });
            for (Item const& item : items) {
                if (item.kind < 0) { if (picture(drawing.pictures[item.index])) return; }
                else if (item.kind == 0) {
                    Fill const& fill = drawing.fillsOf(sheet)[item.index];
                    if (fill.contains(p) && seen(fill.swatch, fill.color, 1.0f)) {
                        tookColour(fill.swatch, scene.resolve(fill.swatch, fill.color));
                        return;
                    }
                } else {
                    Stroke const& stroke = drawing.strokesOf(sheet)[item.index];
                    if (stroke.hit(p, 2.0 / m_zoom) && seen(stroke.swatch, stroke.color, stroke.width)) {
                        tookColour(stroke.swatch, scene.resolve(stroke.swatch, stroke.color));
                        return;
                    }
                }
            }
            for (int i = int(drawing.pictures.size()) - 1; i >= 0; --i)
                if (drawing.pictures[i].painted && drawing.pictures[i].sheet == sheet && picture(drawing.pictures[i])) return;
        }
        for (int i = int(drawing.pictures.size()) - 1; i >= 0; --i)
            if (!drawing.pictures[i].painted && drawing.pictures[i].order < 0 && picture(drawing.pictures[i])) return;
    }
    emit message(tr("There is nothing there to take a colour from."));
}
