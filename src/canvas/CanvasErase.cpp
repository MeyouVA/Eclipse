// Eclipse - the Camera view: painting into bitmaps, erasing art and pictures, lines added to art, the eraser's cut.
#include "canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

void Canvas::paintIntoBitmap(std::function<void(QPainter&)> const& paint, QString const& text, QRectF reach)
{
    Drawing art = realDrawing() ? *realDrawing() : Drawing();
    // The pixels of the art layer being worked on.
    int const sheet = sheetOf(m_art);
    int at = -1;
    for (int i = 0; i < art.pictures.size() && at < 0; ++i)
        if (art.pictures[i].painted && art.pictures[i].sheet == sheet)
            at = i;
    if (at < 0) {
        // The pixels of the drawing: as Preferences say (the frame of the scene and a quarter of it more on every side).
        QRectF const area = bitmapArea();
        // As fine as the layer asks: so many pixels across each unit of the scene.
        double const fine = bitmapFineness();
        Picture pixels;
        pixels.painted = true;
        pixels.sheet = sheet;
        pixels.size = QSize(int(std::ceil(area.width() * fine)), int(std::ceil(area.height() * fine)));
        pixels.image = QImage(pixels.size, QImage::Format_ARGB32_Premultiplied);
        pixels.image.fill(Qt::transparent);
        pixels.placement = QTransform::fromScale(1.0 / fine, 1.0 / fine) * QTransform::fromTranslate(area.left(), area.top());
        at = int(art.pictures.size());
        art.pictures.push_back(pixels);
    }
    Picture& pixels = art.pictures[at];
    if (pixels.image.isNull() || !pixels.placement.isInvertible())
        return;
    QImage image = pixels.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (reach.isValid() && !pixels.bounds().contains(reach) && pixels.placement.type() <= QTransform::TxScale) {
        // Paint reaching beyond the pixels: they grow to take it in (and a little more), up to 8192 pixels each way.
        QRect const old = image.rect();
        QRect const wanted = pixels.placement.inverted().mapRect(reach).toAlignedRect().adjusted(-64, -64, 64, 64);
        QRect grown = old.united(wanted);
        grown.setLeft(std::max(grown.left(), old.right() + 1 - 8192));
        grown.setTop(std::max(grown.top(), old.bottom() + 1 - 8192));
        grown.setRight(std::min(grown.right(), grown.left() + 8191));
        grown.setBottom(std::min(grown.bottom(), grown.top() + 8191));
        if (grown != old) {
            QImage bigger(grown.size(), QImage::Format_ARGB32_Premultiplied);
            bigger.fill(Qt::transparent);
            {
                QPainter onto(&bigger);
                onto.setCompositionMode(QPainter::CompositionMode_Source);
                onto.drawImage(-grown.topLeft(), image);
            }
            image = bigger;
            pixels.placement = QTransform::fromTranslate(grown.left(), grown.top()) * pixels.placement;
            pixels.size = grown.size();
        }
    }
    // Pieces of the pixels cut loose (and perhaps moved) go back into them first, where they now lie.
    QVector<int> pieces;
    for (int i = at + 1; i < art.pictures.size(); ++i)
        if (art.pictures[i].painted && art.pictures[i].sheet == sheet && !art.pictures[i].image.isNull())
            pieces.push_back(i);
    if (!pieces.isEmpty()) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int i : std::as_const(pieces)) {
            painter.setTransform(art.pictures[i].placement * pixels.placement.inverted());
            painter.drawImage(QPointF(0, 0), art.pictures[i].image);
        }
    }
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setTransform(pixels.placement.inverted());
        if (!m_paintClip.isEmpty())
            painter.setClipPath(m_paintClip);       // Overlay Brush Mode
        paint(painter);
        m_paintClip = QPainterPath();
    }
    if (m_bitmapUnchanged && pieces.isEmpty()) {
        m_bitmapUnchanged = false;
        return;
    }
    m_bitmapUnchanged = false;
    for (int k = int(pieces.size()) - 1; k >= 0; --k)
        art.pictures.removeAt(pieces[k]);
    if (!pieces.isEmpty())
        clearSelection();
    Picture& merged = art.pictures[at];
    merged.image = image;
    m_document->setDrawing(m_layer, m_frame, art, text);
}

int Canvas::liftedPieceAt(QPointF p) const
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return -1;
    int first = -1;
    for (int i = 0; i < d->pictures.size(); ++i) {
        Picture const& piece = d->pictures[i];
        if (!piece.painted || !pictureHere(piece))
            continue;
        if (first < 0) {
            first = i;      // the pixels themselves, not a piece of them
            continue;
        }
        if (!piece.image.isNull() && piece.placement.isInvertible()) {
            QPoint const at = piece.placement.inverted().map(p).toPoint();
            if (piece.image.rect().contains(at) && qAlpha(piece.image.pixel(at)) > 0)
                return i;
        }
    }
    return -1;
}

bool Canvas::erasePictures(Drawing& art, QVector<QPolygonF> const& rubbed) const
{
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    for (QPolygonF const& polygon : rubbed)
        area.addPolygon(polygon);
    QRectF const reach = area.boundingRect();
    bool any = false;
    for (Picture& picture : art.pictures) {
        if (picture.linked || picture.image.isNull() || !picture.placement.isInvertible() || !picture.bounds().intersects(reach) || !pictureHere(picture))
            continue;
        QImage image = picture.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setTransform(picture.placement.inverted());
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.fillPath(area, Qt::black);
        }
        picture.image = image;
        any = true;
    }
    return any;
}

void Canvas::addStrokeToArt(Stroke const& stroke)
{
    if (bitmapNow()) {
        Scene const& scene = m_document->scene();
        bool const hard = m_hardEdges && (m_tool == Line || m_tool == Rectangle || m_tool == Ellipse || m_tool == Polyline);
        paintIntoBitmap([&](QPainter& painter) {
            if (hard)
                painter.setRenderHint(QPainter::Antialiasing, false);       // Edge Type: hard
            paintStroke(painter, stroke, scene.brushFor(stroke));
        }, tr("Paint Stroke"), stroke.bounds());
        return;
    }
    bool const behind = m_tool == Polyline && m_polyBehind;
    if (m_art == LineArt && !behind) {
        m_document->addStroke(m_layer, m_frame, stroke);
        return;
    }
    Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
    Stroke placed = stroke;
    if (behind) {
        // Draw Behind Current Layers: under everything of the art layer (below the lowest painted area, areas lying under lines).
        int const bottom = Drawing::bottomOrder(art.fills, art.strokes);
        placed.order = std::any_of(art.fills.begin(), art.fills.end(), [bottom](Fill const& f) { return f.order == bottom; }) ? bottom - 1 : bottom;
        art.strokes.push_front(placed);
        commitDrawing(art, tr("Draw Stroke"));
        return;
    }
    placed.order = Drawing::topOrder(art.fills, art.strokes);
    art.strokes.push_back(placed);
    commitDrawing(art, tr("Draw Stroke"));
}

void Canvas::eraseArtAt(QPointF p, double radius)
{
    if (m_art == LineArt) {
        m_document->eraseAt(m_layer, m_frame, p, radius);
        return;
    }
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    Drawing art = *d;
    art.strokes.clear();
    for (Stroke const& s : d->strokes)
        if (!s.hit(p, radius))
            art.strokes.push_back(s);
    // Brush shapes go as lines do; paint from the bucket stays.
    art.fills.clear();
    for (Fill const& f : d->fills)
        if (f.contours.isEmpty() || !f.hit(p, radius))
            art.fills.push_back(f);
    if (art.strokes.size() != d->strokes.size() || art.fills.size() != d->fills.size())
        commitDrawing(art, tr("Erase"));
}

void Canvas::eraseStretch(int from)
{
    // The stretch the eraser has just covered, cut out of what is shown while it is dragged.
    Stroke stretch;
    stretch.width = m_stroke.width;
    stretch.points = m_stroke.points.mid(from);
    QVector<QPolygonF> const rubbed = eraserArea(stretch);
    if (rubbed.isEmpty())
        return;
    QPainterPath area;
    for (QPolygonF const& polygon : rubbed)
        area.addPolygon(polygon);
    QRectF const reach = area.boundingRect();
    double const fine = std::max(0.25 / m_zoom, 0.05);
    bool any = false;
    QVector<Fill> fills;
    for (Fill const& fill : std::as_const(m_eraseArt.fills)) {
        bool changed = false;
        if (fill.bounds().intersects(reach))
            fills += fill.minus(rubbed, -1.0, &changed);
        else
            fills.push_back(fill);
        any = any || changed;
    }
    QVector<Stroke> strokes;
    for (Stroke const& stroke : std::as_const(m_eraseArt.strokes)) {
        bool changed = false;
        if (stroke.bounds().intersects(reach))
            strokes += stroke.minus(rubbed, fine, &changed);
        else
            strokes.push_back(stroke);
        any = any || changed;
    }
    // Pictures kept in the scene - the pixels of a bitmap layer among them - are rubbed out where it passes.
    bool const pixels = (m_art == LineArt || bitmapNow()) && erasePictures(m_eraseArt, rubbed);
    if (!any && !pixels)
        return;
    m_eraseArt.fills = fills;
    m_eraseArt.strokes = strokes;
    if (!m_eraseAny) {
        // What was picked may be gone or in pieces.
        m_selStrokes.clear();
        m_selFills.clear();
    m_selPictures.clear();
        m_ceStroke = m_cePoint = m_ceFill = -1;
        emit selectionChanged();
    }
    m_eraseAny = true;
}

void Canvas::cutWithEraser()
{
    Scene const& scene = m_document->scene();
    bool const everywhere = m_paintAll && m_layer >= 0 && m_layer < scene.layers.size();
    // In the line art and the colour art at once, when that is asked and one of the two is being worked on.
    QVector<Art> arts = {m_art};
    if (m_bothArts && (m_art == LineArt || m_art == ColourArt))
        arts = {LineArt, ColourArt};
    if (m_stroke.points.isEmpty() || (!m_eraseAny && !everywhere && arts.size() == 1))
        return;
    m_eraseAny = false;
    QVector<QPolygonF> const rubbed = eraserArea(m_stroke);
    if (rubbed.isEmpty())
        return;
    double const fine = std::max(0.25 / m_zoom, 0.05);
    auto cut = [&](Drawing const& from, Drawing& art) {
        art = from;
        bool any = false;
        art.fills.clear();
        for (Fill const& fill : from.fills) {
            bool changed = false;
            art.fills += fill.minus(rubbed, fine, &changed);
            any = any || changed;
        }
        art.strokes.clear();
        for (Stroke const& stroke : from.strokes) {
            bool changed = false;
            art.strokes += stroke.minus(rubbed, fine, &changed);
            any = any || changed;
        }
        if ((m_art == LineArt || bitmapNow()) && erasePictures(art, rubbed))
            any = true;
        return any;
    };
    QVector<int> frames = {m_frame};
    if (everywhere) {
        QVector<int> const cells = scene.layers[m_layer].cells;
        QVector<int> seen;
        if (m_frame >= 0 && m_frame < cells.size())
            seen.push_back(cells[m_frame]);
        for (int f = 0; f < cells.size(); ++f) {
            if (cells[f] == Layer::kNoDrawing || seen.contains(cells[f]))
                continue;
            seen.push_back(cells[f]);
            frames.push_back(f);
        }
    }
    int const here = m_frame;
    Art const artWas = m_art;
    bool const several = frames.size() * arts.size() > 1;
    bool begun = false;
    for (int f : std::as_const(frames)) {
        for (Art const art : std::as_const(arts)) {
            m_frame = f;
            m_art = art;
            Drawing const* const under = currentDrawing();
            if (!under)
                continue;
            Drawing const from = *under;
            Drawing left;
            if (!cut(from, left))
                continue;
            if (!begun && several)
                m_document->beginMacro(tr("Erase"));
            begun = true;
            commitDrawing(left, tr("Erase"));
        }
    }
    m_frame = here;
    m_art = artWas;
    if (!begun)
        return;
    if (several)
        m_document->endMacro();
    // What was picked may be gone or in pieces.
    m_selStrokes.clear();
    m_selFills.clear();
    m_selPictures.clear();
    m_ceStroke = m_cePoint = m_ceFill = -1;
    emit selectionChanged();
}

void Canvas::commitDrawing(Drawing const& drawing, QString const& text)
{
    m_committing = true;
    m_document->setDrawing(m_layer, m_frame, followLines(withArt(drawing), realDrawing()), text);
    m_committing = false;
}
