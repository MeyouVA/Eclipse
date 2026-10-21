// Eclipse - the Camera view: the pen pressed - what each tool starts.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"

void Canvas::penDown(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers)
{
    // A frame in between the two drawings of a morphing has no drawing of its own to work on: a tool that changes artwork
    // makes the morphing's frames real drawings first, then works on this one - both one step to undo, closed when the
    // pen comes up (or at once when the press starts no drag).
    static QVector<Tool> const looking = {Hand, Zoom, RotateView, Transform, IK, Rigging, Dropper, Morphing, SplineOffset, Turn3D, Reposition};
    Scene const& scene = m_document->scene();
    if (!panning && !m_morphMacro && !looking.contains(m_tool) && m_layer >= 0 && m_layer < scene.layers.size()
        && scene.layers[m_layer].holdsDrawings() && scene.layers[m_layer].morphing(m_frame)) {
        m_document->beginMacro(tr("Draw on Morphing"));
        m_morphMacro = true;
        if (m_document->convertMorph(m_layer, m_frame))
            emit message(tr("The morphing's frames in between were made drawings of their own, to draw on."));
    }
    penDownTool(widgetPos, pressure, panning, modifiers);
    if (m_drag == DragNone)
        closeMorphMacro();
}

void Canvas::closeMorphMacro()
{
    if (m_morphMacro) {
        m_morphMacro = false;
        m_document->endMacro();
    }
}

void Canvas::penDownTool(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers)
{
    if (m_leader)
        followSettings(*m_leader);
    m_lastWidgetPos = widgetPos;
    if (m_bending) {
        // The Line tool's curve, bent where it should be: this click puts it down.
        m_bending = false;
        m_drag = DragNone;
        keepShape();
        update();
        return;
    }
    if (panning || m_spaceDown || m_tool == Hand) {
        m_drag = DragPan;
        updateCursor();
        return;
    }
    if (m_tool == Zoom) {
        zoomAt(widgetPos, modifiers & Qt::AltModifier ? 1.0 / 1.5 : 1.5);
        return;
    }
    if (m_tool == Turn3D) {
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && !m_drawingView && !scene.layers[m_layer].locked) {
            m_poseStart = m_dragPose = scene.layers[m_layer].poseAt(m_frame);
            m_turnFrom = widgetPos;
            m_drag = DragPoseTurn;
        }
        return;
    }
    if (m_tool == SplineOffset) {
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && !scene.layers[m_layer].keys.isEmpty()) {
            m_drag = DragSplineOffset;
            m_splineFrom = m_splineNow = widgetPos;
        } else {
            emit message(tr("Spline Offset moves the motion path of a layer that has keyframes."));
        }
        return;
    }
    if (m_tool == RotateView) {
        m_drag = DragRotateView;
        return;
    }
    if (m_playing)
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    if (m_tool == Dropper) {
        pickColourAt(widgetPos);
        return;
    }
    // A control point of the motion path, taken hold of with the Transform tool: it is picked, and dragged.
    if (m_tool == Transform && m_showControl && !m_drawingView) {
        int key = -1, control = -1;
        if (controlPointAt(widgetPos, &key, &control)) {
            pickControlPoint(key, control);
            m_cpDragPos = m_cpPressPos = widgetPos;
            m_cpMoved = false;
            m_cpDepth = modifiers & Qt::AltModifier;
            m_drag = DragControlPoint;
            return;
        }
    }
    // A layer that shows a symbol is moved about like any other, but there is nothing on it to draw on: its
    // picture is the symbol's.
    if (scene.layers[m_layer].showsSymbol() && m_tool != Transform && m_tool != IK && m_tool != Rigging) {
        emit message(tr("This layer shows a symbol: its artwork is changed in the symbol itself (Edit > Edit Symbol), or after Expand Symbol."));
        return;
    }
    // A point of the guide in use, taken hold of with a drawing tool: it is dragged.
    if (int const point = guideHandleAt(widgetPos); point >= 0) {
        m_guideEdit = scene.guides;
        m_guidePoint = point;
        m_drag = DragGuide;
        return;
    }
    // A plain guide, taken hold of with the Select tool where there is nothing else to pick: it is dragged.
    if (m_tool == Select && handleAt(widgetPos) == NoHandle) {
        if (int const line = lineGuideAt(widgetPos); line >= 0) {
            QPointF const p = toLayer(widgetPos);
            bool taken = hasSelection() && selectionBounds().contains(p);
            if (Drawing const* const d = currentDrawing()) {
                for (Stroke const& stroke : d->strokes)
                    taken = taken || stroke.hit(p, 4.0 / m_zoom);
                for (Fill const& fill : d->fills)
                    taken = taken || fill.contains(p);
            }
            if (!taken) {
                m_guideEdit = scene.guides;
                m_guideLine = line;
                m_drag = DragGuide;
                return;
            }
        }
    }
    beginGuidedStroke(widgetPos);
    if (m_tool == Transform) {
        transformDown(widgetPos);
        update();
        return;
    }
    if (m_tool == IK) {
        ikDown(widgetPos);
        update();
        return;
    }
    if (!scene.layers[m_layer].holdsDrawings()) {
        emit message(scene.layers[m_layer].kind == Layer::Sound
                         ? tr("A sound layer holds no drawings. Select a drawing layer to draw.")
                         : scene.layers[m_layer].kind == Layer::Camera
                         ? tr("The camera holds no drawings. Select a drawing layer to draw, or use the Transform tool to move the camera.")
                         : scene.layers[m_layer].kind == Layer::Peg
                         ? tr("A peg holds no drawings. Select a drawing layer to draw, or use the Transform tool to move the peg.")
                         : tr("This layer holds no drawings. Select a drawing layer to draw."));
        return;
    }
    if (!scene.layers[m_layer].visible)
        return;
    if (scene.layers[m_layer].locked) {
        emit message(tr("This layer is locked. Unlock it in the Timeline to draw on it."));
        return;
    }
    if (scene.layers[m_layer].morphing(m_frame)) {
        emit message(tr("This frame is in a morphing: its drawing is made from the two either side of it. Draw on one of those, or use Animation > Morphing > Convert Morphing to Drawings."));
        return;
    }
    QPointF const p = toLayer(widgetPos);

    if (m_tool == EditGradient) {
        gradientDown(widgetPos, p);
        return;
    }
    if (m_tool == Stamp) {
        stampAt(p);
        return;
    }
    if (m_tool == Morphing) {
        hintDown(widgetPos, p, modifiers);
        return;
    }
    if (m_tool == PencilEditor) {
        thicknessDown(p, modifiers);
        return;
    }
    if (m_tool == PencilRetouch) {
        if (Drawing const* const d = currentDrawing()) {
            m_eraseArt = *d;
            m_inked = false;
            m_drag = DragRetouch;
            m_dragNow = p;
            // Alt does the other of the pair: thinner for thicker, fainter for more solid, and back.
            m_retouchNow = modifiers & Qt::AltModifier ? (m_retouchMode ^ 1) : m_retouchMode;
            m_singleNow = m_singleStroke != bool(modifiers & Qt::ShiftModifier);
            m_onlyLine = -1;
            retouchAt(p, m_retouchNow);
        }
        return;
    }
    if (m_tool == SmoothEditor) {
        if (Drawing const* const d = currentDrawing()) {
            m_eraseArt = *d;
            m_inked = false;
            m_drag = DragSmooth;
            m_singleNow = m_singleStroke != bool(modifiers & Qt::ShiftModifier);
            m_onlyLine = -1;
            smoothAt(p);
        }
        return;
    }
    if (m_tool == RepaintBrush) {
        if (Drawing const* const d = currentDrawing()) {
            m_repaintFrom = *d;
            m_eraseArt = *d;
            m_repaintPath = Stroke();
            m_repaintPath.width = float(std::max(m_brushWidth, 1.0));
            m_inked = false;
            m_drag = DragRepaint;
            m_dragNow = p;
            repaintTo(p, -1.0);
        }
        return;
    }
    if (m_tool == Ink) {
        if (Drawing const* const d = currentDrawing()) {
            m_eraseArt = *d;
            m_inked = false;
            m_drag = DragInk;
            inkAt(p);
        }
        return;
    }
    float const raw = pressure;     // (how hard the pen pressed: the Pencil's opacity, smoothing with pressure)
    if (m_tool == StrokeTool || (m_tool == Pencil && m_pencilMinSize >= 100))
        pressure = 1.0f;  // a pencil line has one thickness (unless it is to follow the pen: a Minimum Size)
    if (m_tool == Rigging) {
        riggingDown(widgetPos, p, modifiers);
    } else if (m_tool == ContourEditor) {
        contourDown(p, modifiers);
        emit pointPicked(pointThickness());
    } else if (m_tool == Text) {
        endText(true);
        // On words written before: they are taken up again, in the font they have.
        int words = -1;
        if (Drawing const* const d = currentDrawing())
            for (int i = int(d->fills.size()) - 1; i >= 0 && words < 0; --i)
                if (!d->fills[i].text.isEmpty() && d->fills[i].bounds().contains(p))
                    words = i;
        if (words >= 0) {
            Fill const& fill = currentDrawing()->fills[words];
            QFont font;
            if (font.fromString(fill.textFont)) {
                m_textFont = font;
                emit textFontPicked(font);
            }
            m_textAlign = fill.textAlign;
            m_textSpacing = int(std::lround(fill.textSpacing * 100.0));
            m_textKerning = fill.textKerning;
            emit textLayoutPicked(m_textAlign, m_textSpacing, m_textKerning);
            beginText(layerViewTransform().map(fill.textAt), fill.textAt, fill.text);
            m_textEdit = words;
        } else {
            beginText(widgetPos, p);
        }
    } else if (m_tool == Polyline) {
        if (!m_polyActive) {
            m_polyActive = true;
            m_stroke = Stroke();
            m_stroke.swatch = m_swatch;
            m_stroke.color = scene.resolve(m_swatch, Qt::black);
            m_stroke.width = float(m_brushWidth);
            m_stroke.join = m_join;
            m_stroke.capStart = m_capStart;
            m_stroke.capEnd = m_capEnd;
            textureNewLine(m_stroke);
            setMouseTracking(true);
        } else if (m_stroke.points.size() >= 3 && QLineF(layerViewTransform().map(m_stroke.points.first().pos), widgetPos).length() <= 7) {
            // Back on the first point: the line closes into a shape.
            m_stroke.closed = true;
            finishPolyline(true);
            update();
            return;
        }
        // A click on the point just made adds nothing (the second click of a double click).
        if (m_stroke.points.isEmpty() || QLineF(layerViewTransform().map(m_stroke.points.last().pos), widgetPos).length() > 2) {
            // (With a guide in use, each piece runs one of its ways from the point before.)
            StrokePoint point{endMagnet(snapped(m_stroke.points.isEmpty() ? p : toLayer(guidedFrom(layerViewTransform().map(m_stroke.points.last().pos), widgetPos)))), 1.0f};
            point.corner = m_stroke.curve;      // on a line that bends elsewhere, a point clicked is a corner
            m_stroke.points.push_back(point);
            m_drag = DragPolyHandle;            // dragged on from here, the line bends through it
        }
        m_polyHover = m_stroke.points.isEmpty() ? snapped(p) : m_stroke.points.last().pos;
    } else if (m_tool == Line || m_tool == Rectangle || m_tool == Ellipse || m_tool == CloseGap) {
        m_drag = DragShape;
        m_dragStart = m_tool == Line || m_tool == CloseGap ? endMagnet(snapped(p)) : snapped(p);
        m_stroke = Stroke();
        m_stroke.swatch = m_swatch;
        m_stroke.color = scene.resolve(m_swatch, Qt::black);
        m_stroke.width = m_tool == CloseGap ? 0.0f : float(m_brushWidth);
        m_stroke.join = m_join;
        m_stroke.capStart = m_capStart;
        m_stroke.capEnd = m_capEnd;
        textureNewLine(m_stroke);
        updateShape(snapped(p), modifiers);
    } else if (m_tool == Brush || m_tool == Pencil || m_tool == StrokeTool) {
        m_drag = DragDraw;
        m_stroke = Stroke();
        m_stroke.swatch = m_swatch;
        m_stroke.color = scene.resolve(m_swatch, Qt::black);
        m_stroke.width = m_tool == StrokeTool ? 0.0f : float(m_brushWidth);
        m_stroke.join = m_join;
        m_stroke.capStart = m_capStart;
        m_stroke.capEnd = m_capEnd;
        textureNewLine(m_stroke);
        StrokePoint first{p, pressure};
        if (m_tool == Pencil && m_pencilMinOpacity < 100)
            first.opacity = float((m_pencilMinOpacity + (100 - m_pencilMinOpacity) * std::clamp(raw, 0.0f, 1.0f)) / 100.0);
        m_stroke.points.push_back(first);
        m_rawPressure = {raw};
        m_stabiliserLine = m_stabiliserPen = p;
    } else if (m_tool == Eraser) {
        m_drag = DragErase;
        if (m_eraserCuts || bitmapNow()) {
            // What it passes over is cut out as it goes, in a drawing of its own; the scene
            // gets the result, as one step, when it lifts.
            m_stroke = Stroke();
            m_stroke.width = float(m_brushWidth);
            m_stroke.points.push_back({p, pressure});      // (narrower where the pen presses less, with a Minimum Size)
            Drawing const* const under = currentDrawing();
            m_eraseArt = under ? *under : Drawing();
            m_eraseAny = false;
            eraseStretch(0);
            update();
        } else {
            eraseArtAt(p, m_brushWidth / 2.0 * pressure);
        }
    } else if ((m_tool == Paint || m_tool == Unpaint) && bitmapNow()) {
        // On a bitmap layer the bucket fills pixels.
        fillBitmapAt(p, m_tool == Unpaint);
    } else if (isPaintTool(m_tool)) {
        // A click paints what is under it (when the pen lifts); a drag draws round things to paint them all.
        m_drag = DragPaint;
        m_dragLasso = m_lasso != bool(modifiers & Qt::AltModifier);
        m_dragStart = m_dragNow = p;
        m_lassoPath = QPolygonF({p});
    } else if (m_tool == Envelope && envelopeDown(widgetPos)) {
        // A point of the outline round the selection is in hand.
    } else if (selecting()) {
        if (m_tool == Reposition)
            selectAll();        // whatever drawing this is: all of it
        m_dragStart = m_dragNow = p;
        m_dragTransform = QTransform();
        m_dragBounds = selectionBounds();
        m_dragHandle = handleAt(widgetPos);
        bool const shift = modifiers & Qt::ShiftModifier;
        if (m_dragHandle == Rotate) {
            m_drag = DragRotate;
        } else if (m_dragHandle != NoHandle) {
            m_drag = DragScale;
        } else {
            // Press inside the selection drags it.  Otherwise press on an item selects it
            // (and drags it); on empty space, start a marquee.  Shift adds or removes.
            if (m_tool == Reposition) {
                m_drag = hasSelection() ? DragMove : DragNone;      // pressed anywhere, it all moves
            } else if (hasSelection() && !shift && m_dragBounds.contains(p)) {
                m_drag = DragMove;
            } else if (int const piece = m_tool == Select && bitmapNow() ? liftedPieceAt(p) : -1; piece >= 0) {
                // On a bitmap layer: a piece of its pixels cut loose before is picked up again (the rest of the pixels
                // are not taken whole: a drag there picks pixels out instead).
                setSelection({}, {}, {piece});
                m_dragBounds = selectionBounds();
                m_drag = DragMove;
            } else if (m_tool != Cutter && !(m_tool == Select && bitmapNow()) && selectAt(p, shift)) {
                m_dragBounds = selectionBounds();
                m_drag = shift || !hasSelection() ? DragNone : DragMove;
            } else {
                m_drag = DragMarquee;
                m_dragLasso = m_lasso != bool(modifiers & Qt::AltModifier);
                m_marqueeAdds = shift;
                m_lassoPath = QPolygonF({p});
            }
        }
    }
    update();
}

void Canvas::setLeader(Canvas* leader)
{
    m_leader = leader;
    if (leader) {
        followSettings(*leader);
        connect(leader, &Canvas::toolChanged, this, [this](int tool) {
            if (m_leader)
                setTool(Tool(tool));
        });
    }
}

void Canvas::followSettings(Canvas const& leader)
{
    if (m_tool != leader.m_tool)
        setTool(leader.m_tool);
    m_swatch = leader.m_swatch;
    m_brushWidth = leader.m_brushWidth;
    m_bitmapHardness = leader.m_bitmapHardness;
    m_bitmapFlow = leader.m_bitmapFlow;
    m_bitmapTip = leader.m_bitmapTip;
    m_smoothing = leader.m_smoothing;
    m_minSize = leader.m_minSize;
    m_cornerAngle = leader.m_cornerAngle;
    m_join = leader.m_join;
    m_capStart = leader.m_capStart;
    m_capEnd = leader.m_capEnd;
    m_lineTexture = leader.m_lineTexture;
    m_lineTextureAmount = leader.m_lineTextureAmount;
    m_lineTextureCrisp = leader.m_lineTextureCrisp;
    m_lineRampLow = leader.m_lineRampLow;
    m_lineRampHigh = leader.m_lineRampHigh;
    m_brushTexture = leader.m_brushTexture;
    m_brushTextureAmount = leader.m_brushTextureAmount;
    m_brushTextureCrisp = leader.m_brushTextureCrisp;
    m_brushRampLow = leader.m_brushRampLow;
    m_brushRampHigh = leader.m_brushRampHigh;
    m_autoFlatten = leader.m_autoFlatten;
    m_eraserCuts = leader.m_eraserCuts;
    m_gapSize = leader.m_gapSize;
    m_paintAll = leader.m_paintAll;
    m_bothArts = leader.m_bothArts;
    m_lasso = leader.m_lasso;
    m_dropperAllLayers = leader.m_dropperAllLayers;
    m_drawBehind = leader.m_drawBehind;
    m_polyBehind = leader.m_polyBehind;
    m_pencilMode = leader.m_pencilMode;
    m_pushHighlight = leader.m_pushHighlight;
    m_snapping = leader.m_snapping;
    m_cutterGesture = leader.m_cutterGesture;
    m_cutterTip = leader.m_cutterTip;
    m_hideManipulator = leader.m_hideManipulator;
    m_ceControls = leader.m_ceControls;
    m_envAdvanced = leader.m_envAdvanced;
    m_eraserMinSize = leader.m_eraserMinSize;
    m_pencilMinSize = leader.m_pencilMinSize;
    for (int slot = 0; slot < 3; ++slot) {
        m_stencils[slot] = leader.m_stencils[slot];
        m_customStencils[slot] = leader.m_customStencils[slot];
    }
    m_pressureSmoothing = leader.m_pressureSmoothing;
    m_minSmoothing = leader.m_minSmoothing;
    m_pencilMinOpacity = leader.m_pencilMinOpacity;
    m_opacitySmoothing = leader.m_opacitySmoothing;
    m_singleStroke = leader.m_singleStroke;
    m_hardEdges = leader.m_hardEdges;
    m_overlayBrush = leader.m_overlayBrush;
    m_maskLayers = leader.m_maskLayers;
    m_brushShapes = leader.m_brushShapes;
    m_textFont = leader.m_textFont;
    m_textAlign = leader.m_textAlign;
    m_textSpacing = leader.m_textSpacing;
    m_textKerning = leader.m_textKerning;
    m_animate = leader.m_animate;
    m_animateRange = leader.m_animateRange;
    m_keyFrameOne = leader.m_keyFrameOne;
    m_nudgeStep = leader.m_nudgeStep;
    if (m_art != leader.m_art)
        setArt(leader.m_art);
}
