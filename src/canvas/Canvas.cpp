// Eclipse - the Camera view: made, tools, zoom, the current cell, cursors, art layers, bitmap area, overlay mask.
#include "canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

Canvas::Canvas(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setAcceptDrops(true);       // templates, from the Library
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    setMouseTracking(true);     // the status bar says where the pointer is
    connect(m_document, &Document::changed, this, &Canvas::onDocumentChanged);
    connect(m_document, &Document::resetDone, this, &Canvas::cancelInput);
    updateCursor();
}

void Canvas::onDocumentChanged()
{
    m_pointViewLayer = -1;
    Scene const& scene = m_document->scene();
    if (m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].locked
        && (m_drag != DragNone || m_polyActive || m_bending || m_tabletActive)
        && m_drag != DragPan && m_drag != DragRotateView && m_drag != DragZoom)
        cancelInput();
    m_modeCache = QImage();
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
    m_dropperTexture = false;
    if (changed) {
        // The tool to come back to once the Dropper has taken a colour (unless it is sticky).
        if (tool == Dropper)
            m_toolBeforeDropper = int(m_tool);
        finishPolyline(true);
        endText(true);
        cancelInput();
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
    m_zoom = std::clamp(m_zoom * factor, m_zoomMin, m_zoomMax);
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
    bool const oldControls = showsPlanningControls();
    if (playing && !m_playing)
        cancelInput();
    m_playing = playing;
    if (oldControls != showsPlanningControls())
        emit planningControlsChanged();
    if (playing)
        clearSelection();
    update();
}

void Canvas::setTransformOptions(TransformOptions const& options)
{
    bool const oldControls = showsPlanningControls();
    m_tx = options;
    update();
    if (oldControls != showsPlanningControls())
        emit planningControlsChanged();
}

void Canvas::setCurrentCell(int layer, int frame)
{
    // (However it goes, the Morphing tool's panel is told when it is done.)
    struct Told {
        Canvas* canvas;
        ~Told() { emit canvas->morphHereChanged(); }
    } const told{this};
    if (layer != m_layer || frame != m_frame) {
        finishPolyline(true);
        endText(true);
        cancelInput();
    }
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
        setSelection({}, {});
        m_ceStroke = m_cePoint = m_ceFill = -1;
        emit pointPicked(-1);
    }
    if (layer != m_layer && !m_transformLayers.isEmpty()) {
        // Another of those picked with the Transform tool made the current one: the one it was stays picked with them.
        QVector<int> others = m_transformLayers;
        if (others.removeAll(layer) > 0) {
            others.push_back(m_layer);
        } else {
            others.clear();
        }
        m_transformLayers = others;
        emit transformLayersChanged(m_transformLayers);
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
    else if (isPaintTool(m_tool) || m_tool == Dropper || m_tool == Ink)
        setCursor(toolCursor());
    else if (m_tool == Zoom || m_tool == RotateView || m_tool == ShiftTrace)
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
    if (index == Layer::kNoDrawing)
        return nullptr;
    Drawing const& rest = scene.layers[m_layer].drawings[index];
    return pointsPosed() ? pointViewOf(rest, index) : &rest;
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
            whole.set = real->set;
            whole.model = real->model;
            whole.modelCentre = real->modelCentre;
            whole.modelScale = real->modelScale;
            whole.trace = real->trace;
            whole.pivotSet = real->pivotSet;
            whole.pivot = real->pivot;
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
    cancelInput();
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

QPainterPath Canvas::overlayMask() const
{
    QPainterPath mask;
    Scene const& scene = m_document->scene();
    if (!m_maskLayers || m_layer < 0 || m_layer >= scene.layers.size())
        return mask;
    QTransform const toMine = worldOf(m_layer, m_frame).inverted();
    for (int l : m_maskLayers()) {
        if (l == m_layer || l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        Layer const& layer = scene.layers[l];
        int const index = m_frame >= 0 && m_frame < layer.cells.size() ? layer.cells[m_frame] : -1;
        if (index < 0 || index >= layer.drawings.size())
            continue;
        Drawing const& d = layer.drawings[index];
        QTransform const there = worldOf(l, m_frame) * toMine;
        auto add = [&](QVector<QPolygonF> const& outlines) {
            QPainterPath piece;
            piece.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& outline : outlines) {
                piece.addPolygon(there.map(outline));
                piece.closeSubpath();
            }
            mask = mask.isEmpty() ? piece : mask.united(piece);
        };
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            for (Fill const& fill : d.fillsOf(sheet))
                add(fill.outlines);
            for (Stroke const& line : d.strokesOf(sheet))
                if (line.width > 0.0f)
                    add(line.outlinePolygons());
        }
    }
    return mask.isEmpty() ? mask : mask.simplified();
}
