// Eclipse - the Camera view: the pen lifted - a line kept, a shape kept, paint under a closed line.
#include "canvas/Canvas.h"
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
    if (drag == DragZoom) {
        zoomToolUp();
        return;
    }
    if (drag == DragTrace) {
        traceUp();
        return;
    }
    if (drag == DragLayerPick) {
        penUpLayerPick();
        return;
    }
    if (penUpTool(drag))
        return;
    if (drag == DragDraw) {
        if (penUpDraw(drag))
            return;
    } else if (drag == DragShape) {
        if (penUpShape(drag))
            return;
    } else if (drag == DragSegment) {
        if (penUpSegment(drag))
            return;
    } else if (drag == DragCeBox) {
        if (penUpContourBox(drag))
            return;
    } else if (drag == DragPoint) {
        if (penUpPoint(drag))
            return;
    } else if (drag == DragHandle && m_ceHandle == 2) {
        if (penUpBothHandles(drag))
            return;
    } else if (drag == DragHandle) {
        if (penUpHandle(drag))
            return;
    } else if (drag == DragPaint) {
        if (penUpPaint(drag))
            return;
    } else if (drag == DragPointMarquee) {
        if (penUpPointMarquee(drag))
            return;
    } else if (drag == DragMarquee) {
        if (penUpMarquee(drag))
            return;
    } else if (drag == DragWeights) {
        if (penUpWeights(drag))
            return;
    } else if (drag == DragJoint) {
        if (penUpJoint(drag))
            return;
    } else if (drag == DragWarpRest) {
        if (penUpWarpRest(drag))
            return;
    } else if (drag == DragPoseSkew || drag == DragPoseMove || drag == DragPoseScale || drag == DragPoseRotate || drag == DragBone || drag == DragIK || drag == DragWarp || drag == DragPoseTurn) {
        if (penUpPose(drag))
            return;
    } else if (drag == DragPivot) {
        if (penUpPivot(drag))
            return;
    } else if (drag == DragQuad) {
        if (penUpQuad(drag))
            return;
    } else if (drag == DragMove || drag == DragScale || drag == DragRotate) {
        if (penUpSelection(drag))
            return;
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
