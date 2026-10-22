// Eclipse - the Camera view painted: onion skin, snapshots and view modes, the layers of the frame.
#include "canvas/Canvas.h"
#include "actions/theme/InterfaceColours.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

// A drawing of the onion skin: whole, in its colour - or only its outlines.
void Canvas::paintOnion(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const& tint) const
{
    if (!m_onionOutlines && m_onionBrightnessAlpha) {
        // The lighter a colour, the fainter: its own colour (from its swatch) given an opacity as dark as it is.
        Drawing faded = drawing;
        auto fade = [&scene](QColor colour) {
            float const light = (0.299f * colour.redF() + 0.587f * colour.greenF() + 0.114f * colour.blueF());
            colour.setAlphaF(colour.alphaF() * std::clamp(1.0f - light, 0.0f, 1.0f));
            return colour;
        };
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            for (Fill& f : faded.fillsOf(sheet)) {
                f.color = fade(scene.resolve(f.swatch, f.color));
                f.swatch = -1;
            }
            for (Stroke& s : faded.strokesOf(sheet)) {
                s.color = fade(scene.resolve(s.swatch, s.color));
                s.swatch = -1;
            }
        }
        paintDrawing(painter, scene, faded, opacity, m_onionOriginal ? nullptr : &tint);
        return;
    }
    if (!m_onionOutlines) {
        paintDrawing(painter, scene, drawing, opacity, m_onionOriginal ? nullptr : &tint);
        return;
    }
    painter.save();
    painter.setOpacity(painter.opacity() * opacity);
    QPen edge(tint, 1.4);
    edge.setCosmetic(true);
    painter.setPen(edge);
    painter.setBrush(Qt::NoBrush);
    for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::LineSheet), int(Drawing::OverlaySheet)}) {
        for (Fill const& fill : drawing.fillsOf(sheet))
            for (QPolygonF const& outline : fill.outlines)
                painter.drawPolygon(outline);
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            if (stroke.width <= 0.0f)
                continue;       // a line that is not seen has no outline to show
            QPolygonF centre;
            for (StrokePoint const& p : stroke.path())
                centre << p.pos;
            if (stroke.closed)
                painter.drawPolygon(centre);
            else
                painter.drawPolyline(centre);
        }
    }
    painter.restore();
}

void Canvas::takeSnapshot()
{
    m_snapshot = modeImage(m_document->scene(), m_frame, ViewRender);
    update();
}

QImage Canvas::modeImage(Scene const& scene, int frame, ViewMode mode)
{
    QSize const size(std::max(1, scene.width), std::max(1, scene.height));
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    if (mode == ViewDepth) {
        // Each layer alone; at each point, the nearest layer painted there - light near the camera, dark far from it.
        out.fill(Qt::black);
        QVector<int> layers;
        double nearest = -1e300, furthest = 1e300;
        for (int l : scene.drawOrder(frame)) {
            Layer const& layer = scene.layers[l];
            if (!scene.enabled(l) || layer.kind == Layer::ColourCard || layer.drawingAt(frame) == Layer::kNoDrawing || !layer.holdsDrawings())
                continue;
            layers.push_back(l);
            double const z = scene.depthOf(l, frame);
            nearest = std::max(nearest, z);
            furthest = std::min(furthest, z);
        }
        QVector<float> depth(qsizetype(size.width()) * size.height(), -std::numeric_limits<float>::infinity());
        QTransform const camera = scene.cameraTransform(frame);
        for (int l : std::as_const(layers)) {
            Layer const& layer = scene.layers[l];
            QImage alone(size, QImage::Format_ARGB32_Premultiplied);
            alone.fill(Qt::transparent);
            {
                QPainter p(&alone);
                p.setTransform(scene.worldTransform(l, frame) * camera);
                Pose const pose = scene.poseOf(l, frame);
                paintLayerDrawing(p, scene, l, frame,
                                  Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[layer.drawingAt(frame)], layer, pose)),
                                  1.0, camera);
            }
            double const z = scene.depthOf(l, frame);
            int const grey = nearest - furthest < 1e-9 ? 255 : int(std::lround(60.0 + 195.0 * (z - furthest) / (nearest - furthest)));
            for (int y = 0; y < size.height(); ++y) {
                QRgb const* const from = reinterpret_cast<QRgb const*>(alone.constScanLine(y));
                QRgb* const to = reinterpret_cast<QRgb*>(out.scanLine(y));
                float* const d = depth.data() + qsizetype(y) * size.width();
                for (int x = 0; x < size.width(); ++x)
                    if (qAlpha(from[x]) > 127 && float(z) >= d[x]) {
                        d[x] = float(z);
                        to[x] = qRgb(grey, grey, grey);
                    }
            }
        }
        return out;
    }
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (mode == ViewMatte) {
            // (The matte is of what is painted: the paper behind it is clear.)
            Scene clear = scene;
            clear.background = Qt::transparent;
            paintFrame(p, clear, frame);
        } else {
            paintFrame(p, scene, frame);
        }
    }
    if (mode == ViewMatte) {
        for (int y = 0; y < size.height(); ++y) {
            QRgb* const row = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < size.width(); ++x) {
                int const a = qAlpha(row[x]);
                row[x] = qRgb(a, a, a);
            }
        }
    }
    return out;
}

void Canvas::paintEvent(QPaintEvent*)
{
    QElapsedTimer drawing;
    drawing.start();
    // Preferences: pencil lines shown fainter in the view (only here: what is exported keeps them as they are).
    s_lineOpacity = m_pencilOpacity / 100.0;
    // (While anything moves - the pen, the playhead - paint is drawn with its quick edge.)
    s_fineFills = m_drag == DragNone && !m_playing;
    paintView();
    s_fineFills = true;
    s_lineOpacity = 1.0;
    m_lastDrawMs = drawing.nsecsElapsed() / 1.0e6;
    m_averageDrawMs = m_averageDrawMs <= 0.0 ? m_lastDrawMs : m_averageDrawMs * 0.9 + m_lastDrawMs * 0.1;
    if (m_performanceReport) {
        // The Camera View performance report: how long this picture took, and how many such a second that makes.
        QPainter painter(this);
        QString const text = tr("%1 ms  (average %2 ms, %3 a second)").arg(m_lastDrawMs, 0, 'f', 1).arg(m_averageDrawMs, 0, 'f', 1)
                                 .arg(m_averageDrawMs > 0.0 ? 1000.0 / m_averageDrawMs : 0.0, 0, 'f', 0);
        QRectF const box(8, 8, painter.fontMetrics().horizontalAdvance(text) + 12, painter.fontMetrics().height() + 6);
        painter.fillRect(box, QColor(0, 0, 0, 160));
        painter.setPen(QColor(120, 230, 140));
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

void Canvas::paintView()
{
    Scene const& scene = m_document->scene();
    QPainter painter(this);
    painter.fillRect(rect(), m_backdrop);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(cameraViewTransform());

    QRectF const frameRect(0, 0, scene.width, scene.height);

    // Playing from the preview: the frame as it was rendered, and nothing else to work out.
    if (m_playing && m_usePreview && !m_drawingView && m_frame >= 0 && m_frame < m_preview.size() && !m_preview[m_frame].isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(frameRect, m_preview[m_frame]);
        QPen border(QColor(70, 70, 70));
        border.setCosmetic(true);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frameRect);
        return;
    }

    // A snapshot shown, or Render, Matte or Depth View: that picture of the frame, and nothing else.
    if (((m_showSnapshot && !m_snapshot.isNull()) || m_viewMode != ViewNormal) && !m_drawingView && m_frame >= 0) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!(m_showSnapshot && !m_snapshot.isNull()) && (m_modeCache.isNull() || m_modeCacheFrame != m_frame || m_modeCacheMode != m_viewMode)) {
            // (Made again only when the frame, the mode or the scene has changed.)
            m_modeCache = modeImage(scene, m_frame, m_viewMode);
            m_modeCacheFrame = m_frame;
            m_modeCacheMode = m_viewMode;
        }
        painter.drawImage(frameRect, m_showSnapshot && !m_snapshot.isNull() ? m_snapshot : m_modeCache);
        QPen border(QColor(70, 70, 70));
        border.setCosmetic(true);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frameRect);
        return;
    }

    bool const transforming = m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate || m_drag == DragPoint || m_drag == DragHandle
                              || m_drag == DragSegment || m_drag == DragCeBox;
    bool const erasing = (m_drag == DragErase && (m_eraserCuts || bitmapNow()) && m_eraseAny) || m_drag == DragGradient || m_drag == DragThickness || ((m_drag == DragInk || m_drag == DragSmooth || m_drag == DragRepaint || m_drag == DragRetouch) && m_inked) || m_drag == DragEnvelope;
    QTransform const view = viewTransform();
    // The grid, when it is to lie behind the drawings.
    if (m_grid && m_gridUnder && !m_playing)
        paintGrid(painter, frameRect);
    // Back to front - with the layer being drawn on last of all when that is asked for.
    QVector<int> order;
    // (In the Drawing view there is no camera and no depth: the layers as they are stacked.)
    QVector<int> stacked(scene.layers.size());
    std::iota(stacked.begin(), stacked.end(), 0);
    bool heldBack = false;
    for (int l : m_drawingView ? stacked : scene.drawOrder(m_frame)) {
        if (!scene.inDisplay(l))
            continue;      // (not in what the display shows)
        if (m_currentOnTop && !m_playing && l == m_layer)
            heldBack = true;
        else
            order.push_back(l);
    }
    if (heldBack && scene.inDisplay(m_layer))
        order.push_back(m_layer);
    if (m_drawingView) {
        // The sheet of paper the drawing is on.
        painter.setTransform(cameraViewTransform());
        painter.fillRect(frameRect, Qt::white);
    }
    // Onion skin: the neighbouring drawings of the layer being edited, tinted - underneath it, or (Preferences) over everything.
    auto onionsOf = [&](int l) {
        Layer const& layer = scene.layers[l];
        if (!m_onionSkin || m_playing || (l != m_layer && (!layer.onion || !m_onionOthers)))
            return;
        // Each is shown where the layer is on its own frame; the further off, the fainter.
        double const strength = m_onionOpacity / 100.0;
        for (int i = 0, prev = m_frame; i < m_onionPrev; ++i) {
            prev = m_onionByFrame ? prev - 1 : layer.previousDrawingFrame(prev);
            if (prev < 0)
                break;
            if (layer.drawingAt(prev) == Layer::kNoDrawing)
                continue;       // an empty frame: nothing of it to show
            if (m_onionMark != Drawing::NoMark && layer.drawings[layer.cells[prev]].mark != m_onionMark) {
                --i;            // not one of the drawings asked for: the next one back is looked at instead
                continue;
            }
            painter.setTransform(worldOf(l, prev) * view);
            if (m_onionStrength[0][i] > 0)
                paintOnion(painter, scene, shown(l, prev, layer.drawings[layer.cells[prev]]),
                           strength * (m_onionPrev - i) / m_onionPrev * m_onionStrength[0][i] / 100.0, onionStepColour(false, i));
        }
        for (int i = 0, next = m_frame; i < m_onionNext; ++i) {
            next = m_onionByFrame ? next + 1 : layer.nextDrawingFrame(next);
            if (next < 0 || next >= scene.frameCount())
                break;
            if (layer.drawingAt(next) == Layer::kNoDrawing)
                continue;
            if (m_onionMark != Drawing::NoMark && layer.drawings[layer.cells[next]].mark != m_onionMark) {
                --i;
                continue;
            }
            painter.setTransform(worldOf(l, next) * view);
            if (m_onionStrength[1][i] > 0)
                paintOnion(painter, scene, shown(l, next, layer.drawings[layer.cells[next]]),
                           strength * 0.86 * (m_onionNext - i) / m_onionNext * m_onionStrength[1][i] / 100.0, onionStepColour(true, i));
        }
    };
    for (int l : std::as_const(order))
        paintViewLayer(painter, l, frameRect, view, transforming, erasing, onionsOf);
    if (m_onionOver)
        for (int l : std::as_const(order))
            if (scene.shows(l) && scene.layers[l].holdsDrawings() && (!m_drawingView || l == m_layer || m_lightTable))
                onionsOf(l);

    if (paintViewAids(painter, frameRect, view))
        return;
    paintSelectionAids(painter);
}

// A layer of the frame painted in the view (as paintView goes through them, back to front).
void Canvas::paintViewLayer(QPainter& painter, int l, QRectF const& frameRect, QTransform const& view, bool transforming, bool erasing,
                            std::function<void(int)> const& onionsOf)
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[l];
    // In the Drawing view: the current layer; and the others only on the light table.
    if (m_drawingView && l != m_layer && (!m_lightTable || !layer.holdsDrawings()))
        return;
    if (layer.kind == Layer::ColourCard && scene.shows(l)) {
        QColor colour = layer.cardColor;
        colour.setAlphaF(colour.alphaF() * float(layer.opacity));
        painter.setTransform(cameraViewTransform());
        painter.fillRect(frameRect, colour);
        return;
    }
    if (!scene.shows(l) || !layer.holdsDrawings())
        return;
    painter.setTransform(worldOf(l, m_frame) * view);
    if (!m_onionOver)
        onionsOf(l);
    painter.setTransform(worldOf(l, m_frame) * view);
    int const index = layer.drawingAt(m_frame);
    // The light table: while drawing, the layers that are not being drawn on are faint.
    double const faint = m_lightTable && !m_playing && l != m_layer && m_tool != Transform && m_tool != IK && m_tool != Rigging ? m_lightWash / 100.0 : 1.0;
    if (index != Layer::kNoDrawing && layer.morphing(m_frame)) {
        static QColor const dark(34, 34, 38);
        Drawing const made = shown(l, m_frame, layer.shownAt(m_frame));
        if (!m_playing && m_backlight)
            paintDrawing(painter, scene, made, 1.0, &dark);
        else
            paintLayerDrawing(painter, scene, l, m_frame, made, layer.opacity * poseOf(l, m_frame).opacity * faint, view);
        return;
    }
    if (index != Layer::kNoDrawing && !m_playing && m_backlight) {
        // Backlight: everything drawn as one dark shape, as if lit from behind.
        static QColor const dark(34, 34, 38);
        paintDrawing(painter, scene, shown(l, m_frame, l == m_layer && erasing ? withArt(m_eraseArt) : l == m_layer && transforming ? followLines(withArt(previewDrawing()), realDrawing(), true) : layer.drawings[index]), 1.0, &dark);
    } else if (index != Layer::kNoDrawing && !m_playing && m_outlineLocked && layer.locked) {
        // A locked layer as outlines: the edges of its paint and the paths of its lines.
        Drawing const lines = shown(l, m_frame, layer.drawings[index]);
        QPen edge(layer.trackColor.isValid() ? layer.trackColor : QColor(200, 70, 60));
        edge.setCosmetic(true);
        painter.setPen(edge);
        painter.setBrush(Qt::NoBrush);
        for (QVector<Fill> const* fills : {&lines.underlayFills, &lines.colourFills, &lines.fills, &lines.overlayFills})
            for (Fill const& fill : *fills)
                for (QPolygonF const& outline : fill.outlines)
                    painter.drawPolygon(outline);
        for (QVector<Stroke> const* strokes : {&lines.underlayStrokes, &lines.colourStrokes, &lines.strokes, &lines.overlayStrokes})
            for (Stroke const& stroke : *strokes) {
                QPolygonF centre;
                for (StrokePoint const& p : stroke.path())
                    centre << p.pos;
                if (stroke.closed)
                    painter.drawPolygon(centre);
                else
                    painter.drawPolyline(centre);
            }
    } else if (l == m_layer && transforming)
        paintLayerDrawing(painter, scene, l, m_frame, followLines(withArt(previewDrawing()), realDrawing(), true), layer.opacity * poseOf(l, m_frame).opacity, view, !m_previewAllArt);
    else if (index != Layer::kNoDrawing && l == m_layer && !m_previewAllArt && !m_playing) {
        // Only the sheet being worked on.
        Drawing one = erasing ? withArt(m_eraseArt) : layer.drawings[index];
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            if (sheet != sheetOf(m_art) && !(bothArtsNow() && (sheet == Drawing::LineSheet || sheet == Drawing::ColourSheet))) {
                one.fillsOf(sheet).clear();
                one.strokesOf(sheet).clear();
            }
        }
        paintLayerDrawing(painter, scene, l, m_frame, shown(l, m_frame, one), layer.opacity * poseOf(l, m_frame).opacity, view, true);
    } else if (index != Layer::kNoDrawing)
        paintLayerDrawing(painter, scene, l, m_frame, shown(l, m_frame, erasing && l == m_layer ? withArt(m_eraseArt) : layer.drawings[index]),
                          layer.opacity * poseOf(l, m_frame).opacity * faint
                              * (m_showStrokes && m_strokesWash && l == m_layer && !m_playing ? 0.3 : 1.0), view);
    if ((m_showStrokes || m_tool == StrokeTool || m_tool == CloseGap) && !m_playing && l == m_layer && index != Layer::kNoDrawing) {
        // Where the pen went, and the edges of the painted areas: hair lines over the artwork.
        Drawing const lines = erasing ? withArt(m_eraseArt) : transforming ? previewDrawing() : layer.drawings[index];
        QPen hair(QColor(40, 200, 235));
        hair.setCosmetic(true);
        painter.setPen(hair);
        painter.setBrush(Qt::NoBrush);
        for (Fill const& fill : lines.fills)
            for (QPolygonF const& outline : fill.outlines)
                painter.drawPolygon(outline);
        for (Stroke const& stroke : lines.strokes) {
            QPolygonF centre;
            for (StrokePoint const& p : stroke.path())
                centre << p.pos;
            if (stroke.closed)
                painter.drawPolygon(centre);
            else
                painter.drawPolyline(centre);
        }
    }
    if (l == m_layer && m_drag == DragDraw && m_tool == Pencil && m_pencilMode != PencilDraw)
        paintStroke(painter, m_stroke, QBrush(m_pushHighlight));     // a highlight: it pushes or thickens, it is not kept
    else if (l == m_layer && (m_drag == DragDraw || m_drag == DragShape))
        paintStroke(painter, m_stroke, scene.brushFor(m_stroke));
    if (l == m_layer && (m_drag == DragDraw || m_drag == DragShape) && m_stroke.width <= 0.0f) {
        // A line with no thickness, as it is drawn: a hair line.
        QPen hair(QColor(40, 200, 235));
        hair.setCosmetic(true);
        painter.setPen(hair);
        painter.setBrush(Qt::NoBrush);
        QPolygonF centre;
        for (StrokePoint const& p : m_stroke.path())
            centre << p.pos;
        painter.drawPolyline(centre);
    }
    if (l == m_layer && m_brushOutline && (m_tool == Brush || m_tool == Eraser || m_tool == Pencil) && m_hoverKnown && !m_playing
        && layer.holdsDrawings()) {
        // The size of what the tool draws, round the pointer (as big as the layer is shown).
        QTransform const where = layerViewTransform();
        double const radius = m_brushWidth / 2.0 * std::sqrt(std::abs(where.determinant()));
        painter.save();
        painter.resetTransform();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 200), 2.0));
        painter.drawEllipse(m_hoverWidget, radius, radius);
        painter.setPen(QPen(QColor(20, 20, 20, 220), 1.0));
        painter.drawEllipse(m_hoverWidget, radius, radius);
        painter.restore();
    }
    if (l == m_layer && m_tool == Ink && m_inkShowInkable && !m_playing && layer.holdsDrawings()) {
        // Show Inkable Lines: each line the Ink tool would change, traced over in a dashed light line.
        if (Drawing const* const d = currentDrawing()) {
            QColor const colour = m_document->scene().resolve(m_swatch, Qt::black);
            QTransform const where = layerViewTransform();
            painter.save();
            painter.resetTransform();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setBrush(Qt::NoBrush);
            QPen dashed(QColor(80, 220, 255, 220), 1.5, Qt::DashLine);
            painter.setPen(dashed);
            for (Stroke const& stroke : d->strokes) {
                if (stroke.width <= 0.0f || keepsColour(stroke.swatch) || (stroke.swatch == m_swatch && stroke.color == colour))
                    continue;
                QPolygonF centre;
                for (StrokePoint const& sp : stroke.path())
                    centre << where.map(sp.pos);
                painter.drawPolyline(centre);
            }
            painter.restore();
        }
    }
    if (l == m_layer && m_tool == SmoothEditor && m_hoverKnown && !m_playing && layer.holdsDrawings()) {
        // The smoothing tool's brush, round the pointer, in its colour (Preferences > Colours).
        QTransform const where = layerViewTransform();
        double const radius = std::max(m_brushWidth / 2.0, 4.0 / std::max(m_zoom, 1e-6)) * std::sqrt(std::abs(where.determinant()));
        painter.save();
        painter.resetTransform();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(InterfaceColours::get("smoothBrush"));
        painter.drawEllipse(m_hoverWidget, radius, radius);
        painter.restore();
    }
    if (l == m_layer && m_tool == Stamp && m_hoverKnown && !m_playing && m_drag == DragNone && layer.holdsDrawings()) {
        // The stamp, faint, where a click would put it.
        double const k = m_stampScale / 100.0;
        QPointF const at = toLayer(m_hoverWidget);
        painter.save();
        painter.setTransform(QTransform::fromScale(k, k) * QTransform::fromTranslate(at.x(), at.y()) * layerViewTransform());
        paintDrawing(painter, scene, stampNow(), 0.45);
        painter.restore();
    }
    if (l == m_layer && m_tool == PencilEditor && !m_playing && layer.holdsDrawings()) {
        // The thickness points of the pencil lines, to take hold of: a mark on the line and one on each edge there.
        Drawing const* const lines = m_drag == DragThickness ? &m_eraseArt : currentDrawing();
        if (lines) {
            QTransform const where = layerViewTransform();
            painter.save();
            painter.resetTransform();
            painter.setRenderHint(QPainter::Antialiasing, true);
            for (int i = 0; i < lines->strokes.size(); ++i) {
                Stroke line = lines->strokes[i];
                if (line.width <= 0.0f || line.points.size() < 2)
                    continue;
                line.ownThickness();
                for (int k = 0; k < line.thickness.size(); ++k) {
                    ThicknessPoint const& key = line.thickness[k];
                    QPointF const centre = line.pointAt(key.at), way = line.directionAt(key.at);
                    QPointF const across(way.y(), -way.x());
                    QPointF const left = where.map(centre + across * (line.width / 2.0 * key.left));
                    QPointF const right = where.map(centre - across * (line.width / 2.0 * key.right));
                    QPointF const middle = where.map(centre);
                    bool const held = i == m_thickStroke && k == m_thickKey;
                    painter.setPen(QPen(QColor(60, 200, 230), 1.0));
                    painter.drawLine(left, right);
                    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                    painter.setBrush(held ? QColor(255, 255, 255) : QColor(60, 200, 230));
                    QPolygonF diamond;
                    diamond << middle + QPointF(0, -4) << middle + QPointF(4, 0) << middle + QPointF(0, 4) << middle + QPointF(-4, 0);
                    painter.drawPolygon(diamond);
                    painter.setBrush(held ? QColor(255, 255, 255) : QColor(255, 210, 70));
                    painter.drawEllipse(left, 2.5, 2.5);
                    painter.drawEllipse(right, 2.5, 2.5);
                }
            }
            painter.restore();
        }
    }
    if (l == m_layer && m_tool == Envelope && !m_playing && hasSelection()) {
        // The outline round what is selected, and its points to take hold of.
        QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
        if (points.size() >= 9) {
            QTransform const where = layerViewTransform();
            painter.save();
            painter.resetTransform();
            QPen outline(QColor(240, 160, 60), 1.2);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(where.map(Deform::warpLine(points, true, 12)));
            if (m_envAdvanced && m_envPicked >= 0 && m_envPicked + 2 < points.size()) {
                // The handles of the point pressed last.
                QPointF const at = where.map(points[m_envPicked]);
                for (int which : {1, 2}) {
                    QPointF const handle = where.map(points[m_envPicked] + points[m_envPicked + which]);
                    painter.setPen(QPen(QColor(240, 160, 60), 1.0, Qt::DotLine));
                    painter.drawLine(at, handle);
                    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                    painter.setBrush(m_envPoint == m_envPicked + which ? QColor(255, 255, 255) : QColor(240, 160, 60));
                    painter.drawEllipse(handle, 3.5, 3.5);
                }
                painter.setBrush(Qt::NoBrush);
            }
            painter.setPen(QPen(QColor(20, 20, 20), 1.0));
            for (int i = 0; i < points.size(); i += 3) {
                QPointF const at = where.map(points[i]);
                painter.setBrush(i == m_envPoint || (m_envPoint < 0 && i == m_envPicked) ? QColor(255, 255, 255) : QColor(240, 160, 60));
                painter.drawRect(QRectF(at.x() - 3.5, at.y() - 3.5, 7, 7));
            }
            painter.restore();
        }
    }
    if (l == m_layer && m_polyActive) {
        // The polyline so far, and on to where the next point would go.
        Stroke preview = m_stroke;
        StrokePoint next{m_polyHover, 1.0f};
        next.corner = preview.curve;
        if (m_drag != DragPolyHandle)
            preview.points.push_back(next);
        paintStroke(painter, preview, scene.brushFor(preview));
    }
}

