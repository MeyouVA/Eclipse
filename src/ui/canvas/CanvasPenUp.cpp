// Eclipse - the Camera view: the pen lifted - a line kept, a shape kept, paint under a closed line.
#include "ui/canvas/Canvas.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QSet>
#include <QInputDialog>
#include <QFile>

#include <QImage>
#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"

void Canvas::commitPenStroke(Stroke const& stroke, double tolerance)
{
    // Overlay Brush Mode: kept only where the artwork of the layers chosen is.
    QPainterPath const mask = m_overlayBrush && (m_tool == Brush || m_tool == Pencil) ? overlayMask() : QPainterPath();
    QVector<QPolygonF> maskShape;
    for (QPolygonF const& outline : mask.toSubpathPolygons())
        maskShape.push_back(outline);
    if (bitmapNow())
        m_paintClip = mask;
    else if (!mask.isEmpty() && !(m_tool == Brush && m_brushShapes)) {
        QVector<QPolygonF> outside{QPolygonF(stroke.bounds().adjusted(-1000, -1000, 1000, 1000).united(mask.boundingRect()))};
        outside += maskShape;
        QVector<Stroke> const pieces = stroke.minus(outside, std::max(tolerance * 0.5, 0.05));
        if (pieces.isEmpty())
            return;
        m_document->beginMacro(tr("Draw Stroke"));
        for (Stroke const& piece : pieces)
            addStrokeToArt(piece);
        m_document->endMacro();
        return;
    }
    if (bitmapNow()) {
        // A bitmap layer: the stroke is painted into the pixels of the drawing, looking as it would as a line or a shape.
        Scene const& scene = m_document->scene();
        bool const asShape = m_tool == Brush && m_brushShapes;
        Fill shape;
        if (asShape) {
            shape = Fill::fromStroke(stroke, std::max(tolerance * 0.5, 0.05));
            shape.texture = m_brushTexture;
            shape.textureAmount = float(m_brushTextureAmount) / 100.0f;
            shape.textureCrisp = m_brushTextureCrisp;
            shape.textureLow = float(m_brushRampLow) / 100.0f;
            shape.textureHigh = float(m_brushRampHigh) / 100.0f;
            if (shape.outlines.isEmpty())
                return;
        }
        // The stroke at a share of its thickness, painted: as a shape (the Brush) or as a line.
        auto paintAt = [&](QPainter& onto, double share) {
            if (m_tool == Brush && (m_bitmapTip != BitmapRound || m_tipShape.engaged())) {
                stampTip(onto, stroke, share, scene.brushFor(stroke));
                return;
            }
            if (!asShape) {
                Stroke thinner = stroke;
                thinner.width = float(stroke.width * share);
                paintStroke(onto, thinner, scene.brushFor(stroke));
                return;
            }
            Fill piece = shape;
            if (share < 1.0) {
                Stroke thinner = stroke;
                thinner.width = float(stroke.width * share);
                piece = Fill::fromStroke(thinner, std::max(tolerance * 0.5, 0.05));
                piece.texture = shape.texture;
                piece.textureAmount = shape.textureAmount;
                piece.textureCrisp = shape.textureCrisp;
                piece.textureLow = shape.textureLow;
                piece.textureHigh = shape.textureHigh;
            }
            if (!piece.outlines.isEmpty())
                paintFill(onto, piece, scene.brushFor(shape));
        };
        bool const soft = m_tool == Brush && (m_bitmapHardness < 100 || m_bitmapFlow < 100 || m_tipShape.mode != TipShape::Normal || m_tipShape.dual);
        paintIntoBitmap([&](QPainter& painter) {
            QImage* const target = dynamic_cast<QImage*>(painter.device());
            if (!soft || !target) {
                paintAt(painter, 1.0);
                return;
            }
            QImage dab(target->size(), QImage::Format_ARGB32_Premultiplied);
            dab.fill(Qt::transparent);
            {
                QPainter onto(&dab);
                onto.setRenderHints(painter.renderHints());
                onto.setTransform(painter.transform());
                double const hard = m_bitmapHardness / 100.0;
                int const passes = hard >= 1.0 ? 1 : 8;
                for (int i = 0; i < passes; ++i) {
                    onto.setOpacity(1.0 / double(passes - i));
                    paintAt(onto, 1.0 - (1.0 - hard) * double(i) / passes);
                }
            }
            if (m_tool == Brush && m_tipShape.dual) {
                // The dual tip: the stroke kept only where the second tip, stamped along it, leaves its mark.
                QImage mask(target->size(), QImage::Format_ARGB32_Premultiplied);
                mask.fill(Qt::transparent);
                {
                    QPainter onto(&mask);
                    onto.setRenderHints(painter.renderHints());
                    onto.setTransform(painter.transform());
                    TipShape const& t = m_tipShape;
                    Dabs second;
                    second.kind = t.dualKind;
                    second.roundness = t.dualRoundness;
                    second.angle = t.dualAngle;
                    second.count = t.dualCount;
                    second.spacing = t.dualSpacing;
                    second.flipX = t.dualFlipX;
                    second.flipY = t.dualFlipY;
                    second.randomAngle = t.dualRandomAngle;
                    second.randomSpacing = t.dualRandomSpacing;
                    second.randomCount = t.dualRandomCount;
                    second.randomSize = t.dualRandomSize;
                    second.randomOffset = t.dualRandomOffset;
                    second.size = std::max(0.5, stroke.width * t.dualSize / 100.0);
                    stampDabs(onto, stroke.path(), second, QBrush(Qt::black), m_dualImage);
                }
                QPainter keep(&dab);
                keep.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                keep.drawImage(QPointF(0, 0), mask);
            }
            painter.save();
            painter.resetTransform();
            painter.setOpacity(m_bitmapFlow / 100.0);
            // The tip's mode: over the pixels; only on those painted (a stencil); behind them; or taking them away.
            int const mode = m_tool == Brush ? m_tipShape.mode : TipShape::Normal;
            painter.setCompositionMode(mode == TipShape::Stencil ? QPainter::CompositionMode_SourceAtop
                                       : mode == TipShape::Behind ? QPainter::CompositionMode_DestinationOver
                                       : mode == TipShape::Erase ? QPainter::CompositionMode_DestinationOut
                                                                 : QPainter::CompositionMode_SourceOver);
            painter.drawImage(QPointF(0, 0), dab);
            painter.restore();
        }, asShape ? tr("Paint Brush Stroke") : tr("Paint Stroke"), stroke.bounds());
        return;
    }
    if (m_tool == Brush && m_brushShapes) {
        double const fine = std::max(tolerance * 0.5, 0.05);
        Fill shape = Fill::fromStroke(stroke, fine);
        shape.texture = m_brushTexture;
        shape.textureAmount = float(m_brushTextureAmount) / 100.0f;
        shape.textureCrisp = m_brushTextureCrisp;
        shape.textureLow = float(m_brushRampLow) / 100.0f;
        shape.textureHigh = float(m_brushRampHigh) / 100.0f;
        if (!shape.outlines.isEmpty()) {
            Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
            // Drawn behind, the stroke is the one that gives way: what lies over it is cut out of it.
            QVector<Fill> mine{shape};
            if (!maskShape.isEmpty()) {
                mine = shape.within(maskShape, fine);       // Overlay Brush Mode
                if (mine.isEmpty())
                    return;
            }
            if (m_autoFlatten) {
                QVector<Fill> kept;
                for (Fill const& under : std::as_const(art.fills)) {
                    if (!under.overlaps(shape)) {
                        kept.push_back(under);
                    } else if (under.swatch == shape.swatch && under.color == shape.color && under.texture == shape.texture
                               && (shape.texture == 0 || (under.textureAmount == shape.textureAmount && under.textureCrisp == shape.textureCrisp
                                                           && under.textureLow == shape.textureLow && under.textureHigh == shape.textureHigh))) {
                        for (Fill& piece : mine)
                            if (piece.mergeWith(under, fine))
                                break;
                    } else if (m_drawBehind) {
                        kept.push_back(under);
                        QVector<Fill> left;
                        for (Fill const& piece : std::as_const(mine))
                            left += piece.minus(under.outlines, fine);
                        mine = left;
                    } else {
                        kept += under.minus(shape.outlines, fine);
                    }
                }
                art.fills = kept;
            }
            // In the order of the sheet: under everything (Draw Behind), or over everything - lines it lies on among them.
            int const place = m_drawBehind ? Drawing::bottomOrder(art.fills, art.strokes) : Drawing::orderOnTop(art.fills, art.strokes);
            for (Fill& piece : mine)
                piece.order = place;
            if (m_drawBehind)
                art.fills = mine + art.fills;
            else
                art.fills += mine;
            if (!mine.isEmpty())
                commitDrawing(art, tr("Draw Brush Stroke"));
        }
    } else if (m_tool == Brush && m_drawBehind) {
        Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
        Stroke placed = stroke;
        // Under everything: below the lowest place of the order when a painted area is there (areas lie under lines).
        int const bottom = Drawing::bottomOrder(art.fills, art.strokes);
        placed.order = std::any_of(art.fills.begin(), art.fills.end(), [bottom](Fill const& f) { return f.order == bottom; }) ? bottom - 1 : bottom;
        art.strokes.push_front(placed);
        commitDrawing(art, tr("Draw Stroke"));
    } else {
        addStrokeToArt(stroke);
    }
}

void Canvas::keepStroke(Stroke const& stroke, bool fromPen, double tolerance)
{
    // With a Symmetry guide: once more over its axis - and over every other axis of symmetry the scene has.
    QVector<QTransform> const mirrors = symmetries();
    // A line of the Line tool or the Polyline whose end was put on the end of another: the two are joined there.
    if (!fromPen && mirrors.isEmpty() && keepJoined(stroke))
        return;
    if (!mirrors.isEmpty())
        m_document->beginMacro(tr("Draw Stroke"));
    if (fromPen)
        commitPenStroke(stroke, tolerance);
    else
        addStrokeToArt(stroke);
    for (QTransform const& mirror : mirrors) {
        Stroke other = stroke;
        other.transform(mirror);
        if (fromPen)
            commitPenStroke(other, tolerance);
        else
            addStrokeToArt(other);
    }
    if (!mirrors.isEmpty())
        m_document->endMacro();
}

QPointF Canvas::stabilised(QPointF line, QPointF pen, int strength)
{
    // At 0 the line is where the pen is; at 100 it goes a twelfth of the way there with each move of the pen.
    double const follow = 1.0 - std::clamp(strength, 0, 100) / 100.0 * (11.0 / 12.0);
    return line + (pen - line) * follow;
}

void Canvas::bendLine(QPointF at)
{
    StrokePoint a{m_bendFrom, 1.0f}, b{m_bendTo, 1.0f};
    a.handles = b.handles = true;
    a.corner = b.corner = false;
    QPointF const middle = (m_bendFrom + m_bendTo) / 2.0;
    if (m_lineMode == LineCurve) {
        // One bow, through the pointer: the curve's middle is where the pointer is.
        QPointF const control = at * 2.0 - middle;
        a.out = (control - m_bendFrom) * (2.0 / 3.0);
        b.in = (control - m_bendTo) * (2.0 / 3.0);
    } else {
        // Two bows, one each way: the first third of the line goes towards the pointer, the last away from it.
        QPointF const along = (m_bendTo - m_bendFrom) / 3.0, off = (at - middle) * 1.5;
        a.out = along + off;
        b.in = -along - off;
    }
    a.in = -a.out;
    b.out = -b.in;
    m_stroke.curve = true;
    m_stroke.points = {a, b};
}

void Canvas::keepShape()
{
    stencilLine(m_stroke);
    // Auto Fill: a rectangle or an ellipse is painted inside with the colour in hand, under its line.
    bool const fill = m_shapeAutoFill && m_stroke.closed && (m_tool == Rectangle || m_tool == Ellipse) && !bitmapNow() && m_stroke.width > 0.0f;
    bool const twin = m_autoColourArt && m_art == LineArt && !bitmapNow() && m_stroke.width > 0.0f;
    if (!fill && !twin) {
        keepStroke(m_stroke, false);
        return;
    }
    m_document->beginMacro(tr("Draw Shape"));
    if (fill)
        fillUnder(m_stroke);
    keepStroke(m_stroke, false);
    if (twin)
        colourArtTwin(m_stroke);
    m_document->endMacro();
}

void Canvas::fillUnder(Stroke& line)
{
    Drawing art = currentDrawing() ? *currentDrawing() : Drawing();
    // The paint and its line know each other by a number of their own in the drawing.
    int next = 1;
    if (Drawing const* const whole = realDrawing())
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            for (Stroke const& s : whole->strokesOf(sheet))
                next = std::max(next, s.link + 1);
            for (Fill const& f : whole->fillsOf(sheet))
                next = std::max(next, f.link + 1);
        }
    line.link = next;
    QPolygonF outline;
    for (StrokePoint const& point : line.path())
        outline << point.pos;
    Fill area;
    area.outlines = {outline};
    area.follows = true;
    area.link = next;
    area.seed = insideOf(area.outlines);
    area.swatch = line.swatch;
    area.color = line.color;
    area.order = Drawing::topOrder(art.fills, art.strokes);
    art.fills.push_back(area);
    commitDrawing(art, tr("Paint"));
}

void Canvas::penUp()
{
    Drag const drag = m_drag;
    if (drag == DragSplineOffset) {
        QPointF const by = splineOffset();
        m_drag = DragNone;
        if (QLineF(m_splineFrom, m_splineNow).length() >= 1.0)
            m_document->offsetPath(m_layer, by);
        update();
        return;
    }
    if (drag == DragControlPoint) {
        m_drag = DragNone;
        if (m_cpMoved && m_cpDepth) {
            // Up the view is away from the camera (as Alt with the Transform tool moves a layer), two units a pixel.
            Scene const& scene = m_document->scene();
            Layer const& l = scene.layers[m_layer];
            if (m_cpKey >= 0 && m_cpKey + 1 < l.keys.size()) {
                Keyframe const& a = l.keys[m_cpKey];
                Keyframe const& b = l.keys[m_cpKey + 1];
                QVector<double> placed;
                a.pathLine(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), &placed);
                double const was = m_cpIndex < a.controlsZ.size() ? a.controlsZ[m_cpIndex] : a.pose.z + (b.pose.z - a.pose.z) * placed.value(m_cpIndex, 0.0);
                double const by = (m_cpDragPos.y() - m_cpPressPos.y()) / std::max(m_zoom, 1e-6) * 2.0;
                m_document->setControlPointDepth(m_layer, m_cpKey, m_cpIndex, was - by);
            }
        } else if (m_cpMoved) {
            m_document->moveControlPoint(m_layer, m_cpKey, m_cpIndex, controlPointFromView(m_cpKey, m_cpIndex, m_cpDragPos));
        }
        m_cpMoved = false;
        m_cpDepth = false;
        update();
        return;
    }
    if (drag == DragGuide) {
        m_drag = DragNone;
        QVector<Scene::Guide> const moved = m_guideEdit;
        m_guideEdit.clear();
        m_guidePoint = -1;
        m_guideLine = -1;
        if (moved != m_document->scene().guides)
            m_document->setGuides(moved, tr("Move Guide"));
        update();
        return;
    }
    if (drag == DragEnvelope) {
        m_drag = DragNone;
        bool const pulled = m_envNow != m_envRest;
        Drawing const art = m_eraseArt;
        m_eraseArt = Drawing();
        m_envRest.clear();
        m_envNow.clear();
        m_envPoint = -1;
        if (pulled)
            commitDrawing(art, tr("Envelope"));
        update();
        return;
    }
    if (drag == DragRepaint) {
        m_drag = DragNone;
        // Once more, the edges fitted with points and handles as the brush makes them.
        repaintTo(m_dragNow, std::max(0.25 / m_zoom, 0.05));
        if (m_inked) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, tr("Repaint Brush"));
        }
        m_eraseArt = Drawing();
        m_repaintFrom = Drawing();
        m_repaintPath = Stroke();
        m_inked = false;
        update();
        return;
    }
    if (drag == DragInk || drag == DragSmooth || drag == DragRetouch) {
        m_drag = DragNone;
        if (m_inked) {
            Drawing const art = m_eraseArt;
            commitDrawing(art, drag == DragInk ? tr("Ink") : drag == DragRetouch ? tr("Pencil Retouch") : tr("Smooth"));
        }
        m_eraseArt = Drawing();
        m_inked = false;
        update();
        return;
    }
    if (drag == DragThickness) {
        m_drag = DragNone;
        Drawing const* const d = currentDrawing();
        if (d && m_thickStroke >= 0 && m_thickStroke < m_eraseArt.strokes.size() && m_thickStroke < d->strokes.size()) {
            Stroke& line = m_eraseArt.strokes[m_thickStroke];
            if (!m_thickMoved && m_thickOneSide && m_thickExisting) {
                // Alt and a click on a thickness point: it goes (the thickness runs on between its neighbours).
                if (line.thickness.size() > 1 && m_thickKey >= 0 && m_thickKey < line.thickness.size()) {
                    line.thickness.removeAt(m_thickKey);
                    Drawing const art = m_eraseArt;
                    commitDrawing(art, tr("Remove Thickness Point"));
                }
            } else if (m_thickMoved) {
                Drawing const art = m_eraseArt;
                commitDrawing(art, tr("Pencil Line Thickness"));
            }
        }
        m_eraseArt = Drawing();
        m_thickStroke = m_thickKey = -1;
        update();
        return;
    }
    if (drag == DragGradient) {
        m_drag = DragNone;
        Drawing const* const d = currentDrawing();
        if (m_gradMoved && d && m_gradStroke >= 0 && m_gradStroke < d->strokes.size() && m_gradStroke < m_eraseArt.strokes.size()) {
            Stroke const& was = d->strokes[m_gradStroke];
            Stroke const& now = m_eraseArt.strokes[m_gradStroke];
            if (!was.gradPlaced || was.gradFrom != now.gradFrom || was.gradTo != now.gradTo) {
                Drawing const art = m_eraseArt;
                int const line = m_gradStroke;
                commitDrawing(art, tr("Edit Gradient"));
                m_gradStroke = line;        // it stays in hand
            }
        } else if (m_gradMoved && d && m_gradFill >= 0 && m_gradFill < d->fills.size() && m_gradFill < m_eraseArt.fills.size()) {
            Fill const& was = d->fills[m_gradFill];
            Fill const& now = m_eraseArt.fills[m_gradFill];
            if (!was.gradPlaced || was.gradFrom != now.gradFrom || was.gradTo != now.gradTo) {
                Drawing const art = m_eraseArt;
                commitDrawing(art, tr("Edit Gradient"));
            }
        }
        m_eraseArt = Drawing();
        update();
        return;
    }
    if (drag == DragHint) {
        m_drag = DragNone;
        hintUp();
        return;
    }
    if (drag == DragBoneIK) {
        m_drag = DragNone;
        if (m_ikBegun)
            m_document->endMacro();
        m_ikBegun = false;
        m_ikChain.clear();
        update();
        return;
    }
    if (drag == DragErase && (m_eraserCuts || bitmapNow())) {
        m_drag = DragNone;
        cutWithEraser();
        update();
        return;
    }
    if (drag == DragDraw) {
        m_drag = DragNone;
        // The stabiliser's Catch Up: the line goes on to where the pen lifted.
        if (m_stabiliser == StabiliserCatchUp && (m_tool == Brush || m_tool == Pencil) && !m_stroke.points.isEmpty()) {
            QPointF const from = m_stroke.points.last().pos;
            float const pressure = m_stroke.points.last().pressure;
            double const length = QLineF(from, m_stabiliserPen).length() * m_zoom;
            int const steps = int(std::ceil(length / 2.0));
            for (int i = 1; i <= steps; ++i)
                m_stroke.points.push_back({from + (m_stabiliserPen - from) * (double(i) / steps), pressure});
        }
        // Line Pushing, Auto Adjust Thickness: the stroke is done to the line it was drawn along, and is not kept.
        if (m_tool == Pencil && m_pencilMode != PencilDraw && !bitmapNow()) {
            Stroke const highlight = m_stroke;
            m_stroke = Stroke();
            Drawing edited = currentDrawing() ? *currentDrawing() : Drawing();
            if (pushLine(edited, highlight, m_pencilMode, 16.0 / std::max(m_zoom, 1e-6)))
                commitDrawing(edited, m_pencilMode == PencilPush ? tr("Push Line") : tr("Adjust Line Thickness"));
            else
                emit message(tr("Draw along a pencil line to push it, or to make it thicker."));
            update();
            return;
        }
        double pressed = 1.0;
        if (!m_rawPressure.isEmpty()) {
            pressed = 0.0;
            for (float p : std::as_const(m_rawPressure))
                pressed += p;
            pressed /= m_rawPressure.size();
        }
        double const tolerance = (0.2 + smoothingFor(pressed) * 0.038) / m_zoom;
        if (m_tool == Pencil && m_pencilMinOpacity < 100)
            smoothOpacity(m_stroke, m_opacitySmoothing);
        bool const ownThick = (m_tool == Pencil || m_tool == Brush) && m_stroke.width > 0.0f && !bitmapNow();
        Stroke thick = m_stroke;
        if (m_tool == Brush) {
            LineOptions speedOnly = m_lineOptions;
            speedOnly.startTaper = speedOnly.endTaper = speedOnly.fade = 0;
            applyTaperAndSpeed(thick, speedOnly, m_zoom);
        }
        if (m_tool == Brush || m_tool == Pencil)
            thick.points = evenlySampled(thick.points, 2.0 / std::max(m_zoom, 1e-6));
        int const stencil = thicknessStencilOf(Pencil);
        QVector<float> const& custom = m_customStencils[stencilSlot(Pencil)];
        if (m_tool == Pencil && stencil == StencilCount && !custom.isEmpty())
            applyProfile(thick, custom);
        else if (m_tool == Pencil && stencil != StencilUniform && stencil < StencilCount)
            applyStencil(thick, stencil);
        if (m_tool == Brush) {
            LineOptions shapeOnly = m_lineOptions;
            shapeOnly.speed = 0;
            applyTaperAndSpeed(thick, shapeOnly, m_zoom);
        }
        // (A line that faded away ends where it did; a bitmap stroke is painted along all of its samples.)
        if ((m_tool == Brush || m_tool == Pencil) && (!ownThick || (m_tool == Brush && m_lineOptions.fade > 0)))
            m_stroke.points = thick.points;
        // The line options (Tool Properties): trimmed and carried on to the lines it meets.
        double const near = 12.0 / std::max(m_zoom, 1e-6);
        if ((m_tool == Pencil || m_tool == StrokeTool) && currentDrawing()) {
            if (m_lineOptions.trim)
                trimExtra(m_stroke, *currentDrawing(), 30.0 / std::max(m_zoom, 1e-6));
            if (m_lineOptions.connect)
                connectEnds(m_stroke, *currentDrawing(), near);
        }
        if (m_snapping && m_snapGrid && (m_tool == Brush || m_tool == Pencil) && m_stroke.points.size() >= 2) {
            // Snap to Grid: the line begins and ends on a crossing of the grid when it is near one.
            m_stroke.points.first().pos = snapped(m_stroke.points.first().pos);
            m_stroke.points.last().pos = snapped(m_stroke.points.last().pos);
        }
        QVector<StrokePoint> const drawnSamples = thick.points;
        if (ownThick)
            for (StrokePoint& point : m_stroke.points) {
                point.pressure = 1.0f;
                point.lean = 0.0f;
            }
        m_stroke.fit(tolerance, m_cornerAngle);
        if (ownThick)
            m_stroke.takeThicknessFrom(drawnSamples);
        if (m_tool == StrokeTool && m_lineOptions.straight) {
            // Straight pieces from point to point.
            m_stroke.curve = false;
            for (StrokePoint& point : m_stroke.points) {
                point.handles = false;
                point.in = point.out = QPointF();
            }
        }
        bool const colourToo = m_autoColourArt && m_art == LineArt && (m_tool == Brush || m_tool == Pencil) && !bitmapNow();
        bool const finishing = ((m_tool == Brush || m_tool == Pencil) && !bitmapNow() && (m_lineOptions.closeGap || m_lineOptions.building)) || colourToo;
        if (finishing)
            m_document->beginMacro(tr("Draw Stroke"));
        double const reach = m_stroke.width / 2.0 + 8.0 / std::max(m_zoom, 1e-6);
        QRectF const box = m_stroke.bounds();
        double room = 0.0;
        {
            QVector<StrokePoint> const run = m_stroke.path();
            for (int i = 0; i < run.size(); ++i) {
                QPointF const a = run[i].pos, b = run[(i + 1) % run.size()].pos;
                room += a.x() * b.y() - b.x() * a.y();
            }
            room = std::abs(room) / 2.0;
        }
        double const least = std::max(double(m_stroke.width), 2.0 / std::max(m_zoom, 1e-6));
        if (m_lineAutoFill && (m_tool == Brush || m_tool == Pencil) && m_stroke.points.size() >= 3 && !bitmapNow()
            && QLineF(m_stroke.points.first().pos, m_stroke.points.last().pos).length() <= reach && std::max(box.width(), box.height()) > reach * 2.0
            && room > least * least * 4.0) {
            m_document->beginMacro(tr("Draw Stroke"));
            fillUnder(m_stroke);
            keepStroke(m_stroke, true, tolerance);
            m_document->endMacro();
        } else {
            keepStroke(m_stroke, true, tolerance);
        }
        if (finishing) {
            finishDrawnLine(near);
            if (colourToo && realDrawing()) {
                // The line just drawn (or the edge of the brush shape just made), with no thickness, into the colour art.
                Drawing whole = *realDrawing();
                QVector<Stroke> edges;
                if (m_tool == Brush && m_brushShapes && !whole.fills.isEmpty())
                    edges = whole.fills.last().contours;
                else if (!whole.strokes.isEmpty())
                    edges = {whole.strokes.last()};
                for (Stroke edge : std::as_const(edges)) {
                    edge.width = 0.0f;
                    edge.group = 0;
                    whole.colourStrokes.push_back(edge);
                }
                if (!edges.isEmpty()) {
                    m_committing = true;
                    m_document->setDrawing(m_layer, m_frame, whole, tr("Draw Stroke"));
                    m_committing = false;
                }
            }
            m_document->endMacro();
        }
    } else if (drag == DragShape) {
        // A click without a drag draws nothing.
        bool const drawn = m_stroke.bounds().width() * m_zoom > m_stroke.width * m_zoom + 3 || m_stroke.bounds().height() * m_zoom > m_stroke.width * m_zoom + 3;
        if (drawn && m_tool == Line && m_lineMode != LineStraight && m_stroke.points.size() == 2) {
            // The Line tool's curves: now the pointer bends the line, and a click puts it down.
            m_bending = true;
            m_bendFrom = m_stroke.points[0].pos;
            m_bendTo = m_stroke.points[1].pos;
            update();
            return;
        }
        m_drag = DragNone;
        if (drawn)
            keepShape();
    } else if (drag == DragSegment) {
        bool const moved = QLineF(m_dragStart, m_dragNow).length() * m_zoom > 1.0;
        Drawing const edited = moved ? previewDrawing() : Drawing();
        m_drag = DragNone;
        if (moved)
            commitDrawing(edited, tr("Bend Line"));
        m_segIndex = -1;
        update();
    } else if (drag == DragCeBox) {
        bool const moved = contourValid() && QLineF(m_dragStart, m_dragNow).length() * m_zoom > 1.0;
        Drawing const edited = moved ? previewDrawing() : Drawing();
        m_drag = DragNone;
        if (moved)
            commitDrawing(edited, m_ceBoxCorner == 4 ? tr("Rotate Points") : tr("Scale Points"));
        m_ceBoxCorner = -1;
        update();
    } else if (drag == DragPoint) {
        if (contourValid() && contourLine(*currentDrawing())->points[m_cePoint].pos != m_dragNow) {
            Drawing edited = previewDrawing();
            m_drag = DragNone;
            // An end dropped on an end: the line closes, or two lines become one.
            bool const joined = joinDroppedEnd(edited) || jointDroppedPoint(edited);
            int const stroke = m_ceStroke, point = m_cePoint;
            commitDrawing(edited, joined ? tr("Join Points") : tr("Move Point"));
            if (joined) {
                m_ceStroke = stroke;
                m_cePoint = point;
                m_ceFill = -1;
            }
        }
    } else if (drag == DragHandle && m_ceHandle == 2) {
        if (contourValid()) {
            bool const pulled = pulledHandles();
            Drawing edited = pulled ? previewDrawing() : *currentDrawing();
            m_drag = DragNone;
            int const stroke = m_ceStroke, point = m_cePoint, fill = m_ceFill, contour = m_ceContour;
            QString name = tr("Pull Handles");
            if (!pulled) {
                // Only clicked: a corner becomes smooth, a smooth point a corner (and the points lying on it with it).
                QVector<QPair<int, int>> const stacked = stackedPoints(edited);
                bool const corner = contourLine(edited)->isCorner(point);
                for (auto const& [line, at] : stacked) {
                    Stroke& target = line < 0 || m_ceFill >= 0 ? *contourLine(edited) : edited.strokes[line];
                    target.setCorner(at, !corner);
                }
                contourEdited(edited);
                name = corner ? tr("Smooth Point") : tr("Corner Point");
            }
            commitDrawing(edited, name);
            m_ceStroke = stroke;     // the point stays picked, with its handles showing
            m_cePoint = point;
            m_ceFill = fill;
            m_ceContour = contour;
        }
        m_ceHandle = -1;
    } else if (drag == DragHandle) {
        if (contourValid() && m_dragNow != m_dragStart) {
            Drawing const edited = previewDrawing();
            m_drag = DragNone;
            int const stroke = m_ceStroke, point = m_cePoint, fill = m_ceFill, contour = m_ceContour;
            commitDrawing(edited, tr("Move Handle"));
            m_ceStroke = stroke;     // the point stays picked, with its handles showing
            m_cePoint = point;
            m_ceFill = fill;
            m_ceContour = contour;
        }
        m_ceHandle = -1;
    } else if (drag == DragPaint) {
        m_drag = DragNone;
        QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
        QPolygonF const shape = m_lassoPath;
        m_lassoPath.clear();
        QRectF const reach = m_dragLasso ? shape.boundingRect() : rect;
        bool const lasso = m_dragLasso;
        QPointF const at = m_dragStart;
        auto paint = [&] {
            if (reach.width() * m_zoom <= 3 && reach.height() * m_zoom <= 3)
                paintAt(at);
            else if (lasso)
                paintInside([&shape](QPointF q) { return shape.containsPoint(q, Qt::OddEvenFill); }, shape.boundingRect());
            else
                paintInside([&rect](QPointF q) { return rect.contains(q); }, rect);
        };
        Scene const& scene = m_document->scene();
        QVector<QPair<int, QTransform>> others;
        if (m_paintLayers && isPaintTool(m_tool) && m_layer >= 0 && m_layer < scene.layers.size()) {
            for (int l = 0; l < scene.layers.size(); ++l) {
                if (l == m_layer || !scene.layers[l].holdsDrawings() || scene.layers[l].locked || !scene.shows(l))
                    continue;
                QTransform const theirs = worldOf(l, m_frame);
                if (theirs.isInvertible())
                    others.push_back({l, worldOf(m_layer, m_frame) * theirs.inverted()});
            }
        }
        if (!others.isEmpty()) {
            bool const small = reach.width() * m_zoom <= 3 && reach.height() * m_zoom <= 3;
            int const mine = m_layer, here = m_frame;
            m_document->beginMacro(m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
            others.push_front({mine, QTransform()});
            for (auto const& [l, to] : std::as_const(others)) {
                // (With Apply to All Drawings as well: every drawing of each of them.)
                QVector<int> frames = {here};
                if (m_paintAll) {
                    frames.clear();
                    QVector<int> const cells = m_document->scene().layers[l].cells;
                    QVector<int> seen;
                    for (int f = 0; f < cells.size(); ++f) {
                        if (cells[f] == Layer::kNoDrawing || seen.contains(cells[f]))
                            continue;
                        seen.push_back(cells[f]);
                        frames.push_back(f);
                    }
                }
                QPolygonF const there = to.map(lasso ? shape : QPolygonF(rect));
                m_layer = l;
                for (int f : std::as_const(frames)) {
                    if (m_document->scene().layers[l].drawingAt(f) == Layer::kNoDrawing)
                        continue;
                    m_frame = f;
                    if (small)
                        paintAt(to.map(at));
                    else
                        paintInside([&there](QPointF q) { return there.containsPoint(q, Qt::OddEvenFill); }, there.boundingRect());
                }
            }
            m_layer = mine;
            m_frame = here;
            m_document->endMacro();
            update();
        } else if (!m_paintAll || m_layer < 0 || m_layer >= scene.layers.size()) {
            paint();
        } else {
            // The same in every drawing of the layer, as one step: each drawing is come to in turn, by a frame that shows it.
            QVector<int> const cells = scene.layers[m_layer].cells;
            QVector<int> frames, seen;
            for (int f = 0; f < cells.size(); ++f) {
                if (cells[f] == Layer::kNoDrawing || seen.contains(cells[f]))
                    continue;
                seen.push_back(cells[f]);
                frames.push_back(f);
            }
            int const here = m_frame;
            m_document->beginMacro(m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
            for (int f : std::as_const(frames)) {
                m_frame = f;
                paint();
            }
            m_frame = here;
            m_document->endMacro();
        }
    } else if (drag == DragPointMarquee) {
        m_drag = DragNone;
        QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
        Drawing const* const d = currentDrawing();
        m_cePoint = -1;
        m_ceExtra.clear();
        if (rect.width() * m_zoom <= 2 && rect.height() * m_zoom <= 2) {
            m_ceStroke = m_ceFill = -1;     // a click on nothing: no curve is edited any more
        } else if (d && contourLine(*d)) {
            QVector<StrokePoint> const& points = contourLine(*d)->points;
            for (int i = 0; i < points.size(); ++i) {
                if (!rect.contains(points[i].pos))
                    continue;
                if (m_cePoint < 0)
                    m_cePoint = i;
                else
                    m_ceExtra.push_back(i);
            }
        }
        emit pointPicked(pointThickness());
    } else if (drag == DragMarquee) {
        m_drag = DragNone;
        QRectF const rect = QRectF(m_dragStart, m_dragNow).normalized();
        // A click without movement on empty space just clears the selection.
        if (m_tool == Cutter || (m_tool == Select && bitmapNow())) {
            // (The Select tool on a bitmap layer picks pixels: those in the shape are cut loose, to be moved.)
            QPolygonF const shape = m_dragLasso ? m_lassoPath : QPolygonF(rect);
            m_lassoPath.clear();
            if (m_tool == Cutter && m_cutterGesture != GestureOff && m_dragLasso && currentDrawing() && isGesture(shape, m_zoom)) {
                // A gesture: a quick straight stroke across lines.
                Drawing edited = *currentDrawing();
                clearSelection();
                if (gestureCut(edited, shape.first(), shape.last(), m_cutterGesture, m_cutterTip))
                    commitDrawing(edited, m_cutterGesture == GestureBreak ? tr("Break Lines") : tr("Cut Lines"));
            } else if (shape.size() >= 3 && (shape.boundingRect().width() * m_zoom > 2 || shape.boundingRect().height() * m_zoom > 2))
                cutInside(shape);
            else
                clearSelection();
        } else if (m_dragLasso) {
            QPolygonF const shape = m_lassoPath;
            m_lassoPath.clear();
            if (shape.size() >= 3 && (shape.boundingRect().width() * m_zoom > 2 || shape.boundingRect().height() * m_zoom > 2))
                selectInside([&shape](QPointF q) { return shape.containsPoint(q, Qt::OddEvenFill); }, m_marqueeAdds);
            else if (!m_marqueeAdds)
                clearSelection();
        } else if (rect.width() * m_zoom > 2 || rect.height() * m_zoom > 2)
            selectInRect(rect, m_marqueeAdds);
    } else if (drag == DragWeights) {
        m_drag = DragNone;
        QVector<SkinDab> const dabs = m_weightDabs;
        m_weightDabs.clear();
        if (!dabs.isEmpty())
            m_document->editLayers({m_layer}, dabs.first().strength < 0 ? tr("Erase Weights") : tr("Paint Weights"),
                                   [dabs](Layer& layer) { layer.skin += dabs; });
    } else if (drag == DragJoint) {
        m_drag = DragNone;
        QVector<QPointF> joints = m_document->scene().layers[m_layer].bones;
        if (m_bone >= 0 && m_bone < joints.size()) {
            joints[m_bone] = m_dragNow;
            m_document->setBones(m_layer, joints, tr("Move Joint"));
        }
    } else if (drag == DragWarpRest) {
        QVector<QPointF> const points = warpRestDragged();
        m_drag = DragNone;
        m_document->setWarp(m_layer, points, m_document->scene().layers[m_layer].warpClosed,
                            m_warpIndex % 3 == 0 ? tr("Move Deformer Point") : tr("Move Deformer Handle"));
    } else if (drag == DragPoseSkew || drag == DragPoseMove || drag == DragPoseScale || drag == DragPoseRotate || drag == DragBone || drag == DragIK || drag == DragWarp || drag == DragPoseTurn) {
        m_drag = DragNone;
        {
            // What was changed of a value that follows another layer does not show: say why.
            Scene const& scene = m_document->scene();
            for (Driver const& d : scene.layers[m_layer].drivers) {
                int const by = scene.layerIndex(d.source);
                if (by >= 0 && std::abs(Driver::read(m_dragPose, d.value) - Driver::read(m_poseStart, d.value)) > 1e-9) {
                    emit message(tr("This layer follows %1 there (Layer Properties > Attribute Controls): the change is kept in its keyframes, "
                                    "but shows only once it no longer follows it.").arg(scene.layers[by].name));
                    break;
                }
            }
        }
        if (!(m_dragPose == m_poseStart) && !m_animate)
            m_document->shiftPoses(m_layer, m_frame, m_poseStart, m_dragPose,
                                   drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone")
                                   : drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer")
                                   : drag == DragPoseTurn ? tr("Turn Layer in Space") : tr("Rotate Layer"),
                                   m_animateRange ? std::max(0, m_frame - m_onionPrev) : -1, m_animateRange ? m_frame + m_onionNext : -1);
        else if (!(m_dragPose == m_poseStart) && m_keyFrameOne && m_frame > 0 && m_document->scene().layers[m_layer].keys.isEmpty()) {
            // Its first keyframe, and not on the first frame: the first frame gets one too, of the layer as it was.
            QString const text = drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer") : drag == DragPoseScale ? tr("Scale Layer") : drag == DragPoseRotate ? tr("Rotate Layer")
                                 : drag == DragPoseTurn ? tr("Turn Layer in Space")
                                 : drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : tr("Turn Bone");
            m_document->beginMacro(text);
            m_document->setKeyframe(m_layer, 0, m_poseStart, text);
            m_document->setKeyframe(m_layer, m_frame, m_dragPose, text);
            m_document->endMacro();
        } else if (!(m_dragPose == m_poseStart))
            m_document->setKeyframe(m_layer, m_frame, m_dragPose,
                                    drag == DragWarp ? tr("Reshape Deformer") : drag == DragIK ? tr("Inverse Kinematics") : drag == DragBone ? tr("Turn Bone") : drag == DragPoseSkew ? tr("Skew Layer") : drag == DragPoseMove ? tr("Move Layer")
                                    : drag == DragPoseScale ? tr("Scale Layer") : drag == DragPoseTurn ? tr("Turn Layer in Space") : tr("Rotate Layer"));
    } else if (drag == DragPivot) {
        m_drag = DragNone;
        m_document->setPivot(m_layer, m_dragPivot);
    } else if (drag == DragQuad) {
        m_drag = DragNone;
        m_document->setQuad(m_layer, m_dragQuad, m_frame);
    } else if (drag == DragMove || drag == DragScale || drag == DragRotate) {
        if (!m_dragTransform.isIdentity()) {
            Drawing const edited = previewDrawing();
            m_drag = DragNone;
            QString const text = drag == DragMove ? tr("Move") : drag == DragScale ? (m_tool == Perspective ? tr("Perspective") : tr("Scale")) : tr("Rotate");
            bool const everywhere = m_applyAll && m_tool == Select;
            QRectF const region = everywhere ? selectionBounds() : QRectF();
            QTransform const done = m_dragTransform;
            bool const all = m_tool == Reposition;
            if (everywhere || all)
                m_document->beginMacro(text);
            commitDrawing(edited, text);
            if (all) {
                repositionOthers(done, text);
                m_document->endMacro();
            }
            if (everywhere) {
                alsoInOtherDrawings(region, &done, text);
                m_document->endMacro();
            }
        }
    }
    m_drag = DragNone;
    m_dragTransform = QTransform();
    m_alignLines.clear();
    if (!m_polyActive)
        m_stroke = Stroke();
    updateCursor();
    update();
}

void Canvas::finishPolyline(bool keep)
{
    if (!m_polyActive)
        return;
    m_polyActive = false;
    Stroke line = m_stroke;
    m_stroke = Stroke();
    stencilLine(line);
    update();
    if (keep && line.points.size() >= 2 && m_layer >= 0 && m_layer < m_document->scene().layers.size()) {
        bool const twin = m_autoColourArt && m_art == LineArt && !bitmapNow() && line.width > 0.0f;
        if (twin)
            m_document->beginMacro(tr("Draw Stroke"));
        keepStroke(line, false);
        if (twin) {
            colourArtTwin(line);
            m_document->endMacro();
        }
    }
}
