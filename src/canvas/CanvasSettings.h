// Eclipse - the Camera view's settings as the panels and the window set and read them: its tools' sizes, stencils, textures,
// pressure, the grid, guides, stamps, snapping and the rest.  (Canvas has them as its own.)
#pragma once

#include <algorithm>
#include <cmath>

#include "canvas/CanvasState.h"

class CanvasSettings : public CanvasState {
public:
    // True for the paint bucket in any of its ways.
    static bool isPaintTool(Tool tool) { return tool == Paint || tool == PaintUnpainted || tool == Repaint || tool == Unpaint; }
    Art art() const { return m_art; }
    bool previewsAllArt() const { return m_previewAllArt; }
    Tool tool() const { return m_tool; }
    int swatch() const { return m_swatch; }
    void setBrushWidth(double width) { m_brushWidth = width; }
    bool brushOutline() const { return m_brushOutline; }
    double brushWidth() const { return m_brushWidth; }
    void setStabiliser(int mode) { m_stabiliser = std::clamp(mode, 0, 2); }
    int stabiliser() const { return m_stabiliser; }
    // 0 to 100: how far behind the pen the line keeps.
    void setStabiliserStrength(int strength) { m_stabiliserStrength = std::clamp(strength, 0, 100); }
    int stabiliserStrength() const { return m_stabiliserStrength; }
    void setLineMode(int mode) { m_lineMode = std::clamp(mode, 0, 2); }
    int lineMode() const { return m_lineMode; }
    bool bendingLine() const { return m_bending; }
    static int stencilSlot(int tool) { return tool == Line ? 1 : tool == Polyline ? 2 : 0; }
    // The Pencil Retouch tool: thicker (0) or thinner (1) where it is rubbed; more solid (2) or fainter (3).
    void setRetouchMode(int mode) { m_retouchMode = std::clamp(mode, 0, 3); }
    int retouchMode() const { return m_retouchMode; }
    // Preferences: how strongly pencil lines show in the views (per cent; what is exported is not changed).
    void setPencilLinesOpacity(int percent) { m_pencilOpacity = std::clamp(percent, 0, 100); settingChanged(); }
    void setLineOptions(LineOptions const& options) { m_lineOptions = options; }
    void setCopyArt(CopyArt const& how) { m_copyArt = how; }
    CopyArt const& copyArt() const { return m_copyArt; }
    bool autoColourArt() const { return m_autoColourArt; }
    LineOptions const& lineOptions() const { return m_lineOptions; }
    // The Brush and the Pencil: a line whose ends meet is painted inside with the colour in hand.
    void setLineAutoFill(bool on) { m_lineAutoFill = on; }
    bool lineAutoFill() const { return m_lineAutoFill; }
    // The Select tool: lines scaled get thicker or thinner with it (off: they keep their thickness).
    void setScaleThickness(bool on) { m_scaleThickness = on; }
    bool scaleThickness() const { return m_scaleThickness; }
    // The Rectangle and the Ellipse: the inside is painted with the colour in hand as the shape is drawn.
    void setShapeAutoFill(bool on) { m_shapeAutoFill = on; }
    bool shapeAutoFill() const { return m_shapeAutoFill; }
    void setSmoothing(int percent) { m_smoothing = percent; }
    int minimumBrushSize() const { return m_minSize; }
    void setMinimumBrushSize(int percent) { m_minSize = percent; }
    void setBitmapHardness(int percent) { m_bitmapHardness = std::clamp(percent, 0, 100); }
    void setBitmapFlow(int percent) { m_bitmapFlow = std::clamp(percent, 1, 100); }
    int bitmapHardness() const { return m_bitmapHardness; }
    BitmapTip bitmapTip() const { return m_bitmapTip; }
    void setBitmapTip(BitmapTip tip) { m_bitmapTip = tip; }
    int bitmapFlow() const { return m_bitmapFlow; }
    // The Brush with a pen that tells its tilt: the more the pen leans, the thicker the line (Tilt Sensitivity, per cent).
    void setTiltSensitivity(int percent) { m_tiltSensitivity = std::clamp(percent, 0, 100); }
    int tiltSensitivity() const { return m_tiltSensitivity; }
    void setPressureFeel(int feel) { m_pressureFeel = std::clamp(feel, 1, 100); }
    int pressureFeel() const { return m_pressureFeel; }
    float feltPressure(float raw) const
    {
        return std::pow(std::clamp(raw, 0.0f, 1.0f), std::pow(2.0f, (50.0f - float(m_pressureFeel)) / 25.0f));
    }
    // Turns sharper than this stay corners; gentler ones are rounded.
    void setCornerAngle(int degrees) { m_cornerAngle = degrees; }
    int lineTexture() const { return m_lineTexture; }
    int lineTextureAmount() const { return m_lineTextureAmount; }
    bool lineTextureCrisp() const { return m_lineTextureCrisp; }
    int brushTexture() const { return m_brushTexture; }
    int brushTextureAmount() const { return m_brushTextureAmount; }
    int lineTextureLow() const { return m_lineRampLow; }
    int lineTextureHigh() const { return m_lineRampHigh; }
    int brushTextureLow() const { return m_brushRampLow; }
    int brushTextureHigh() const { return m_brushRampHigh; }
    bool brushTextureCrisp() const { return m_brushTextureCrisp; }
    bool onionSkin() const { return m_onionSkin; }
    bool drawingView() const { return m_drawingView; }
    int gridKind() const { return m_gridKind; }
    // Preferences: how many fields the standard field grid has from the middle to each side, across and down (12 each).
    void setFieldChart(int across, int down)
    {
        m_fieldsAcross = std::clamp(across, 2, 64);
        m_fieldsDown = std::clamp(down, 2, 64);
        settingChanged();
    }
    int fieldsAcross() const { return m_fieldsAcross; }
    int fieldsDown() const { return m_fieldsDown; }
    bool backlight() const { return m_backlight; }
    // ---- selection (Select tool) --------------------------------------------------------
    QVector<int> const& selectedStrokes() const { return m_selStrokes; }
    QVector<int> const& selectedFills() const { return m_selFills; }
    bool hasSelection() const { return !m_selStrokes.isEmpty() || !m_selFills.isEmpty() || !m_selPictures.isEmpty(); }
    // How many lines and painted areas are selected.
    int selectionCount() const { return int(m_selStrokes.size() + m_selFills.size() + m_selPictures.size()); }
    // How many of the things selected are pictures (imported bitmaps: picked with the Select tool on the line art).
    int selectedPictureCount() const { return int(m_selPictures.size()); }
    // The Select tool or the Cutter: what is selected can be handled with either.
    bool selecting() const { return m_tool == Select || m_tool == Cutter || m_tool == Reposition || m_tool == Perspective || m_tool == Envelope; }
    int contourStroke() const { return m_ceStroke; }
    int contourPoint() const { return m_cePoint; }
    bool autoFlatten() const { return m_autoFlatten; }
    bool eraserCuts() const { return m_eraserCuts; }
    void setEraserCuts(bool on) { m_eraserCuts = on; }
    // Draw Behind: what the brush draws goes under the artwork already in the drawing.
    bool drawBehind() const { return m_drawBehind; }
    bool selectBothArts() const { return m_selectBothArts; }
    // ... which is so just now (with one of those tools, on one of those two sheets).
    bool bothArtsNow() const { return m_selectBothArts && (m_tool == Select || m_tool == Cutter) && (m_art == LineArt || m_art == ColourArt); }
    // The one picked last (-1, -1: none).
    int pickedControlKey() const { return m_cpKey; }
    int pickedControl() const { return m_cpIndex; }
    bool eraserBothArts() const { return m_bothArts; }
    // The Envelope tool: how many points the outline round the selection has.
    int envelopeToolPoints() const { return m_envCount; }
    void setEnvelopeToolPoints(int count)
    {
        m_envCount = count;
        settingChanged();
    }
    // ... and how wide a gap in the lines round an area they close of themselves (pixels of the drawing).
    double closeGapSize() const { return m_gapSize; }
    void setCloseGapSize(double pixels) { m_gapSize = pixels; }
    // Gap closing measured on the screen (so it closes the same gaps on screen at any zoom), else in drawing units.
    void setCloseGapInPixels(bool on) { m_gapInPixels = on; }
    bool closeGapInPixels() const { return m_gapInPixels; }
    double gapNow() const { return m_gapInPixels ? m_gapSize / std::max(m_zoom, 1e-6) : m_gapSize; }
    // The Paint tool: new paint left selected; textured areas painted plain; gradients laid as the last one placed.
    void setPaintSelectNew(bool on) { m_paintSelectNew = on; }
    bool paintSelectNew() const { return m_paintSelectNew; }
    void setPaintOverTexture(bool on) { m_paintOverTexture = on; }
    bool paintOverTexture() const { return m_paintOverTexture; }
    void setUseStoredGradient(bool on) { m_useStoredGradient = on; }
    bool useStoredGradient() const { return m_useStoredGradient; }
    bool hasStoredGradient() const { return m_hasStoredGradient; }
    // The Dropper: unseen lines and areas passed over; not sticky, the tool before it comes back once a colour is taken.
    void setDropperNoTransparency(bool on) { m_dropperNoTransparency = on; }
    void setBitmapTolerance(int t) { m_bitmapTolerance = std::clamp(t, 0, 255); }
    void setBitmapOverlap(int pixels) { m_bitmapOverlap = std::clamp(pixels, 0, 16); }
    void setBitmapSource(int source) { m_bitmapSource = std::clamp(source, 0, 1); }
    int bitmapTolerance() const { return m_bitmapTolerance; }
    int bitmapOverlap() const { return m_bitmapOverlap; }
    int bitmapSource() const { return m_bitmapSource; }
    bool dropperNoTransparency() const { return m_dropperNoTransparency; }
    void setStickyDropper(bool on) { m_stickyDropper = on; }
    bool stickyDropper() const { return m_stickyDropper; }
    // The Ink tool's options (Tool Properties); the order: 0 left as is, 1 over the rest, 2 under it.
    void setInkShowInkable(bool on) { m_inkShowInkable = on; settingChanged(); }
    bool inkShowInkable() const { return m_inkShowInkable; }
    void setInkSelectNew(bool on) { m_inkSelectNew = on; }
    bool inkSelectNew() const { return m_inkSelectNew; }
    void setInkSmart(bool on) { m_inkSmart = on; }
    bool inkSmart() const { return m_inkSmart; }
    void setInkSelectionMode(bool on) { m_inkSelectionMode = on; }
    bool inkSelectionMode() const { return m_inkSelectionMode; }
    void setInkJoinChange(bool on) { m_inkJoinChange = on; }
    bool inkJoinChange() const { return m_inkJoinChange; }
    void setInkOrder(int order) { m_inkOrder = std::clamp(order, 0, 2); }
    int inkOrder() const { return m_inkOrder; }
    bool paintsAllDrawings() const { return m_paintAll; }
    bool paintsAllLayers() const { return m_paintLayers; }
    bool permanentSelection() const { return m_permanentSelection; }
    // The Stamp tool: how big the stamp comes out (per cent), and whether it is one of the user's own.
    int stampScale() const { return m_stampScale; }
    bool hasOwnStamp() const { return m_stampOwn; }
    // The Stamp tool: each stamp made smaller by up to this much at random (per cent; 0: all as big as the scale says).
    void setStampRandomSize(int percent) { m_stampRandomSize = std::clamp(percent, 0, 90); }
    int stampRandomSize() const { return m_stampRandomSize; }
    // A stamp may hold several drawings: each click puts down the next of them (or any one, at random).
    int stampDrawingCount() const { return m_stampOwn ? int(std::max<qsizetype>(m_stampSet.size(), 1)) : 1; }
    int stampTurn() const { return m_stampTurn; }
    bool stampRandom() const { return m_stampRandom; }
    bool respectsProtected() const { return m_respectProtected; }
    bool proportionGrid() const { return m_proportionGrid; }
    double nudgeStep() const { return m_nudgeStep; }
    void setNudgeStep(double pixels) { m_nudgeStep = pixels; }
    int ikIterations() const { return m_ikIterations; }
    void setIkIterations(int rounds) { m_ikIterations = std::clamp(rounds, 1, 500); }
    double ikError() const { return m_ikError; }
    void setIkError(double units) { m_ikError = std::clamp(units, 0.001, 50.0); }
    int proportionAcross() const { return m_propAcross; }
    int proportionDown() const { return m_propDown; }
    void setProportionParts(int across, int down)
    {
        m_propAcross = across;
        m_propDown = down;
        settingChanged();
    }
    double safeMargin() const { return m_safeMargin; }
    void setSafeMargin(double part)
    {
        m_safeMargin = part;
        settingChanged();
    }
    // The dropper looks at every layer, or only at the one being worked on.
    bool dropperAllLayers() const { return m_dropperAllLayers; }
    // Everything that is not of the colour in hand shown faint.
    bool highlightColour() const { return m_highlightColour; }
    // Weights: the bone (0: the first) whose pull is painted, how big the dab is (scene units), and how strongly each one adds.
    int weightBone() const { return m_weightBone; }
    void setWeightBone(int bone) { m_weightBone = std::max(0, bone); settingChanged(); }
    double weightRadius() const { return m_weightRadius; }
    void setWeightStrength(double strength) { m_weightStrength = std::clamp(strength, 0.01, 1.0); }
    RigMode rigMode() const { return m_rigMode; }
    void setRigMode(RigMode mode) { m_rigMode = mode; settingChanged(); }
    bool brushShapes() const { return m_brushShapes; }
    int contourFill() const { return m_ceFill; }
    bool lasso() const { return m_lasso; }
    bool snapsToContour() const { return m_snapContour; }
    // Guides (Scene::guides): seen and snapped to, or not.
    bool showsGuides() const { return m_showGuides; }
    bool guidesLocked() const { return m_guidesLocked; }
    bool selectsByColour() const { return m_selectByColour; }
    bool snapsAndAligns() const { return m_snapAlign; }
    int activeGuide() const { return m_activeGuide; }
    int previewFrames() const { return int(m_preview.size()); }
    QSize previewSize() const { return m_preview.isEmpty() ? QSize() : m_preview.first().size(); }
    bool usesPreview() const { return m_usePreview; }
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode) { m_viewMode = mode; settingChanged(); }
    bool hasSnapshot() const { return !m_snapshot.isNull(); }
    QImage snapshot() const { return m_snapshot; }
    bool showsSnapshot() const { return m_showSnapshot; }
    bool snapsToGrid() const { return m_snapGrid; }
    bool animates() const { return m_animate; }
    // No Z Dragging: Alt with the Transform tool does not move a layer nearer or further.
    bool noZDragging() const { return m_noZDrag; }
    int transformMode() const { return m_transformMode; }
    void setTransformMode(int mode)
    {
        m_transformMode = mode;
        settingChanged();
    }
    bool keysFrameOne() const { return m_keyFrameOne; }
    void setKeysFrameOne(bool on) { m_keyFrameOne = on; }
    bool zoomsOnMouse() const { return m_zoomOnMouse; }
    void setZoomOnMouse(bool on) { m_zoomOnMouse = on; }
    double zoom() const { return m_zoom; }
    void setZoomLimits(int leastPercent, int mostPercent) { m_zoomMin = std::clamp(leastPercent, 1, 100) / 100.0; m_zoomMax = std::clamp(mostPercent, 100, 25600) / 100.0; }
    double zoomMin() const { return m_zoomMin; }
    double zoomMax() const { return m_zoomMax; }
    void setDefaultZoom(int percent) { m_defaultZoom = std::clamp(percent, 1, 25600); }
    int defaultZoom() const { return m_defaultZoom; }
    void setZoomToolOut(bool out) { m_zoomToolOut = out; }
    bool zoomToolOut() const { return m_zoomToolOut; }
    void setQuickCloseUpFactor(double factor) { m_quickFactor = std::clamp(factor, 1.1, 16.0); }
    double quickCloseUpFactor() const { return m_quickFactor; }
    void setPaperZoom(bool on, int screenPpi) { m_paperZoom = on; m_screenPpi = std::clamp(screenPpi, 30, 600); }
    bool paperZoom() const { return m_paperZoom; }
    bool shiftTrace() const { return m_shiftTrace; }
    bool traceManipulator() const { return m_traceManipulator; }
    int traceLayer() const { return m_traceLayer; }
    int traceDrawing() const { return m_traceDrawing; }
    double viewRotation() const { return m_viewRotation; }
    bool viewMirrored() const { return m_mirror; }
    int onionPrevious() const { return m_onionPrev; }
    int onionNext() const { return m_onionNext; }
    int onionOpacity() const { return m_onionOpacity; }
    int onionStrength(bool after, int step) const { return step >= 0 && step < kOnionSteps ? m_onionStrength[after ? 1 : 0][step] : 0; }
    bool showsControl() const { return m_showControl; }
    bool showsSymbolPivots() const { return m_symbolPivots; }
    // The points of the polyline being laid down (empty when there is none).
    int polylinePoints() const { return m_polyActive ? int(m_stroke.points.size()) : 0; }
    void setTextFont(QFont const& font) { m_textFont = font; }
    QFont textFont() const { return m_textFont; }
    void setTextAlign(int align) { m_textAlign = std::clamp(align, 0, 3); }     // 3: justified
    int textAlign() const { return m_textAlign; }
    void setTextLineSpacing(int percent) { m_textSpacing = std::clamp(percent, 20, 1000); }
    int textLineSpacing() const { return m_textSpacing; }
    // And how far apart the letters are, likewise.
    void setTextKerning(int percent) { m_textKerning = std::clamp(percent, 50, 400); }
    int textKerning() const { return m_textKerning; }
    bool appliesToAllDrawings() const { return m_applyAll; }
    int onionMark() const { return m_onionMark; }
    bool onionOutlines() const { return m_onionOutlines; }
    void setOnionBrightnessAlpha(bool on)
    {
        m_onionBrightnessAlpha = on;
        settingChanged();
    }
    bool onionBrightnessAlpha() const { return m_onionBrightnessAlpha; }
    // Preferences: the onion skins over everything rather than under the drawing they are of.
    void setOnionOver(bool on)
    {
        m_onionOver = on;
        settingChanged();
    }
    bool onionOver() const { return m_onionOver; }
    bool onionOriginal() const { return m_onionOriginal; }
    bool onionOthers() const { return m_onionOthers; }
    bool appliesToOnionRange() const { return m_applyOnion; }
    bool onionByFrame() const { return m_onionByFrame; }
    QColor onionBefore() const { return m_onionBefore; }
    QColor onionAfter() const { return m_onionAfter; }
    QColor backdrop() const { return m_backdrop; }
    int lightTableWash() const { return m_lightWash; }
    void setLightTableWhite(int percent) { m_lightWhite = std::clamp(percent, 0, 100); }
    int lightTableWhite() const { return m_lightWhite; }
    void setLightTableShade(bool on, bool inCamera) { m_lightShade = on; m_lightShadeCamera = inCamera; }
    bool lightTableShade() const { return m_lightShade; }
    bool lightTableShadeCamera() const { return m_lightShadeCamera; }
    void setOnionWashRange(int least, int most) { m_onionWashMin = std::clamp(least, 0, 100); m_onionWashMax = std::clamp(most, 0, 100); }
    int onionWashMin() const { return m_onionWashMin; }
    int onionWashMax() const { return m_onionWashMax; }
    bool onionWash() const { return m_onionWash; }
    bool onionStepHasColour(bool after, int step) const { return step >= 0 && step < kOnionSteps && m_onionStepColour[after ? 1 : 0][step].isValid(); }
    // The words being changed with the Text tool: which painted area of the drawing (-1: new words).
    int textEdited() const { return m_textEdit; }
    void setTipShape(TipShape const& shape) { m_tipShape = shape; }
    TipShape const& tipShape() const { return m_tipShape; }
    // A tip of your own (BitmapImage): a picture whose light parts paint (and whose clear parts do not).
    void setTipImage(QImage const& image) { m_tipImage = image; }
    // The dual tip's picture (a dualKind of BitmapImage).
    void setDualTipImage(QImage const& image) { m_dualImage = image; }
    QImage const& tipImage() const { return m_tipImage; }
    void setBitmapCanvas(int percent) { m_bitmapCanvas = std::clamp(percent, 100, 400); }
    void setBitmapCanvasSize(int widthPercent, int heightPercent)
    {
        m_bitmapCanvas = std::clamp(widthPercent, 100, 400);
        m_bitmapCanvasHeight = std::clamp(heightPercent, 100, 400);
    }
    void setBitmapPpi(int ppi) { m_bitmapPpi = std::clamp(ppi, 18, 1200); }
    void setBitmapCanvasBasis(int basis) { m_bitmapBasis = std::clamp(basis, 0, 2); }
    void setSnapping(bool on) { m_snapping = on; }
    bool snapping() const { return m_snapping; }
    // The Select tool: no box and no handles round what is selected (it is still moved by dragging it, and by the keys).
    void setHideManipulator(bool on) { m_hideManipulator = on; settingChanged(); }
    bool hidesManipulator() const { return m_hideManipulator; }
    // The Envelope tool: the handles of the outline's points (its advanced controls), shown and pulled, or not.
    void setEnvelopeAdvanced(bool on) { m_envAdvanced = on; settingChanged(); }
    bool envelopeAdvanced() const { return m_envAdvanced; }
    void setContourControls(bool on) { m_ceControls = on; settingChanged(); }
    bool contourControls() const { return m_ceControls; }
    void setCutterGesture(int gesture) { m_cutterGesture = std::clamp(gesture, 0, 3); }
    int cutterGesture() const { return m_cutterGesture; }
    // How the Cutter leaves the ends of a pencil line it cuts: cut square (Stroke::FlatCap, a bevel tip) or round.
    void setCutterTip(int cap) { m_cutterTip = std::clamp(cap, 0, 1); }
    int cutterTip() const { return m_cutterTip; }
    void setPencilMode(int mode) { m_pencilMode = std::clamp(mode, 0, 2); }
    int pencilMode() const { return m_pencilMode; }
    void setPushHighlight(QColor const& colour) { m_pushHighlight = colour; }
    QColor pushHighlight() const { return m_pushHighlight; }
    // The Polyline tool: its lines go under everything else of the art layer (Draw Behind Current Layers).
    void setPolylineBehind(bool on) { m_polyBehind = on; }
    bool polylineBehind() const { return m_polyBehind; }
    // The Eraser with a pen: how wide it rubs where the pen barely touches, per cent of its size (100: pressure leaves it be).
    void setEraserMinimumSize(int percent) { m_eraserMinSize = std::clamp(percent, 0, 100); }
    void setPencilMinimumSize(int percent) { m_pencilMinSize = std::clamp(percent, 0, 100); }
    int pencilMinimumSize() const { return m_pencilMinSize; }
    // Whether the tool in hand takes the pen's pressure as how thick it draws.
    bool pressureThickens() const { return m_tool == Brush || (m_tool == Pencil && m_pencilMinSize < 100); }
    int eraserMinimumSize() const { return m_eraserMinSize; }
    void setPressureSmoothing(bool on, int minimumPercent)
    {
        m_pressureSmoothing = on;
        m_minSmoothing = std::clamp(minimumPercent, 0, 100);
    }
    void setPencilOpacity(int minimumPercent, int smoothing)
    {
        m_pencilMinOpacity = std::clamp(minimumPercent, 0, 100);
        m_opacitySmoothing = std::clamp(smoothing, 0, 100);
    }
    void setSingleStroke(bool on) { m_singleStroke = on; }
    bool singleStroke() const { return m_singleStroke; }
    // Edge Type of what the shape tools draw on a bitmap layer: smooth (antialiased) or hard (every pixel all or nothing).
    void setHardEdges(bool on) { m_hardEdges = on; }
    bool hardEdges() const { return m_hardEdges; }
    void setMaskLayers(std::function<QVector<int>()> masks) { m_maskLayers = std::move(masks); }
    bool overlayBrush() const { return m_overlayBrush; }

protected:
    // A setting changed that shows: the view painted again.
    virtual void settingChanged() = 0;
};
