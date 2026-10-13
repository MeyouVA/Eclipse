// Eclipse - the drawing view: shows the current frame and takes pen / mouse input.
#pragma once

#include "EclipseExport.h"

#include <QTransform>
#include <QFont>
#include <QWidget>

#include <functional>

#include "model/scene/Scene.h"

class Document;

class ECLIPSE_UI_API Canvas : public QWidget {
    Q_OBJECT

public:
    enum Tool {
        Select, Transform, Brush, Eraser, Paint, Hand, Pencil, Line, Rectangle, Ellipse, ContourEditor, Rigging, IK,
        // Takes the colour of what is clicked on.
        Dropper,
        // Click to look closer, Alt-click to draw back.
        Zoom,
        // A line of straight pieces: a click for each point, a double click (or Enter) for the last.
        Polyline,
        // The paint bucket's other ways: only areas that have no paint yet; only what is
        // painted already; and taking paint away.
        PaintUnpainted, Repaint, Unpaint,
        // Click, type, Enter: the words become painted shapes of the current colour.
        Text,
        // Drag to turn the view about its middle.
        RotateView,
        // Draw round a part of the artwork: it is cut loose from the rest, and selected,
        // to be moved, turned, scaled or deleted like anything selected.
        Cutter,
        // Lines with no thickness: they are not seen in the picture, but paint stops at them.
        // The Stroke tool draws one freehand; Close Gap draws a straight one across a gap.
        StrokeTool, CloseGap, EditGradient,
        // Everything in the drawing is in hand at once; moved, scaled or turned, it is so in
        // every drawing of the layer - all of each drawing, on every one of its sheets.
        Reposition,
        // A click puts a copy of the stamp there: a star to begin with, or artwork of your own
        // (Drawing > Create Stamp from Selection).
        Stamp,
        // Press on a point of a pencil line and pull away from it or towards it: the line is
        // that much thicker or thinner there.
        PencilEditor,
        // What is selected is put in perspective: each corner of its box is pulled on its own.
        Perspective,
        // The lines the pen passes over take the colour in hand; paint is left alone.
        Ink,
        // Rubbed over a line or the edge of a brush shape, it smooths it there: the points
        // under the pen settle between their neighbours.
        SmoothEditor,
        // What is selected has an outline of points round it: a point pulled, the artwork inside is
        // pulled about with it.
        Envelope,
        // Morphing hints: on the first drawing of a morphing a click puts a hint down; on the second it is
        // dragged to where that place is to go.  Alt and a click takes one away.
        Morphing,
    };
    // True for the paint bucket in any of its ways.
    static bool isPaintTool(Tool tool) { return tool == Paint || tool == PaintUnpainted || tool == Repaint || tool == Unpaint; }

    explicit Canvas(Document* document, QWidget* parent = nullptr);

    // Which sheet of the drawing the tools work on.
    enum Art { LineArt, ColourArt, OverlayArt, UnderlayArt };
    Art art() const { return m_art; }
    // The hints of the morphing shown on this frame with the Morphing tool, where they are in the view.
    QVector<QPointF> morphHintsInView() const;
    void setArt(Art art);
    bool previewsAllArt() const { return m_previewAllArt; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    // The palette swatch new strokes and fills are linked to.
    void setSwatch(int id)
    {
        m_swatch = id;
        if (m_highlightColour)
            update();
    }
    int swatch() const { return m_swatch; }
    void setBrushWidth(double width) { m_brushWidth = width; }
    double brushWidth() const { return m_brushWidth; }
    // How much a drawn line is smoothed when the pen lifts: 0 keeps every wobble, 100
    // irons out a lot.
    void setSmoothing(int percent) { m_smoothing = percent; }
    // The Brush: how thick its line is where the pen barely touches, as a percentage of its size
    // (pressing harder goes from there up to the full size).
    int minimumBrushSize() const { return m_minSize; }
    void setMinimumBrushSize(int percent) { m_minSize = percent; }
    // What the tool in hand makes of how hard the pen is pressed (0 to 1).
    float pressureFor(float raw) const
    {
        float const least = m_tool == Brush ? float(m_minSize) / 100.0f : 0.0f;
        return least + (1.0f - least) * raw;
    }
    // Turns sharper than this stay corners; gentler ones are rounded.
    void setCornerAngle(int degrees) { m_cornerAngle = degrees; }
    // How new lines are finished at corners and ends.  Lines that are selected (or shown
    // in the contour editor) are changed too.
    void setJoinStyle(int join);
    void setStartCap(int cap);
    void setEndCap(int cap);
    void setOnionSkin(bool on);
    bool onionSkin() const { return m_onionSkin; }
    // The Drawing view: the drawing of the current cell alone and flat - not moved by its
    // layer, its pegs or the camera - on white paper.  The other layers show, faint and as
    // flat, only with the light table on.
    bool drawingView() const { return m_drawingView; }
    void setDrawingView(bool on);
    int gridKind() const { return m_gridKind; }
    bool backlight() const { return m_backlight; }
    // While playing, nothing but the frame itself is shown (no onion skin, no editing).
    void setPlaying(bool playing);

    void fitToWindow();
    // Where a point of the scene is in the widget.
    QPointF mapFromScene(QPointF scenePos) const { return viewTransform().map(scenePos); }
    QPointF mapToScene(QPointF widgetPos) const { return toScene(widgetPos); }

    // ---- selection (Select tool) --------------------------------------------------------
    bool hasSelection() const { return !m_selStrokes.isEmpty() || !m_selFills.isEmpty() || !m_selPictures.isEmpty(); }
    // How many lines and painted areas are selected.
    int selectionCount() const { return int(m_selStrokes.size() + m_selFills.size() + m_selPictures.size()); }
    // How many of the things selected are pictures (imported bitmaps: picked with the Select tool on the line art).
    int selectedPictureCount() const { return int(m_selPictures.size()); }
    // The Select tool or the Cutter: what is selected can be handled with either.
    bool selecting() const { return m_tool == Select || m_tool == Cutter || m_tool == Reposition || m_tool == Perspective || m_tool == Envelope; }
    // The points picked with the Contour Editor besides contourPoint(), on the same curve.
    QVector<int> contourPointsPicked() const;
    void clearSelection();
    // The point picked with the Contour Editor: which line and which point of it (-1: none);
    // and where its two handles are, in the coordinates of the view.
    int contourStroke() const { return m_ceStroke; }
    int contourPoint() const { return m_cePoint; }
    QPointF contourHandlePos(int handle) const;
    // How the Select tool takes things when dragged over empty space: round a shape drawn
    // freehand (lasso), or in a rectangle (marquee).
    // What the Brush tool makes: a contour shape (a filled area with an editable
    // outline), or - when off - a centre line with a thickness, as the Pencil does.
    // Auto Flatten: a stroke of the brush joins the shapes of its own colour that it
    // overlaps (one shape, with points where they met), and cuts itself out of shapes of
    // other colours under it.
    bool autoFlatten() const { return m_autoFlatten; }
    void setAutoFlatten(bool on) { m_autoFlatten = on; }
    // The eraser cuts the part it passes over out of lines and painted areas;
    // off, it takes away every whole line it touches.
    bool eraserCuts() const { return m_eraserCuts; }
    void setEraserCuts(bool on) { m_eraserCuts = on; }
    // Draw Behind: what the brush draws goes under the artwork already in the drawing.
    bool drawBehind() const { return m_drawBehind; }
    // Clean Up.  Specks - lines and painted shapes no bigger than `size` either way - are taken
    // out of the drawing (of what is selected, when something is); and open lines shorter than
    // `length`.  Each says how many went.
    int removeDirt(double size);
    // Open lines whose end lies within `gap` of another line (or of their own other end) are
    // joined to it by a line with no thickness; says how many were made.
    int closeGaps(double gap);
    // Generate Auto-Matte: a painted shape in the colour in hand covering all that the line art goes
    // round, `radius` further out all round, at the back of the colour art.  False when there is nothing to cover.
    bool autoMatte(double radius, int source = Drawing::LineSheet, int destination = Drawing::ColourSheet);
    // The Select tool: what is selected stays selected on another frame - whatever lies in the same place there.
    // The paint tools: what is painted, repainted or unpainted here is so in the same place in every drawing of the layer.
    void setPaintAllDrawings(bool on) { m_paintAll = on; }
    // The eraser rubs out of the line art and the colour art at once.
    void setEraserBothArts(bool on) { m_bothArts = on; }
    bool eraserBothArts() const { return m_bothArts; }
    // The Envelope tool: how many points the outline round the selection has.
    int envelopeToolPoints() const { return m_envCount; }
    void setEnvelopeToolPoints(int count)
    {
        m_envCount = count;
        update();
    }
    // ... and how wide a gap in the lines round an area they close of themselves (pixels of the drawing).
    double closeGapSize() const { return m_gapSize; }
    void setCloseGapSize(double pixels) { m_gapSize = pixels; }
    bool paintsAllDrawings() const { return m_paintAll; }
    // Apply to All Layers: the paint tools work on every drawing layer that is seen and not locked, in the same
    // place of the picture, as one step.
    void setPaintAllLayers(bool on) { m_paintLayers = on; }
    bool paintsAllLayers() const { return m_paintLayers; }
    void setPermanentSelection(bool on) { m_permanentSelection = on; }
    bool permanentSelection() const { return m_permanentSelection; }
    // The Stamp tool: how big the stamp comes out (per cent), and whether it is one of the user's own.
    int stampScale() const { return m_stampScale; }
    void setStampScale(int percent)
    {
        m_stampScale = percent;
        update();
    }
    bool hasOwnStamp() const { return m_stampOwn; }
    // A stamp may hold several drawings: each click puts down the next of them (or any one, at random).
    int stampDrawingCount() const { return m_stampOwn ? int(std::max<qsizetype>(m_stampSet.size(), 1)) : 1; }
    int stampTurn() const { return m_stampTurn; }
    bool stampRandom() const { return m_stampRandom; }
    void setStampRandom(bool on) { m_stampRandom = on; }
    // Makes a stamp of these drawings (each kept about its own middle), and takes it in hand.
    void createStampFrom(QVector<Drawing> const& drawings);
    void clearStamp();
    // The stamp of the user's own is kept between sessions: taken up again when the program starts.
    void loadStamp();
    // The stamps made so far are kept in a list: their names, which is in hand (-1: the star),
    // taking up another, and taking one off the list.
    QStringList stampNames() const;
    int currentStamp() const;
    void chooseStamp(int index);
    void removeStamp(int index);
    // Whether the paint tools are to leave what has this colour alone: it is protected, and
    // protection is being respected.
    bool keepsColour(int swatch) const;
    bool respectsProtected() const { return m_respectProtected; }
    bool proportionGrid() const { return m_proportionGrid; }
    // From Preferences: how far an arrow key nudges what is selected (pixels); how many parts the
    // proportion grid has each way; how much of the frame, at each edge, lies outside the safe area.
    double nudgeStep() const { return m_nudgeStep; }
    void setNudgeStep(double pixels) { m_nudgeStep = pixels; }
    int proportionAcross() const { return m_propAcross; }
    int proportionDown() const { return m_propDown; }
    void setProportionParts(int across, int down)
    {
        m_propAcross = across;
        m_propDown = down;
        update();
    }
    double safeMargin() const { return m_safeMargin; }
    void setSafeMargin(double part)
    {
        m_safeMargin = part;
        update();
    }
    int removeHair(double length);
    // The dropper looks at every layer, or only at the one being worked on.
    bool dropperAllLayers() const { return m_dropperAllLayers; }
    void setDropperAllLayers(bool on) { m_dropperAllLayers = on; }
    // Everything that is not of the colour in hand shown faint.
    bool highlightColour() const { return m_highlightColour; }
    // What the Rigging tool lays down: a chain of bones, a curve the drawing bends along, or
    // an envelope round it.
    enum RigMode { RigBones, RigCurve, RigEnvelope };
    RigMode rigMode() const { return m_rigMode; }
    void setRigMode(RigMode mode) { m_rigMode = mode; update(); }
    void setDrawBehind(bool on) { m_drawBehind = on; }
    bool brushShapes() const { return m_brushShapes; }
    void setBrushShapes(bool on) { m_brushShapes = on; }
    // The contour of a brush shape picked with the Contour Editor: which painted area (-1:
    // none, or a line is picked instead).
    int contourFill() const { return m_ceFill; }
    bool lasso() const { return m_lasso; }
    bool snapsToContour() const { return m_snapContour; }
    // Guides (Scene::guides): seen and snapped to, or not.
    bool showsGuides() const { return m_showGuides; }
    bool guidesLocked() const { return m_guidesLocked; }
    bool selectsByColour() const { return m_selectByColour; }
    bool snapsAndAligns() const { return m_snapAlign; }
    // Where a plain guide (a place in Scene::guides) crosses the view, level with `widgetPos`: to take hold of it.
    QPointF guideLineInView(int guide, QPointF widgetPos) const;
    // The guide in use (a place in Scene::guides, or -1): one to draw along is shown, its
    // points can be dragged, and the brush and pencil are kept to the ways it runs.
    int activeGuide() const { return m_activeGuide; }
    void setActiveGuide(int index);
    // Where a point of the guide in use is in the view (a null point when there is none).
    QPointF guidePointInView(int point) const;
    // The preview: frames of the scene rendered beforehand (the Preview Manager), shown
    // while playing in place of drawing every layer again.
    void setPreview(QVector<QImage> const& frames);
    int previewFrames() const { return int(m_preview.size()); }
    QSize previewSize() const { return m_preview.isEmpty() ? QSize() : m_preview.first().size(); }
    bool usesPreview() const { return m_usePreview; }
    bool snapsToGrid() const { return m_snapGrid; }
    bool animates() const { return m_animate; }
    // The first time the Transform tool moves a layer that has no keyframes, on a frame after the
    // first: with this on, the first frame gets a keyframe too, of the layer as it was - so the
    // layer goes from there to where it was put, instead of being there all along.
    // The Transform tool doing one thing only, wherever the layer is taken hold of: moving it,
    // turning it, or scaling it (the Translate, Rotate and Scale tools).  TransformAll: by its handles, as usual.
    // (MaintainSize: dragged up or down the layer goes away from the camera or towards it, and is
    // scaled so that it looks as big as before.)
    enum TransformMode { TransformAll, TranslateOnly, RotateOnly, ScaleOnly, SkewOnly, MaintainSize };
    // No Z Dragging: Alt with the Transform tool does not move a layer nearer or further.
    bool noZDragging() const { return m_noZDrag; }
    Q_INVOKABLE void onActionNoZDragging(bool on) { m_noZDrag = on; }
    int transformMode() const { return m_transformMode; }
    void setTransformMode(int mode)
    {
        m_transformMode = mode;
        update();
    }
    bool keysFrameOne() const { return m_keyFrameOne; }
    void setKeysFrameOne(bool on) { m_keyFrameOne = on; }
    void setLasso(bool on);
    // The box round what is selected, in the coordinates of the drawing; null with nothing selected.
    QRectF selectionRect() const { return hasSelection() ? selectionBounds() : QRectF(); }
    // Moves and stretches what is selected so that it fills this box.
    void setSelectionRect(QRectF const& box);
    // Turns what is selected about its middle, clockwise.
    void rotateSelection(double degrees);
    void copySelection();
    void cutSelection();
    // Zooms about the middle of the view.
    void zoomBy(double factor);
    // The same, keeping the point of the view under `anchor` where it is.
    void zoomAt(QPointF anchor, double factor);
    double zoom() const { return m_zoom; }
    double viewRotation() const { return m_viewRotation; }
    bool viewMirrored() const { return m_mirror; }
    void rotateView(double degrees);
    void setOnionRange(int previous, int next);
    int onionPrevious() const { return m_onionPrev; }
    int onionNext() const { return m_onionNext; }
    // How strongly the onion skin shows, 5 to 100 per cent.
    void setOnionOpacity(int percent);
    int onionOpacity() const { return m_onionOpacity; }
    // Each of the drawings shown before and after has a strength of its own, 0 to 100 per cent of
    // what it would show at: `step` 0 is the nearest drawing, 5 the furthest off.
    static constexpr int kOnionSteps = 6;
    void setOnionStrength(bool after, int step, int percent);
    int onionStrength(bool after, int step) const { return step >= 0 && step < kOnionSteps ? m_onionStrength[after ? 1 : 0][step] : 0; }
    bool showsControl() const { return m_showControl; }
    // The points of the polyline being laid down (empty when there is none).
    int polylinePoints() const { return m_polyActive ? int(m_stroke.points.size()) : 0; }
    // Ends the polyline being laid down: kept (when it has two points), or thrown away.
    void finishPolyline(bool keep);

    // The font the Text tool writes in.  Its pixel size is the height of the letters in
    // the units of the scene.
    void setTextFont(QFont const& font) { m_textFont = font; }
    QFont textFont() const { return m_textFont; }
    // How the lines of words stand to the place clicked (0 left, 1 centred, 2 right) and how
    // far apart they are, as a percentage of what the font gives.  In the entry, Shift+Enter
    // starts a new line (shown as a bent arrow).
    void setTextAlign(int align) { m_textAlign = std::clamp(align, 0, 3); }     // 3: justified
    int textAlign() const { return m_textAlign; }
    void setTextLineSpacing(int percent) { m_textSpacing = std::clamp(percent, 20, 1000); }
    int textLineSpacing() const { return m_textSpacing; }
    // And how far apart the letters are, likewise.
    void setTextKerning(int percent) { m_textKerning = std::clamp(percent, 50, 400); }
    int textKerning() const { return m_textKerning; }
    static constexpr char16_t kLineBreak = 0x21B5;
    // The Select tool: what is moved, scaled, turned or deleted is so in every drawing of
    // the layer - in the others, whatever lies within the place the selection had.
    void setApplyToAllDrawings(bool on) { m_applyAll = on; }
    bool appliesToAllDrawings() const { return m_applyAll; }
    // Onion skin by frame: the frames just before and after, instead of the neighbouring drawings.
    void setOnionByFrame(bool on);
    // The drawings before and after as outlines only: the edges of their paint and the paths of their lines.
    void setOnionOutlines(bool on);
    // Only the drawings marked so (Drawing::Mark) are shown before and after; NoMark: all of them.
    void setOnionMark(int mark);
    int onionMark() const { return m_onionMark; }
    bool onionOutlines() const { return m_onionOutlines; }
    bool onionByFrame() const { return m_onionByFrame; }
    void setOnionColours(QColor const& before, QColor const& after);
    QColor onionBefore() const { return m_onionBefore; }
    QColor onionAfter() const { return m_onionAfter; }
    // A colour of its own for one of the drawings shown (the first before, the second after, ...).  An invalid
    // colour takes it away again: that drawing then shows in the colour of its side.
    void setOnionStepColour(bool after, int step, QColor const& colour);
    QColor onionStepColour(bool after, int step) const;
    bool onionStepHasColour(bool after, int step) const { return step >= 0 && step < kOnionSteps && m_onionStepColour[after ? 1 : 0][step].isValid(); }
    // Writes `text` into the current drawing, its first line standing on `layerPos`.
    // The letters are painted shapes from then on: they are not typed text any more.
    void addText(QPointF layerPos, QString const& text);
    // The words being changed with the Text tool: which painted area of the drawing (-1: new words).
    int textEdited() const { return m_textEdit; }
    // The Edit Gradient tool: the area whose gradient is in hand (its place among the
    // painted areas of the art being worked on), or -1.
    int gradientPicked() const { return gradientValid() ? m_gradFill : -1; }

    // The thickness of the point picked with the Contour Editor, as a percentage of the
    // width of its line; -1 when no point is picked.
    int pointThickness() const;
    void setPointThickness(int percent);
    static bool canPaste();
    // The sheet of a drawing an art layer of the view stands for (Drawing::Sheet).
    static int sheetOf(Art art);

    // Renders one frame of a scene the way the canvas shows it, without the editing aids.
    static void paintFrame(QPainter& painter, Scene const& scene, int frame);
    static void paintDrawing(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity,
                             QColor const* tint = nullptr);
    // The drawing of a layer with the effects of the layer applied.  The painter is set up
    // for the coordinates of the layer; `sceneToDevice` is what it would be for those of
    // the scene (the matte of a cutter is another layer, found from there).
    static void paintLayerDrawing(QPainter& painter, Scene const& scene, int layer, int frame, Drawing const& drawing,
                                  double opacity, QTransform const& sceneToDevice);
    static void paintStroke(QPainter& painter, Stroke const& stroke, QBrush const& color);
    static void paintFill(QPainter& painter, Fill const& fill, QBrush const& color);

public slots:
    void setCurrentCell(int layer, int frame);

    // ---- what the menus call (this view is the "camera" responder) -------------------
    void cut() { cutSelection(); }
    void copy() { copySelection(); }
    void paste();
    void deleteSelection();
    void selectAll();
    void deselectAll() { clearSelection(); }
    void onActionZoomIn() { zoomBy(1.25); }
    void onActionZoomOut() { zoomBy(0.8); }
    void onActionResetView() { fitToWindow(); }
    void onActionToggleOnionSkin(bool on) { setOnionSkin(on); }
    // The view turned on the screen (the scene is not changed), seen as in a mirror, and
    // each of the three put back on its own.
    void onActionRotateViewCW() { rotateView(30.0); }
    void onActionRotateViewCCW() { rotateView(-30.0); }
    void onActionMirrorView(bool on);
    void onActionResetRotation() { rotateView(-m_viewRotation); }
    void onActionResetZoom() { zoomBy(1.0 / m_zoom); }
    void onActionResetPan();
    // Aids drawn over the picture: a field grid, the safe areas; and the layers that are
    // not being drawn on shown faintly.
    void onActionShowGrid(bool on);
    // Which grid: 0 twelve fields, 1 sixteen fields, 2 sixteen across by twelve down, 3 squares.
    void onActionGridKind(int kind);
    void onActionGridOutlineOnly(bool on);
    // Behind the drawings instead of over them.
    void onActionGridUnderlay(bool on);
    // Everything drawn shown as a dark shape, to judge its outline.
    void onActionBacklight(bool on);
    // Layers that are locked shown as outlines, so that they do not hide what is worked on.
    void onActionOutlineLocked(bool on);
    void onActionShowSafeArea(bool on);
    void onActionLightTable(bool on);
    // The layer being drawn on painted over the others, whatever its place among them.
    void onActionCurrentDrawingOnTop(bool on);
    // What lies outside the camera frame darkened.
    void onActionCameraMask(bool on);
    // The pivot and the bones of the current layer shown whatever tool is in hand.
    void onActionShowControl(bool on);
    // That put away again, and nothing selected.
    void onActionHideAllControls();
    // How many drawings before and after the current one the onion skin shows.
    void onActionOnionAddPrevious() { setOnionRange(m_onionPrev + 1, m_onionNext); }
    // So many drawings before, or after (a number, as words in the menu have it: "0" to "3").
    void onActionOnionPrevious(QString const& count) { setOnionRange(count.toInt(), m_onionNext); }
    void onActionOnionNext(QString const& count) { setOnionRange(m_onionPrev, count.toInt()); }
    // "frames" or "drawings": what counts as the one before.
    void onActionOnionBy(QString const& what) { setOnionByFrame(what == QLatin1String("frames")); }
    void onActionOnionReducePrevious() { setOnionRange(m_onionPrev - 1, m_onionNext); }
    void onActionOnionReduceNext() { setOnionRange(m_onionPrev, m_onionNext - 1); }
    void onActionOnionAddNext() { setOnionRange(m_onionPrev, m_onionNext + 1); }
    // Joins the selected painted areas of one colour into one area.
    void onActionFlatten();
    // What is selected made one group (picked together from then on); and groups among it taken apart.
    void onActionGroupArtwork();
    // The lines of the line art copied into the colour art as lines with no thickness: the
    // areas to paint there are then closed by lines of the colour art's own.
    void onActionCreateColourArt();
    // A point dragged with the Contour Editor goes onto a point of another line or contour that is near.
    void onActionSnapToContour(bool on) { m_snapContour = on; }
    void setShowGuides(bool on)
    {
        if (m_showGuides == on)
            return;
        m_showGuides = on;
        emit showGuidesChanged(on);
        update();
    }
    // View > Alignment Guides.  `which` is "horizontal" or "vertical".
    void onActionShowGuides(bool on) { setShowGuides(on); }
    void onActionLockGuides(bool on) { m_guidesLocked = on; }
    void onActionClearGuides();
    void onActionNewGuide(QString const& which);
    void onActionCreateContourStrokes();
    void onActionRemoveContourStrokes();
    void onActionRemoveExtraStrokes();
    // The Select tool: a click on something picks everything of its colour.
    void setSelectByColour(bool on) { m_selectByColour = on; }
    // The Select tool: what is dragged goes edge to edge, or middle to middle, with the other
    // artwork of the drawing when it comes near; a line shows what it is in line with.
    void setSnapAlign(bool on) { m_snapAlign = on; }
    void setUsePreview(bool on)
    {
        m_usePreview = on;
        update();
    }
    // Points placed and shapes drawn go to the crossings of the grid when they come near one.
    void onActionSnapToGrid(bool on) { m_snapGrid = on; }
    // Animating: what the Transform tool does to a layer goes on a keyframe at the current
    // frame.  Off: the layer is moved as a whole, in all its keyframes alike.
    void onActionAnimateCurrentFrame()
    {
        m_animate = true;
        m_animateRange = false;
        emit animateChanged(true);
    }
    void onActionAnimateOff()
    {
        m_animate = false;
        m_animateRange = false;
        emit animateChanged(false);
    }
    // Animate Onion Skin Range: the keyframes within the onion skin's reach, before and after this
    // frame, go along with what the Transform tool does; those further off stay.
    void onActionAnimateOnionRange()
    {
        m_animate = false;
        m_animateRange = true;
        emit animateChanged(false);
    }
    bool animatesOnionRange() const { return m_animateRange; }
    // Each thing selected (a group counts as one) goes to a layer of its own, attached to this one.
    void onActionDistributeToLayers();
    void onActionUngroupArtwork();
    // The selected lines with their wobbles evened out; and with the points they can do without taken out.
    void onActionSmooth();
    void onActionReducePoints();
    // The other sheet of the drawing; one by name; and both shown, or only the one worked on.
    void onActionSwitchArt() { setArt(m_art == LineArt ? ColourArt : LineArt); }
    void onActionSetLineArt() { setArt(LineArt); }
    void onActionSetColourArt() { setArt(ColourArt); }
    void onActionSetOverlayArt() { setArt(OverlayArt); }
    void onActionSetUnderlayArt() { setArt(UnderlayArt); }
    void onActionPreviewAllArt(bool on);
    // On what is selected with the Select tool.
    void onActionFlipHorizontal();
    void onActionFlipVertical();
    void onActionRotate90CW();
    void onActionRotate90CCW();
    void onActionBringToFront();
    void onActionSendToBack();
    void onActionBringForward();
    void onActionSendBackward();
    void onActionRotate180();
    // Everything in the drawing of the colour in hand; and what is not selected in place of what is.
    void onActionSelectWithCurrentColour();
    void onActionInvertSelection();
    // The lines themselves (where the pen went) drawn thin over the artwork; with the
    // wash, the artwork is faint under them.
    void onActionShowStrokes(bool on);
    void onActionShowStrokesWash(bool on);
    // A selected line with the pen's swelling taken out of it; and a selected line turned
    // into the painted shape it covers.
    void onActionConvertBrushToPencil();
    void onActionConvertPencilToBrush();
    void onActionBreakApartText();
    void onActionRemoveDirt();
    void onActionRemoveHair();
    void onActionRemoveArtInside();
    void onActionRemoveArtOutside();
    void onActionSelectionArt(QString const& how);
    void onActionRespectProtected(bool on);
    void onActionCloseGaps();
    void onActionStrokesToPencil();
    void onActionCreateStamp();
    void onActionCreateDrawingFromSelection();
    void onActionAutoMatte();
    void onActionProportionGrid(bool on);
    void onActionHighlightColour(bool on);

signals:
    void selectionChanged();
    void toolChanged(int tool);
    // Where the pointer is over the view, in the scene (pixels from its top left corner).
    void pointerMoved(QPointF scenePos);
    // The transform tool was pressed on the artwork of another layer.
    void layerPicked(int layer);
    // The dropper was clicked on something of this colour (`swatch` is Scene::kNoSwatch
    // when the colour is not one of the palette).
    void colourPicked(int swatch, QColor const& colour);
    // A point was picked with the Contour Editor, or none is any more (-1).
    void pointPicked(int thicknessPercent);
    // The view is closer or further than it was.
    void zoomChanged();
    void showControlChanged(bool on);
    void artChanged(int art);
    void drawingViewChanged(bool on);
    // A template dragged out of the Library was let go over the view.
    void templateDropped(QString const& path);
    void lassoChanged(bool on);
    void showGuidesChanged(bool on);
    // The Text tool was pressed on words: this is the font they are in.
    void textFontPicked(QFont const& font);
    void textLayoutPicked(int align, int spacingPercent, int kerningPercent);
    void animateChanged(bool on);
    // Onion skin is on or off, or shows more or fewer drawings, or fainter ones.
    void onionChanged();
    // Something the user should know (shown in the status bar).
    void message(QString const& text);
    void stampsChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    bool event(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void tabletEvent(QTabletEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

private:
    enum Handle { NoHandle, TopLeft, TopRight, BottomRight, BottomLeft, Rotate };

    // From the picture (what the camera sees) to the widget: the zoom and pan of the view.
    QTransform cameraViewTransform() const;
    // From the scene to the widget: through the camera of the scene, then the above.
    QTransform viewTransform() const;
    QPointF toScene(QPointF widgetPos) const;
    // From the coordinates of a layer to those of the scene at a frame, with the pose being
    // dragged by the transform tool in place of the stored one.
    QTransform worldOf(int layer, int frame) const;
    // What is being dragged, for the scene to work with in place of what it has stored.
    PoseOverride dragged() const;
    // From the coordinates of the parent of the current layer to those of the scene, at the
    // depth the current layer stands at: what a drag across the view is measured in.
    QTransform parentSeen() const;
    // From the coordinates of the current layer to those of the widget.
    QTransform layerViewTransform() const;
    // Where the pointer is in the coordinates of the current layer: what the drawing tools use.
    QPointF toLayer(QPointF widgetPos) const;
    bool posing() const { return m_drag == DragPoseSkew || m_drag == DragPoseMove || m_drag == DragPoseScale || m_drag == DragPoseRotate || m_drag == DragBone || m_drag == DragIK || m_drag == DragWarp; }
    // The pose of a layer on a frame, with the one being dragged in place of the stored one.
    Pose poseOf(int layer, int frame) const;
    // A drawing as it is shown: bent along the bones of its layer, except on the layer
    // being drawn on, which is shown at rest so that the pen lands where it is put.
    Drawing shown(int layer, int frame, Drawing const& drawing) const;
    bool showsRest(int layer) const;
    void riggingDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    // The point or handle of a deformer under the pointer (its place in Layer::warp), or -1.
    int warpAt(QVector<QPointF> const& points, QPointF widgetPos) const;
    // The deformer at rest with the point being dragged by the Rigging tool where the pointer has it.
    QVector<QPointF> warpRestDragged() const;
    void paintWarp(QPainter& painter, QVector<QPointF> const& points, bool closed, int picked);
    void ikDown(QPointF widgetPos);
    // The IK tool on rigid bones: the bone in hand and those it hangs on turn so that its tip follows the pointer.
    void boneIkMove(QPointF widgetPos);
    QVector<int> m_ikChain;     // the bone taken hold of, then the bones it hangs on, nearest first
    bool m_ikBegun = false;     // whether anything has been turned yet in this drag
    void paintBones(QPainter& painter, QVector<QPointF> const& joints, int picked);
    // What the transform tool acts on - the current layer and all that hangs under it - as
    // a rectangle in the coordinates of the current layer.
    QRectF subjectBounds() const;
    // The topmost visible drawing layer with artwork under the point, or -1.
    int layerUnder(QPointF scenePos) const;
    void transformDown(QPointF widgetPos);
    // The line, rectangle or ellipse from where the drag began to `p`.
    void updateShape(QPointF p, Qt::KeyboardModifiers modifiers);
    void contourDown(QPointF p, Qt::KeyboardModifiers modifiers);
    void paintContourAids(QPainter& painter);
    bool contourValid() const;
    void paintTransformAids(QPainter& painter);
    void penDown(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers);
    void penMove(QPointF widgetPos, float pressure, Qt::KeyboardModifiers modifiers);
    void penUp();
    void updateCursor();
    void onDocumentChanged();

    // The whole drawing shown in the current cell (both sheets), or null.
    Drawing const* realDrawing() const;
    // The sheet of it the tools work on, as a drawing of its own, or null when the cell
    // is empty.  For the colour art this is a copy kept by the view.
    Drawing const* currentDrawing() const;
    // The whole drawing with the sheet worked on replaced by `art`.
    Drawing withArt(Drawing const& art) const;
    void addStrokeToArt(Stroke const& stroke);
    // What the pen drew, as the tool in hand keeps it: a brush shape, or a line.
    void commitPenStroke(Stroke const& stroke, double tolerance);
    // With a Symmetry guide in use: the turn-over about its axis, for what is on the layer.
    bool symmetry(QTransform* mirror) const;
    // Every way what is drawn is to be repeated: with a Symmetry guide chosen, all the Symmetry guides of the
    // scene count - over each axis, and over each after another.  Empty when none is chosen.
    QVector<QTransform> symmetries() const;
    // Keeps a line drawn - and, with a Symmetry guide in use, its twin on the other side, as one step.
    void keepStroke(Stroke const& stroke, bool fromPen, double tolerance = 0.0);
    void eraseArtAt(QPointF p, double radius);
    // The current drawing with the transform being dragged applied to the selection.
    Drawing previewDrawing() const;
    void commitDrawing(Drawing const& drawing, QString const& text);

    void paintAt(QPointF scenePos);
    QRectF selectionBounds() const;
    QPointF handlePos(Handle handle, QRectF const& bounds) const;  // in scene coordinates
    Handle handleAt(QPointF widgetPos) const;
    // Selects (or with `toggle`, flips) the item under the point; false when there is none.
    bool selectAt(QPointF scenePos, bool toggle);
    void selectInRect(QRectF const& rect, bool add);
    void setSelection(QVector<int> const& strokes, QVector<int> const& fills, QVector<int> const& pictures = {});

    Document* m_document;
    Tool m_tool = Brush;
    int m_swatch = Scene::kNoSwatch;
    double m_brushWidth = 6.0;
    int m_smoothing = 25;
    int m_cornerAngle = 50;
    Stroke::Join m_join = Stroke::RoundJoin;
    Stroke::Cap m_capStart = Stroke::RoundCap;
    Stroke::Cap m_capEnd = Stroke::RoundCap;
    template <class Change> void restyleSelection(QString const& text, Change change);
    bool m_onionSkin = true;
    bool m_playing = false;
    int m_layer = 0;
    int m_frame = 0;

    // view
    double m_zoom = 1.0;
    QPointF m_pan;          // widget-space offset of the centre of the scene from that of the widget
    bool m_fitted = true;   // keep fitting on resize until the user zooms or pans
    double m_viewRotation = 0.0;   // degrees, clockwise on the screen
    // The Camera view and the Drawing view are each looked at in their own way: how close, where,
    // and turned how far.  This is how the one not shown was left (not set until it has been shown).
    bool m_otherViewSet = false;
    double m_otherZoom = 1.0;
    QPointF m_otherPan;
    double m_otherRotation = 0.0;
    bool m_mirror = false;
    bool m_drawingView = false;
    bool m_grid = false;
    int m_gridKind = 0;
    bool m_gridOutline = false;
    bool m_gridUnder = false;
    bool m_backlight = false;
    bool m_outlineLocked = false;
    void paintGrid(QPainter& painter, QRectF const& frameRect) const;
    bool m_safeArea = false;
    bool m_lightTable = false;
    bool m_currentOnTop = false;
    bool m_cameraMask = false;
    Art m_art = LineArt;
    bool m_previewAllArt = true;
    mutable Drawing m_artView;   // the colour art of the current drawing, for currentDrawing()
    bool m_showStrokes = false;
    bool m_strokesWash = false;
    void stepSelection(int direction);
    bool m_showControl = false;
    int m_onionPrev = 1;
    int m_onionNext = 1;
    int m_onionOpacity = 35;
    int m_onionStrength[2][kOnionSteps] = {{100, 100, 100, 100, 100, 100}, {100, 100, 100, 100, 100, 100}};

    // interaction
    enum Drag {
        DragNone, DragDraw, DragErase, DragPan, DragMarquee, DragMove, DragScale, DragRotate,
        DragPoseMove, DragPoseScale, DragPoseRotate, DragPivot, DragShape, DragPoint, DragBone, DragJoint, DragIK, DragQuad, DragRotateView, DragHandle, DragPaint, DragPointMarquee, DragPolyHandle, DragWarpRest, DragWarp, DragGradient, DragGuide, DragThickness, DragInk, DragSmooth, DragPoseSkew, DragEnvelope, DragBoneIK, DragHint
    } m_drag = DragNone;
    bool m_spaceDown = false;
    bool m_polyActive = false;   // a polyline is being laid down, in m_stroke
    QPointF m_polyHover;         // where its next point would go (layer coordinates)
    void pickColourAt(QPointF widgetPos);
    QFont m_textFont;
    int m_textAlign = 0;
    int m_textSpacing = 100;
    int m_textKerning = 100;
    bool m_applyAll = false;
    bool m_onionByFrame = false;
    bool m_onionOutlines = false;
    int m_onionMark = 0;
    void paintOnion(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const& tint) const;
    QColor m_onionBefore = QColor(220, 60, 60);
    QColor m_onionAfter = QColor(50, 160, 80);
    QColor m_onionStepColour[2][kOnionSteps];
    // Does to the other drawings of the layer what was done to the selection: moves what
    // lies within `region` by `transform`, or takes it away when there is none.
    void alsoInOtherDrawings(QRectF const& region, QTransform const* transform, QString const& text);
    int m_quadCorner = -1;          // the corner of the quadmap being dragged
    QVector<QPointF> m_dragQuad;    // and where its corners are meanwhile
    void paintBoneLayers(QPainter& painter);
    void paintQuadAids(QPainter& painter);
    class TextEntry* m_textEntry = nullptr;   // where the Text tool is being typed into
    QPointF m_textAt;                         // and where the words will stand (layer coordinates)
    void beginText(QPointF widgetPos, QPointF layerPos, QString const& words = QString());
    int m_textEdit = -1;                      // the painted area whose words are being changed; -1: new words
    void endText(bool keep);
    void transformSelection(QTransform const& about, QString const& text);
    // Reposition All Drawings: what was just done to the sheet in hand is done to the rest of
    // the drawing and to all the other drawings of the layer.
    void repositionOthers(QTransform const& done, QString const& text);
    void arrangeSelection(bool front);
    bool m_tabletActive = false;  // a pen is driving: ignore the mouse events Qt synthesises
    QPointF m_lastWidgetPos;
    Stroke m_stroke;        // the stroke being drawn

    // selection: indices into the current drawing
    QVector<int> m_selStrokes;
    QVector<int> m_selFills;
    // The pictures of the drawing that are selected.  They belong to the drawing as its line art does, and are
    // picked, moved, scaled, turned and deleted there; the other commands of the Select tool leave them be.
    QVector<int> m_selPictures;
    bool m_keepNoPictures = false;      // the drawing being committed has had its last picture taken away
    bool m_committing = false;    // our own edit is going through: keep the selection
    QPointF m_dragStart;          // scene position where the drag began
    QPointF m_dragNow;
    QRectF m_dragBounds;          // selection bounds when the drag began
    Handle m_dragHandle = NoHandle;
    QTransform m_dragTransform;   // applied to the selection while dragging
    bool m_marqueeAdds = false;
    bool m_lasso = true;
    bool m_snapContour = false;
    bool m_showGuides = true;
    int m_activeGuide = -1;
    QPointF m_guideStart;        // where the line being drawn along the guide began (in the frame)
    QPointF m_guideWay;          // the way it was settled to run (null until the pen has moved)
    bool m_guideBends = false;   // ... and, when that way bends (4- and 5-point perspectives), the circle it is part of
    QPointF m_guideCentre;
    double m_guideRadius = 0.0;
    QPointF m_hoverFrame;        // where the pointer is (in the frame)
    bool m_hoverKnown = false;
    QVector<Scene::Guide> m_guideEdit;   // the guides while a point of one is dragged
    int m_guidePoint = -1;
    int m_guideLine = -1;        // the plain guide being dragged with the Select tool
    bool m_guidesLocked = false;
    bool m_selectByColour = false;
    int m_envCount = 8;
    double m_gapSize = 2.0;
    bool m_paintAll = false;
    bool m_paintLayers = false;
    bool m_bothArts = false;
    bool m_permanentSelection = false;
    bool m_snapAlign = false;
    QVector<QLineF> m_alignLines;        // what the selection being dragged is in line with (on the layer)
    QPointF aligned(QPointF by);
    QPointF alignedCorner(QPointF corner);     // the same for the corner a selection is scaled by
    int lineGuideAt(QPointF widgetPos) const;
    Scene::Guide const* drawingGuide() const;
    // The rectangle or ellipse from a to p (on the layer) set out along the guide; false when there is no guide for it.
    bool guidedShape(QPointF a, QPointF p);
    bool guideTool() const;
    QPointF toFrame(QPointF widgetPos) const;
    int guideHandleAt(QPointF widgetPos) const;
    void beginGuidedStroke(QPointF widgetPos);
    QPointF guided(QPointF widgetPos);
    // For a straight line from `fromWidget`: where it ends, kept to the way of the guide nearest to it.
    QPointF guidedFrom(QPointF fromWidget, QPointF widgetPos) const;
    void paintGuide(QPainter& painter);
    QVector<QImage> m_preview;
    bool m_usePreview = true;
    bool m_snapGrid = false;
    bool m_animate = true;
    bool m_keyFrameOne = false;
    bool m_animateRange = false;
    int m_transformMode = TransformAll;
    bool m_noZDrag = false;
    bool m_maintaining = false;         // the move being dragged is Maintain Size
    bool m_scaleFromPress = false;       // the scale being dragged is counted from where the pen went down
    void paintMotionPath(QPainter& painter);
    QPointF snapped(QPointF p) const;
    bool m_dragLasso = true;      // for the drag going on: Alt gives the other of the two
    QPolygonF m_lassoPath;        // the shape being drawn round things, in layer coordinates
    void selectInside(std::function<bool(QPointF)> const& inside, bool add);
    // The Cutter: what lies inside the shape is cut loose from what lies outside, and selected.
    void cutInside(QPolygonF const& shape);
    // The paint bucket dragged: every line and painted area with a point inside is painted (or unpainted).
    void paintInside(std::function<bool(QPointF)> const& inside, QRectF const& reach);

    // contour editor: the stroke whose points are shown, and the point picked on it
    int m_ceStroke = -1;
    int m_cePoint = -1;
    QVector<int> m_ceExtra;   // other points of that curve picked with it (Shift, or a box dragged round them)
    // Instead of a line, the Contour Editor may have a contour of a brush shape: contour
    // `m_ceContour` of fill `m_ceFill` (then m_ceStroke is -1).
    int m_ceFill = -1;
    int m_ceContour = 0;
    bool m_brushShapes = true;
    bool m_autoFlatten = false;
    bool m_drawBehind = false;
    bool m_dropperAllLayers = true;
    bool m_highlightColour = false;
    bool m_respectProtected = true;
    bool m_proportionGrid = false;
    int m_minSize = 0;
    Drawing m_stamp;             // artwork of the user's own to stamp, about the origin
    bool m_stampOwn = false;
    QVector<Drawing> m_stampSet;  // all the drawings of the stamp in hand (m_stamp is the one whose turn it is)
    int m_stampTurn = 0;
    bool m_stampRandom = false;
    void keepStamp(QVector<Drawing> const& drawings);
    int m_stampScale = 100;
    QPointF m_hoverWidget;       // where the pointer is (in the view)
    Drawing stampNow() const;
    void stampAt(QPointF p);
    int m_thickStroke = -1;      // the Pencil Editor: the line and the point of it in hand
    int m_thickPoint = -1;
    void thicknessDown(QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    bool m_inked = false;        // the Ink tool: whether the drag has coloured a line yet
    void inkAt(QPointF p);
    void smoothAt(QPointF p);
    // The Envelope tool: the outline round what is selected as it was when a point of it was taken
    // hold of, as it is pulled to, and the point in hand.
    QVector<QPointF> m_envRest, m_envNow;
    int m_envPoint = -1;
    int m_envPicked = -1;       // the point pressed last (its place among the points, times three): its handles show
    QVector<QPointF> selectionEnvelope() const;
    bool envelopeDown(QPointF widgetPos);
    void envelopeMove(QPointF p);
    // The Morphing tool.  The morphings that have a key drawing on this frame: which, and whether it is their second.
    QVector<QPair<int, bool>> morphsHere() const;
    void hintDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers);
    void hintUp();
    void paintMorphHints(QPainter& painter);
    int m_hintMorph = -1;       // the morphing whose hint is being dragged, and which of its hints
    int m_hintIndex = -1;
    bool m_hintFar = false;     // on the second drawing: where it goes to
    QPointF m_hintNow;
public:
    // Where a point of that outline is in the view (a null point when there is none): to take hold of it.
    QPointF envelopePointInView(int point) const;
    // A handle of the point of the outline pressed last (the one before it, or the one after); null when none shows.
    QPointF envelopeHandleInView(bool after) const;
private:
    double m_nudgeStep = 1.0;
    int m_propAcross = 3;
    int m_propDown = 3;
    double m_safeMargin = 0.1;
    bool m_eraserCuts = true;
    // Cuts what the eraser passed over (the line `m_stroke`, as thick as the eraser) out of the drawing.
    void cutWithEraser();
    void eraseStretch(int from);    // cuts the eraser's way from that point on out of m_eraseArt
    Drawing m_eraseArt;             // the art being erased, as it is so far in the drag
    // the Edit Gradient tool
    int m_gradFill = -1;
    int m_gradHandle = 0;           // 0: drawn out afresh, 1: its start, 2: its end
    bool m_gradMoved = false;
    QPointF m_gradFrom, m_gradTo, m_gradStart;
    QPointF m_gradStartWidget;
    bool gradientValid() const;
    void gradientDown(QPointF widgetPos, QPointF p);
    void paintGradientAids(QPainter& painter);
    bool m_eraseAny = false;        // whether the drag has taken anything yet
    // The curve the Contour Editor is working on, in a drawing; null when there is none.
    Stroke const* contourLine(Drawing const& drawing) const;
    Stroke* contourLine(Drawing& drawing) const;
    // After that curve was changed: a brush shape draws its outline out again.
    void contourEdited(Drawing& drawing) const;
    int m_ceHandle = -1;      // the handle of that point being dragged: 0 the one before it, 1 the one after
    bool m_ceBreak = false;   // Alt: the handle moves alone, and the point becomes a cusp

    // bones: the joint being dragged (rigging tool) or the bone being turned (transform tool)
    int m_bone = -1;
    // deformers: the point or handle being dragged (its place in Layer::warp)
    int m_warpIndex = -1;
    RigMode m_rigMode = RigBones;

    // transform tool
    Pose m_poseStart;             // the pose of the layer when the drag began
    Pose m_dragPose;              // the pose while dragging
    QPointF m_dragPivot;          // the pivot while dragging it, in layer coordinates
    QTransform m_world0;          // layer to scene when the drag began
    QPointF m_dragStartScene;
};
