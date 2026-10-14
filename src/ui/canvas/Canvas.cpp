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
    // An edit that did not come from the selection itself (undo, another tool) may have
    // changed what the indices refer to.
    if (!m_committing) {
        setSelection({}, {});
        m_ceStroke = m_cePoint = m_ceFill = -1;
        if (m_tool == Reposition)
            selectAll();        // that tool has all of the drawing in hand, always
    }
    update();
}

void Canvas::setTool(Tool tool)
{
    bool const changed = tool != m_tool;
    if (changed) {
        finishPolyline(true);
        endText(true);
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
        || m_artView.strokes.size() != sheetStrokes.size() || m_artView.fills.size() != sheetFills.size()) {
        m_artView.strokes = sheetStrokes;
        m_artView.fills = sheetFills;
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
    return m_art == LineArt && m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].bitmap && scene.layers[m_layer].holdsDrawings();
}

void Canvas::paintIntoBitmap(std::function<void(QPainter&)> const& paint, QString const& text)
{
    Scene const& scene = m_document->scene();
    Drawing art = realDrawing() ? *realDrawing() : Drawing();
    int at = -1;
    for (int i = 0; i < art.pictures.size() && at < 0; ++i)
        if (art.pictures[i].painted)
            at = i;
    if (at < 0) {
        // The pixels of the drawing: the frame of the scene and a quarter of it more on every side.
        QRectF const area(-scene.width * 0.25, -scene.height * 0.25, scene.width * 1.5, scene.height * 1.5);
        Picture pixels;
        pixels.painted = true;
        pixels.size = QSize(int(std::ceil(area.width())), int(std::ceil(area.height())));
        pixels.image = QImage(pixels.size, QImage::Format_ARGB32_Premultiplied);
        pixels.image.fill(Qt::transparent);
        pixels.placement = QTransform::fromTranslate(area.left(), area.top());
        at = int(art.pictures.size());
        art.pictures.push_back(pixels);
    }
    Picture& pixels = art.pictures[at];
    if (pixels.image.isNull() || !pixels.placement.isInvertible())
        return;
    QImage image = pixels.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setTransform(pixels.placement.inverted());
        paint(painter);
    }
    pixels.image = image;
    m_document->setDrawing(m_layer, m_frame, art, text);
}

bool Canvas::erasePictures(Drawing& art, QVector<QPolygonF> const& rubbed)
{
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    for (QPolygonF const& polygon : rubbed)
        area.addPolygon(polygon);
    QRectF const reach = area.boundingRect();
    bool any = false;
    for (Picture& picture : art.pictures) {
        if (picture.linked || picture.image.isNull() || !picture.placement.isInvertible() || !picture.bounds().intersects(reach))
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
        paintIntoBitmap([&](QPainter& painter) { paintStroke(painter, stroke, scene.brushFor(stroke)); }, tr("Paint Stroke"));
        return;
    }
    if (m_art == LineArt) {
        m_document->addStroke(m_layer, m_frame, stroke);
        return;
    }
    Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
    art.strokes.push_back(stroke);
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
    QVector<QPolygonF> const rubbed = stretch.outlinePolygons();
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
    bool const pixels = m_art == LineArt && erasePictures(m_eraseArt, rubbed);
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
    QVector<QPolygonF> const rubbed = m_stroke.outlinePolygons();
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
        if (m_art == LineArt && erasePictures(art, rubbed))
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
    m_document->setDrawing(m_layer, m_frame, withArt(drawing), text);
    m_committing = false;
}
