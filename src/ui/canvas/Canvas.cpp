// Eclipse - the Camera view: how it is set up, which tool and cell are in hand, and the art being worked on.
#include "ui/canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

Canvas::Canvas(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setAcceptDrops(true);       // templates, from the Library
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    setMouseTracking(true);     // the status bar says where the pointer is
    connect(m_document, &Document::changed, this, &Canvas::onDocumentChanged);
    updateCursor();
}

void Canvas::onDocumentChanged()
{
    m_modeCache = QImage();
    // An edit that did not come from the selection itself (undo, another tool) may have changed what the indices refer
    // to - unless the drawing still has as many lines, areas and pictures as it had (an undone point move, a reshape, a
    // recolour): then they are the same things, and stay picked.
    QVector<int> const shape = drawingShape();
    bool const same = !shape.isEmpty() && shape == m_seenShape;
    m_seenShape = shape;
    if (!m_committing && same) {
        // (A point picked on a line that now has fewer points goes to its last.)
        Drawing const* const d = currentDrawing();
        Stroke const* const line = d ? contourLine(*d) : nullptr;
        if (line && m_cePoint >= line->points.size())
            m_cePoint = int(line->points.size()) - 1;
        if (line)
            m_ceExtra.removeIf([&](int i) { return i < 0 || i >= line->points.size() || i == m_cePoint; });
        emit pointPicked(contourValid() ? pointThickness() : -1);
    } else if (!m_committing) {
        setSelection({}, {});
        m_ceStroke = m_cePoint = m_ceFill = -1;
        m_ceExtra.clear();
        if (m_tool == Reposition)
            selectAll();        // that tool has all of the drawing in hand, always
    }
    update();
}

QVector<int> Canvas::drawingShape() const
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return {};
    QVector<int> shape{m_layer, m_frame, int(d->pictures.size())};
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        shape << int(d->strokesOf(sheet).size()) << int(d->fillsOf(sheet).size());
    return shape;
}

void Canvas::setTool(Tool tool)
{
    bool const changed = tool != m_tool;
    if (changed) {
        finishPolyline(true);
        endText(true);
        if (m_bending) {        // a curve of the Line tool not put down yet: it goes
            m_bending = false;
            m_drag = DragNone;
            m_stroke = Stroke();
        }
    }
    bool const bothWas = bothArtsNow();
    if (bothWas && tool != Select && tool != Cutter)
        clearSelection();       // (the next tool sees one sheet only: what is selected would not be the same things)
    m_tool = tool;
    if (!bothWas && bothArtsNow())
        clearSelection();
    if (tool != Select && tool != Cutter && tool != Reposition && tool != Perspective && tool != Envelope)
        clearSelection();
    if (tool == Reposition)
        selectAll();
    m_ceStroke = m_cePoint = m_ceFill = -1;
    emit pointPicked(-1);
    updateCursor();
    update();
    if (changed)
        emit toolChanged(int(tool));
}

void Canvas::zoomBy(double factor)
{
    // The scene point in the middle of the view stays there.
    zoomAt(QPointF(width() / 2.0, height() / 2.0), factor);
}

void Canvas::zoomAt(QPointF anchor, double factor)
{
    QPointF const scenePoint = toScene(anchor);
    m_zoom = std::clamp(m_zoom * factor, 0.02, 64.0);
    emit zoomChanged();
    m_fitted = false;
    m_pan += anchor - viewTransform().map(scenePoint);
    update();
}

void Canvas::setOnionSkin(bool on)
{
    m_onionSkin = on;
    update();
    emit onionChanged();
}

void Canvas::setPlaying(bool playing)
{
    m_playing = playing;
    if (playing)
        clearSelection();
    update();
}

void Canvas::setCurrentCell(int layer, int frame)
{
    // (However it goes, the Morphing tool's panel is told when it is done.)
    struct Told {
        Canvas* canvas;
        ~Told() { emit canvas->morphHereChanged(); }
    } const told{this};
    // Permanent Selection: what lies in the place of the selection on the frame gone to is selected there.
    QRectF const held = m_permanentSelection && m_tool == Select && hasSelection() && layer == m_layer && frame != m_frame
                        ? selectionBounds().adjusted(-1.5, -1.5, 1.5, 1.5) : QRectF();
    // The same drawing still, held over the frame gone to: what was selected in it stays selected.
    bool sameDrawing = false;
    if (layer == m_layer && frame != m_frame && layer >= 0 && layer < m_document->scene().layers.size()) {
        Layer const& on = m_document->scene().layers[layer];
        sameDrawing = on.drawingAt(frame) != Layer::kNoDrawing && on.drawingAt(frame) == on.drawingAt(m_frame);
    }
    if (sameDrawing) {
        m_frame = frame;
        update();
        return;
    }
    if (layer != m_layer || frame != m_frame) {
        finishPolyline(true);  // it belongs to the drawing it was begun on
        endText(true);
        setSelection({}, {});
        m_ceStroke = m_cePoint = m_ceFill = -1;
        emit pointPicked(-1);
    }
    m_layer = layer;
    m_frame = frame;
    if (m_tool == Reposition)
        selectAll();
    if (!held.isNull()) {
        if (Drawing const* const d = currentDrawing()) {
            QVector<int> strokes, fills;
            for (int i = 0; i < d->strokes.size(); ++i)
                if (held.contains(d->strokes[i].bounds()))
                    strokes.push_back(i);
            for (int i = 0; i < d->fills.size(); ++i)
                if (held.contains(d->fills[i].bounds()))
                    fills.push_back(i);
            setSelection(strokes, fills);
        }
    }
    update();
}

// The cross the drawing tools point with.  The system's own cross is drawn by inverting
// what is under it, and on the mid-grey of the view that gives the same grey back: this
// one is black with a white edge, so it shows on anything.
static QCursor drawingCursor()
{
    static QCursor const cursor = [] {
        int const size = 25, mid = size / 2;
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        // Four arms that stop short of the middle, so the point itself stays clear.
        auto arms = [&](QColor const& colour, int thickness) {
            int const half = thickness / 2;
            int const reach = thickness > 1 ? 1 : 0;  // the edge runs a pixel past each end
            for (int sign : {-1, 1}) {
                int const from = mid + sign * (3 - reach), to = mid + sign * (11 + reach);
                p.fillRect(QRect(QPoint(std::min(from, to), mid - half), QPoint(std::max(from, to), mid + half)), colour);
                p.fillRect(QRect(QPoint(mid - half, std::min(from, to)), QPoint(mid + half, std::max(from, to))), colour);
            }
        };
        arms(Qt::white, 3);
        arms(Qt::black, 1);
        p.fillRect(mid, mid, 1, 1, Qt::black);
        p.end();
        return QCursor(pixmap, mid, mid);
    }();
    return cursor;
}

void Canvas::updateCursor()
{
    if (m_spaceDown || m_tool == Hand)
        setCursor(m_drag == DragPan ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    else if (m_tool == Select || m_tool == Transform || m_tool == ContourEditor || m_tool == Rigging || m_tool == IK)
        setCursor(Qt::ArrowCursor);
    else if (isPaintTool(m_tool) || m_tool == Dropper)
        setCursor(Qt::PointingHandCursor);
    else if (m_tool == Zoom || m_tool == RotateView)
        setCursor(Qt::SizeAllCursor);
    else if (m_tool == Text)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(drawingCursor());
}

Drawing const* Canvas::realDrawing() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return nullptr;
    int const index = scene.layers[m_layer].drawingAt(m_frame);
    return index == Layer::kNoDrawing ? nullptr : &scene.layers[m_layer].drawings[index];
}

// The sheet of a drawing an art layer of the view stands for.
int Canvas::sheetOf(Art art)
{
    return art == Canvas::ColourArt ? Drawing::ColourSheet : art == Canvas::OverlayArt ? Drawing::OverlaySheet
         : art == Canvas::UnderlayArt ? Drawing::UnderlaySheet : Drawing::LineSheet;
}

Drawing const* Canvas::currentDrawing() const
{
    Drawing const* const real = realDrawing();
    if (real && bothArtsNow()) {
        // The colour art and, over it, the line art, as one drawing; each thing remembers its sheet.
        void const* const key[5] = {real->strokes.constData(), real->fills.constData(), real->colourStrokes.constData(), real->colourFills.constData(), real->pictures.constData()};
        qsizetype const size[5] = {real->strokes.size(), real->fills.size(), real->colourStrokes.size(), real->colourFills.size(), real->pictures.size()};
        bool same = true;
        for (int i = 0; i < 5; ++i)
            same = same && key[i] == m_bothKey[i] && size[i] == m_bothSize[i];
        if (!same) {
            for (int i = 0; i < 5; ++i) {
                m_bothKey[i] = key[i];
                m_bothSize[i] = size[i];
            }
            m_bothView = Drawing();
            m_bothView.pictures = real->pictures;
            for (int sheet : {int(Drawing::ColourSheet), int(Drawing::LineSheet)}) {
                for (Fill fill : real->fillsOf(sheet)) {
                    fill.sheet = sheet;
                    m_bothView.fills.push_back(fill);
                }
                for (Stroke stroke : real->strokesOf(sheet)) {
                    stroke.sheet = sheet;
                    m_bothView.strokes.push_back(stroke);
                }
            }
        }
        return &m_bothView;
    }
    if (!real || m_art == LineArt)
        return real;
    // The colour art, as a drawing.  It is taken afresh only when the scene has another
    // one: what was handed out before stays where it is as long as nothing changed.
    QVector<Stroke> const& sheetStrokes = real->strokesOf(sheetOf(m_art));
    QVector<Fill> const& sheetFills = real->fillsOf(sheetOf(m_art));
    if (m_artView.strokes.constData() != sheetStrokes.constData() || m_artView.fills.constData() != sheetFills.constData()
        || m_artView.strokes.size() != sheetStrokes.size() || m_artView.fills.size() != sheetFills.size()
        || m_artPictures != real->pictures.constData() || m_artPictureCount != real->pictures.size()) {
        m_artView.strokes = sheetStrokes;
        m_artView.fills = sheetFills;
        // With pixels of its own: those of this art layer.
        m_artView.pictures.clear();
        for (Picture const& picture : real->pictures)
            if (picture.painted && picture.sheet == sheetOf(m_art))
                m_artView.pictures.push_back(picture);
        m_artPictures = real->pictures.constData();
        m_artPictureCount = real->pictures.size();
    }
    return &m_artView;
}

Drawing Canvas::withArt(Drawing const& art) const
{
    if (bothArtsNow()) {
        // Each thing goes back to the sheet it came from; what is new goes to the sheet chosen.
        Drawing whole = realDrawing() ? *realDrawing() : Drawing();
        for (int sheet : {int(Drawing::ColourSheet), int(Drawing::LineSheet)}) {
            whole.fillsOf(sheet).clear();
            whole.strokesOf(sheet).clear();
        }
        int const mine = sheetOf(m_art);
        for (Fill fill : art.fills) {
            int const sheet = fill.sheet == Drawing::ColourSheet || fill.sheet == Drawing::LineSheet ? fill.sheet : mine;
            fill.sheet = -1;
            whole.fillsOf(sheet).push_back(fill);
        }
        for (Stroke stroke : art.strokes) {
            int const sheet = stroke.sheet == Drawing::ColourSheet || stroke.sheet == Drawing::LineSheet ? stroke.sheet : mine;
            stroke.sheet = -1;
            whole.strokesOf(sheet).push_back(stroke);
        }
        if (!art.pictures.isEmpty() || m_keepNoPictures || !realDrawing())
            whole.pictures = art.pictures;
        return whole;
    }
    if (m_art == LineArt) {
        // The lines are the drawing itself; its other sheets come along untouched.
        Drawing whole = art;
        if (Drawing const* const real = realDrawing()) {
            for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::OverlaySheet)}) {
                whole.fillsOf(sheet) = real->fillsOf(sheet);
                whole.strokesOf(sheet) = real->strokesOf(sheet);
            }
            whole.pinned = real->pinned;
            whole.name = real->name;
            whole.mark = real->mark;
            if (whole.pictures.isEmpty() && !m_keepNoPictures)
                whole.pictures = real->pictures;        // (the tools do not touch pictures: one made afresh has none)
        }
        return whole;
    }
    Drawing whole = realDrawing() ? *realDrawing() : Drawing();
    whole.fillsOf(sheetOf(m_art)) = art.fills;
    whole.strokesOf(sheetOf(m_art)) = art.strokes;
    // The pixels of this art layer, as they now are.
    int const mine = sheetOf(m_art);
    whole.pictures.removeIf([mine](Picture const& p) { return p.painted && p.sheet == mine; });
    for (Picture picture : art.pictures) {
        picture.painted = true;
        picture.sheet = mine;
        whole.pictures.push_back(picture);
    }
    return whole;
}

void Canvas::setSelectBothArts(bool on)
{
    if (on == m_selectBothArts)
        return;
    // (What is selected is counted differently with both sheets as one.)
    clearSelection();
    m_ceStroke = m_cePoint = m_ceFill = -1;
    m_selectBothArts = on;
    update();
    emit selectBothArtsChanged(on);
}

void Canvas::setArt(Art art)
{
    if (art == m_art)
        return;
    finishPolyline(true);
    endText(true);
    clearSelection();
    m_ceStroke = m_cePoint = m_ceFill = -1;
    emit pointPicked(-1);
    m_art = art;
    update();
    emit artChanged(int(art));
}

void Canvas::onActionPreviewAllArt(bool on)
{
    m_previewAllArt = on;
    update();
}

bool Canvas::bitmapNow() const
{
    Scene const& scene = m_document->scene();
    return m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].bitmapSheet(sheetOf(m_art)) && scene.layers[m_layer].holdsDrawings()
           && !bothArtsNow();
}

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
        paint(painter);
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

bool Canvas::fillBitmapAt(QPointF p, bool clear)
{
    QColor const colour = clear ? QColor(Qt::transparent) : m_document->scene().resolve(m_swatch, Qt::black);
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
        int const w = before.width(), h = before.height();
        QRgb const from = before.pixel(seed);
        QRgb const to = colour.rgba();
        if ((!clear && qUnpremultiply(from) == to) || (clear && qAlpha(from) == 0)) {
            m_bitmapUnchanged = true;       // nothing to do: no step to undo
            return;
        }
        // Near enough: no channel (alpha among them) more than this far from the pixel clicked.
        auto near = [from](QRgb c) {
            return std::abs(qRed(c) - qRed(from)) <= 40 && std::abs(qGreen(c) - qGreen(from)) <= 40 && std::abs(qBlue(c) - qBlue(from)) <= 40
                   && std::abs(qAlpha(c) - qAlpha(from)) <= 40;
        };
        QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        QVector<bool> seen(qsizetype(w) * h, false);
        QVector<QPoint> todo = {seed};
        seen[qsizetype(seed.y()) * w + seed.x()] = true;
        QRgb const paint = clear ? qRgba(0, 0, 0, 255) : qPremultiply(to);     // (to clear: the mask marks where)
        while (!todo.isEmpty()) {
            QPoint const at = todo.takeLast();
            mask.setPixel(at, paint);
            changed = true;
            for (QPoint const step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
                QPoint const next = at + step;
                if (next.x() < 0 || next.y() < 0 || next.x() >= w || next.y() >= h)
                    continue;
                qsizetype const index = qsizetype(next.y()) * w + next.x();
                if (seen[index] || !near(before.pixel(next)))
                    continue;
                seen[index] = true;
                todo.push_back(next);
            }
        }
        // The region takes the colour (cleared: is cut out) - pixel for pixel, no smoothing at its edge.
        painter.resetTransform();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setCompositionMode(clear ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_Source);
        if (clear) {
            QImage cut = mask;
            QPainter c(&cut);
            c.setCompositionMode(QPainter::CompositionMode_SourceIn);
            c.fillRect(cut.rect(), Qt::black);
            c.end();
            painter.drawImage(0, 0, cut);
        } else {
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
        }
    }, clear ? tr("Unpaint") : tr("Paint"));
    return changed;
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
        paintIntoBitmap([&](QPainter& painter) { paintStroke(painter, stroke, scene.brushFor(stroke)); }, tr("Paint Stroke"), stroke.bounds());
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
    // Only what the stretch reaches is gone through again.  The edges are left as the cut
    // made them (no fitting): the contours get their points when the eraser lifts.
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
    // Each drawing to erase in is come to by a frame that shows it: this one, or with
    // Apply to All Drawings every drawing of the layer.
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

QRectF Canvas::bitmapArea() const
{
    Scene const& scene = m_document->scene();
    QRectF frame(0.0, 0.0, scene.width, scene.height);
    if (m_bitmapBasis != CanvasScene && !m_drawingView && m_layer >= 0 && m_layer < scene.layers.size()) {
        // The camera's frame brought onto the layer: on this frame, or on every frame of the scene.
        auto covered = [&](int frameAt) {
            QTransform const toPicture = worldOf(m_layer, frameAt) * scene.cameraTransform(frameAt);
            return toPicture.isInvertible() ? toPicture.inverted().mapRect(QRectF(0.0, 0.0, scene.width, scene.height)) : QRectF();
        };
        QRectF area = covered(m_frame);
        if (m_bitmapBasis == CanvasCameraMove)
            for (int f = 0; f < scene.frameCount(); ++f)
                area = area.united(covered(f));
        if (area.isValid() && area.width() < scene.width * 64.0 && area.height() < scene.height * 64.0)
            frame = area;
    }
    double const mx = (m_bitmapCanvas / 100.0 - 1.0) / 2.0, my = (m_bitmapCanvasHeight / 100.0 - 1.0) / 2.0;
    return frame.adjusted(-frame.width() * mx, -frame.height() * my, frame.width() * mx, frame.height() * my);
}

double Canvas::bitmapFineness() const
{
    Scene const& scene = m_document->scene();
    double const layer = m_layer >= 0 && m_layer < scene.layers.size() ? std::clamp(scene.layers[m_layer].bitmapResolution, 25, 400) / 100.0 : 1.0;
    return std::clamp(layer * m_bitmapPpi / 72.0, 0.25, 8.0);
}
