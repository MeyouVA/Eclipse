// Eclipse - the Camera view: the paint bucket on bitmap art (tolerance, overlap under the edge, what it looks at).
#include "canvas/Canvas.h"

#include <QPainter>

#include "model/document/Commands.h"

#include <cmath>

namespace {

// Everything the drawing shows on the target's pixels, its art layers in their order: what the bucket looks at for "every art layer".
QImage everyArtLayer(Drawing const& drawing, Scene const& scene, QImage const& own, int ownSheet, QTransform const& toPixels)
{
    QImage all(own.size(), QImage::Format_ARGB32_Premultiplied);
    all.fill(Qt::transparent);
    QPainter p(&all);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        if (sheet == ownSheet) {
            p.resetTransform();
            p.drawImage(0, 0, own);
            continue;
        }
        p.setTransform(toPixels);
        for (Picture const& picture : drawing.pictures)
            if (picture.sheet == sheet && !picture.image.isNull()) {
                p.save();
                p.setTransform(picture.placement * toPixels);
                p.drawImage(0, 0, picture.image);
                p.restore();
            }
        QVector<Fill> areas = drawing.fillsOf(sheet);
        std::stable_sort(areas.begin(), areas.end(), [](Fill const& a, Fill const& b) { return a.order < b.order; });
        for (Fill const& area : std::as_const(areas))
            Canvas::paintFill(p, area, scene.brushFor(area));
        QVector<Stroke> lines = drawing.strokesOf(sheet);
        std::stable_sort(lines.begin(), lines.end(), [](Stroke const& a, Stroke const& b) { return a.order < b.order; });
        for (Stroke const& line : std::as_const(lines))
            Canvas::paintStroke(p, line, scene.brushFor(line));
    }
    p.end();
    return all;
}

}  // namespace

bool Canvas::fillBitmapAt(QPointF p, bool clear)
{
    Scene const& scene = m_document->scene();
    QColor const colour = clear ? QColor(Qt::transparent) : scene.resolve(m_swatch, Qt::black);
    Drawing const drawing = realDrawing() ? *realDrawing() : Drawing();
    int const ownSheet = sheetOf(m_art);
    int const tolerance = m_bitmapTolerance, overlap = m_bitmapOverlap;
    bool const everyLayer = m_bitmapSource == 1;
    bool changed = false;
    paintIntoBitmap([&](QPainter& painter) {
        QImage* const target = dynamic_cast<QImage*>(painter.device());
        if (!target)
            return;
        QPoint const seed = painter.transform().map(p).toPoint();
        if (!target->rect().contains(seed)) {
            m_bitmapUnchanged = true;
            return;
        }
        QImage const before = target->copy();
        QImage const sample = everyLayer ? everyArtLayer(drawing, scene, before, ownSheet, painter.transform()) : before;
        int const w = before.width(), h = before.height();
        QRgb const own = before.pixel(seed);
        QRgb const from = sample.pixel(seed);
        QRgb const to = colour.rgba();
        if ((!clear && qUnpremultiply(own) == to && overlap == 0) || (clear && qAlpha(own) == 0)) {
            m_bitmapUnchanged = true;       // nothing to do: no step to undo
            return;
        }
        // Near enough: no channel (alpha among them) further than the tolerance from the pixel clicked.
        auto near = [from, tolerance](QRgb c) {
            return std::abs(qRed(c) - qRed(from)) <= tolerance && std::abs(qGreen(c) - qGreen(from)) <= tolerance
                   && std::abs(qBlue(c) - qBlue(from)) <= tolerance && std::abs(qAlpha(c) - qAlpha(from)) <= tolerance;
        };
        QVector<bool> seen(qsizetype(w) * h, false);
        QVector<QPoint> region, todo = {seed};
        seen[qsizetype(seed.y()) * w + seed.x()] = true;
        auto each = [&](QPoint at, auto&& take) {
            for (QPoint const step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
                QPoint const next = at + step;
                if (next.x() < 0 || next.y() < 0 || next.x() >= w || next.y() >= h)
                    continue;
                qsizetype const index = qsizetype(next.y()) * w + next.x();
                if (!seen[index] && take(next)) {
                    seen[index] = true;
                    todo.push_back(next);
                }
            }
        };
        while (!todo.isEmpty()) {
            QPoint const at = todo.takeLast();
            region.push_back(at);
            each(at, [&](QPoint next) { return near(sample.pixel(next)); });
        }
        // The overlap: the fill reaches so many pixels further, under the edge of what bounds it.
        QVector<QPoint> rim = region;
        for (int ring = 0; ring < overlap; ++ring) {
            todo.clear();
            for (QPoint const at : std::as_const(rim))
                each(at, [](QPoint) { return true; });
            rim = todo;
            region += todo;
        }
        todo.clear();
        QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        QRgb const paint = clear ? qRgba(0, 0, 0, 255) : qPremultiply(to);     // (to clear: the mask marks where)
        for (QPoint const at : std::as_const(region))
            mask.setPixel(at, paint);
        changed = !region.isEmpty();
        // The region takes the colour (cleared: is cut out) - pixel for pixel, no smoothing at its edge.
        painter.resetTransform();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setCompositionMode(clear ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_Source);
        if (clear) {
            painter.drawImage(0, 0, mask);
            return;
        }
        // Only where the mask is: the rest of the picture stays as it was.
        QImage merged = before;
        QPainter m(&merged);
        m.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        QImage hole = mask;
        {
            QPainter hp(&hole);
            hp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            hp.fillRect(hole.rect(), Qt::black);
        }
        m.drawImage(0, 0, hole);
        m.setCompositionMode(QPainter::CompositionMode_SourceOver);
        m.drawImage(0, 0, mask);
        m.end();
        painter.drawImage(0, 0, merged);
    }, clear ? tr("Unpaint") : tr("Paint"));
    return changed;
}
