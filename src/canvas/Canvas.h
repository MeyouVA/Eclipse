// Eclipse - the drawing view: shows the current frame and takes pen / mouse input.
#pragma once

#include <cmath>

#include "EclipseExport.h"

#include <QUrl>
#include <QTransform>
#include <QFont>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <QPainterPath>

#include "canvas/CanvasSettings.h"
#include "model/scene/Scene.h"

class Document;
class QMenu;
struct LayerEffectSpace;

class ECLIPSE_CANVAS_API Canvas : public QWidget, public CanvasSettings {
    Q_OBJECT

public:

    explicit Canvas(Document* document, QWidget* parent = nullptr);

    // The hints of the morphing shown on this frame with the Morphing tool, where they are in the view.
    QVector<QPointF> morphHintsInView() const;
    void setArt(Art art);

    void setTool(Tool tool);
    void setLeader(Canvas* leader);
    void followSettings(Canvas const& leader);
    // The palette swatch new strokes and fills are linked to.
    void setSwatch(int id)
    {
        m_swatch = id;
        if (m_highlightColour)
            update();
    }
    // Preferences: the Brush, the Eraser and the Pencil show a circle as big as what they draw round the pointer.
    void setBrushOutline(bool on);
    // Where the line goes next, from where it is towards the pen, at that strength.
    static QPointF stabilised(QPointF line, QPointF pen, int strength);
    void setThicknessStencil(int stencil) { setThicknessStencilOf(m_tool, stencil); }
    void setThicknessStencilOf(int tool, int stencil) { m_stencils[stencilSlot(tool)] = std::clamp(int(stencil), 0, int(StencilCount)); }
    void setCustomStencilOf(int tool, QVector<float> const& profile) { m_customStencils[stencilSlot(tool)] = profile; }
    int thicknessStencilOf(int tool) const { return m_stencils[stencilSlot(tool)]; }
    // A stencil of your own (StencilCount): the thickness at evenly spaced places from the start of the line to its end.
    void setCustomStencil(QVector<float> const& profile) { setCustomStencilOf(m_tool, profile); }
    static float profileThickness(QVector<float> const& profile, double along);
    static void applyProfile(Stroke& stroke, QVector<float> const& profile);
    // The thickness of the first line selected along its length (32 places), for a stencil of your own; empty with none.
    QVector<float> selectedLineProfile() const;
    // The lines selected made wiggly: bent this far to each side (scene units), every so far along; one step to undo.
    void wiggleSelectedLines(double amount, double every, quint32 seed);
    int thicknessStencil() const { return thicknessStencilOf(m_tool); }
    // A stencil's thickness at `along` (0: the start of the line, 1: its end), as a share of the full thickness.
    static float stencilThickness(int stencil, double along);
    // The stencil laid on a line: each point as thick as the stencil says at its place along it.
    static void applyStencil(Stroke& stroke, int stencil);
    static int addCrossingPoints(QVector<Stroke>& strokes, QVector<int> const& which);
    // The steps, for the tests too: on the pen's samples (before they are fitted), and in `art` (the drawing it goes into).
    static void applyTaperAndSpeed(Stroke& stroke, LineOptions const& options, double zoom);
    static bool trimExtra(Stroke& stroke, Drawing const& art, double reach);
    static bool connectEnds(Stroke& stroke, Drawing const& art, double reach);
    // What the tool in hand makes of how hard the pen is pressed (0 to 1).
    float pressureFor(float raw) const
    {
        float const least = m_tool == Brush ? float(m_minSize) / 100.0f : m_tool == Eraser ? float(m_eraserMinSize) / 100.0f
                            : m_tool == Pencil ? float(m_pencilMinSize) / 100.0f : 0.0f;
        return least + (1.0f - least) * feltPressure(raw);
    }
    static float tiltedPressure(float pressure, double xTilt, double yTilt, int sensitivity)
    {
        double const lean = std::clamp(std::hypot(xTilt, yTilt) / 60.0, 0.0, 1.0);
        return float(pressure * (1.0 + sensitivity / 100.0 * lean * 1.5));
    }
    void setJoinStyle(int join);
    void setStartCap(int cap);
    void setEndCap(int cap);
    void setLineTexture(int texture);
    void setLineTextureAmount(int percent);
    // A crisp edge: the grain frays at the edge of the line but does not fade there, and is not blurred.
    void setLineTextureCrisp(bool crisp);
    void setBrushTexture(int texture);
    void setBrushTextureAmount(int percent);
    void setBrushTextureCrisp(bool crisp);
    // The ramp of the texture, per cent: grain fainter than `low` is no ink, denser than `high` solid ink.
    void setLineTextureRamp(int low, int high);
    void setBrushTextureRamp(int low, int high);
    void setOnionSkin(bool on);
    void setDrawingView(bool on);
    QJsonObject viewState() const;
    void restoreViewState(QJsonObject const& state);
    void setPlaneProjection(QTransform const& projection);
    void paintPlane(QPainter& painter);
    void cancelPlaneInput();
    bool planeInputActive() const { return m_drag != DragNone || m_bending || m_polyActive; }
    // While playing, nothing but the frame itself is shown (no onion skin, no editing).
    void setPlaying(bool playing);
    bool showsPlanningControls() const { return !m_playing && (m_showControl || m_tx.controlPointsOnSelected); }

    void fitToWindow();
    // Where a point of the scene is in the widget.
    QPointF mapFromScene(QPointF scenePos) const { return viewTransform().map(scenePos); }
    QPointF mapToScene(QPointF widgetPos) const { return toScene(widgetPos); }

    // The points picked with the Contour Editor besides contourPoint(), on the same curve.
    QVector<int> contourPointsPicked() const;
    void clearSelection();
    QPointF contourHandlePos(int handle) const;
    void setAutoFlatten(bool on) { m_autoFlatten = on; }
    int removeDirt(double size);
    int closeGaps(double gap);
    bool autoMatte(double radius, int source = Drawing::LineSheet, int destination = Drawing::ColourSheet);
    // The same on every drawing of the layer, one step of undo (how many drawings were given one).
    int autoMatteAll(double radius, int source = Drawing::LineSheet, int destination = Drawing::ColourSheet);
    void setPaintAllDrawings(bool on) { m_paintAll = on; }
    // The eraser rubs out of the line art and the colour art at once.
    void setEraserBothArts(bool on) { m_bothArts = on; }
    // The Select tool and the Cutter take from the line art and the colour art at once.
    Q_INVOKABLE void setSelectBothArts(bool on);
    // Pictures lie with the line art: they can be taken when it is (one of) the sheets worked on.
    bool picturesHere() const { return m_art == LineArt || bothArtsNow() || bitmapNow(); }
    // A picture of the current drawing that belongs to the art being worked on (painted pixels belong to one art layer).
    bool pictureHere(Picture const& picture) const
    {
        int const mine = sheetOf(m_art);
        return !picture.painted ? m_art == LineArt || bothArtsNow()
                                : picture.sheet == mine || (bothArtsNow() && (picture.sheet == Drawing::LineSheet || picture.sheet == Drawing::ColourSheet));
    }
    // The layer worked on is a bitmap layer, and its pixels are what the tools reach (they lie with the line art).
    bool bitmapNow() const;
    QPointF controlPointPos(int key, int control) const;
    bool controlPointAt(QPointF widgetPos, int* key, int* control) const;
    QPointF controlPointFromView(int key, int control, QPointF widgetPos) const;
    void pickControlPoint(int key, int control);
    // From the scene to the view (for checking where things are shown).
    QTransform viewTransformForTest() const { return viewTransform(); }
    void setPaintAllLayers(bool on) { m_paintLayers = on; }
    void setPermanentSelection(bool on);
    void setStampScale(int percent)
    {
        m_stampScale = percent;
        update();
    }
    void setStampRandom(bool on) { m_stampRandom = on; }
    // Makes a stamp of these drawings (each kept about its own middle), and takes it in hand.
    void createStampFrom(QVector<Drawing> const& drawings);
    void clearStamp();
    // The stamp of the user's own is kept between sessions: taken up again when the program starts.
    void loadStamp();
    QStringList stampNames() const;
    int currentStamp() const;
    void chooseStamp(int index);
    void removeStamp(int index);
    bool keepsColour(int swatch) const;
    int removeHair(double length);
    void setDropperAllLayers(bool on) { m_dropperAllLayers = on; }
    void setWeightRadius(double radius) { m_weightRadius = std::max(1.0, radius); }
    void setDrawBehind(bool on) { m_drawBehind = on; }
    void setBrushShapes(bool on) { m_brushShapes = on; }
    // Where a plain guide (a place in Scene::guides) crosses the view, level with `widgetPos`: to take hold of it.
    QPointF guideLineInView(int guide, QPointF widgetPos) const;
    void setActiveGuide(int index);
    // Where a point of the guide in use is in the view (a null point when there is none).
    QPointF guidePointInView(int point) const;
    void setPreview(QVector<QImage> const& frames);
    // The frame as that mode shows it, the size of the scene's frame.
    static QImage modeImage(Scene const& scene, int frame, ViewMode mode);
    // Snapshots: the frame as rendered, kept to compare later work with - shown in the view in place of the frame.
    void takeSnapshot();
    Q_INVOKABLE void onActionTakeSnapshot(QString slot);
    Q_INVOKABLE void onActionShowSnapshot(QString slot);
    Q_INVOKABLE void onActionHideSnapshot() { setShowSnapshot(false); }
    void setShowSnapshot(bool on) { m_showSnapshot = on && hasSnapshot(); update(); emit snapshotChanged(); }
    Q_INVOKABLE void onActionNoZDragging(bool on) { m_noZDrag = on; }
    // The Transform tool: its options and preferences; the other layers it works on along with the current one.
    TransformOptions const& transformOptions() const { return m_tx; }
    bool animatesPoints() const { return m_animatePoints; }
    void setTransformOptions(TransformOptions const& options);
    QVector<int> transformLayers() const { return m_transformLayers; }
    void setTransformLayers(QVector<int> const& layers);
    // The layer the animation tools move for `layer`: the peg above it in Peg Selection Mode, or when it is not to be animated (-1: none).
    int animationTarget(int layer) const;
    static int changedParts(Pose const& a, Pose const& b);
    Q_INVOKABLE void onActionPegSelectionMode(bool on);
    // Element Mode: the layer clicked is moved itself (Peg Selection Mode and Animate Using Animation Tools are passed over).
    Q_INVOKABLE void onActionElementMode(bool on);
    // View > Show > Deformation Mesh: the current layer's drawing with a lattice over it, bent as its deformers bend it.
    Q_INVOKABLE void onActionShowDeformationMesh(bool on) { m_deformationMesh = on; update(); }
    bool deformationMeshShown() const { return m_deformationMesh; }
    // The lattice the deformation mesh shows (in the layer's coordinates, bent), empty when the layer is not deformed.
    QVector<QPolygonF> deformationMesh(int layer, int frame) const;
    // Adjust Tip to Camera (Pencil, Brush): the size set is the size seen through the camera, whatever the layer's scale or depth.
    void setTipToCamera(int tool, bool on);
    bool tipToCamera(int tool) const { return tool == Pencil ? m_pencilTipToCamera : tool == Brush ? m_brushTipToCamera : false; }
    double tipWidth() const;
    // The layer whose artwork is under a place of the view (-1: none).
    int layerAt(QPointF widgetPos) const { return layerUnder(toScene(widgetPos)); }
    // Which of `urls` are pictures that can be read in.
    static QStringList pictureFiles(QList<QUrl> const& urls);
    // Preferences: a preview the scene has changed since is still played, washed, until it is rendered again.
    void setWashStalePreview(bool on) { m_washStalePreview = on; }
    bool washesStalePreview() const { return m_washStalePreview; }
    void setPreviewStale(bool stale) { m_previewStale = stale; update(); }
    bool previewStale() const { return m_previewStale; }
    bool elementMode() const { return m_tx.elementMode; }
    // Select tool: what is selected stays selected on other frames (Permanent Selection), from the menus.
    Q_INVOKABLE void onActionPermanentSelection(bool on) { setPermanentSelection(on); }
    // The selected lines' pencil texture: which one, how much shows, crisp or soft, and its ramp, in one window.
    Q_INVOKABLE void onActionAdjustLineTexture();
    // The Static Camera Transform tool's drag: how far the camera goes in what it hangs from, and in depth.
    void staticCameraOffset(QPointF& by, double& depth) const;
    Q_INVOKABLE void onActionLockSelection(bool on);
    Q_INVOKABLE void onActionShowTransformControls(bool on);
    Q_INVOKABLE void onActionDisablePivotManipulator(bool on);
    Q_INVOKABLE void onActionMovePivotToCursor();
    Q_INVOKABLE void onActionResetDrawingPivot();
    void nudgeLayer(QPointF direction, bool far);
    // The Drawing Pivot tool sets the pivot of every drawing of the layer, not just the current one.
    void setPivotAllDrawings(bool on) { m_pivotAllDrawings = on; }
    // Reposition Drawing: the Reposition tool moves the current drawing only, not every drawing of the layer.
    void setRepositionCurrent(bool on) { m_repositionCurrent = on; }
    bool pivotAllDrawings() const { return m_pivotAllDrawings; }
    // The keyframe or control point before or after the one picked on the motion path: picked, and the frame it is on.
    bool stepPathPoint(int direction, int* frame);
    bool pickNearestControlPoint();
    int pickedControlKey() const { return m_cpKey; }
    int pickedControlIndex() const { return m_cpIndex; }
    void setLasso(bool on);
    // The box round what is selected, in the coordinates of the drawing; null with nothing selected.
    QRectF selectionRect() const { return hasSelection() ? selectionBounds() : QRectF(); }
    // Moves and stretches what is selected so that it fills this box.
    void setSelectionRect(QRectF const& box);
    // Where a corner of the selection's box is in the view (0: top left, then round to the right; (-1, -1): none selected).
    QPointF selectionCorner(int corner) const;
    // These lines, painted areas and pictures of the current drawing selected (indices into it).
    void setSelection(QVector<int> const& strokes, QVector<int> const& fills, QVector<int> const& pictures = {});
    // Turns what is selected about its middle, clockwise.
    void rotateSelection(double degrees);
    void copySelection();
    void cutSelection();
    // Zooms about the middle of the view.
    void zoomBy(double factor);
    // Zoom In and Zoom Out: about the pointer when it is over the view and the preferences say so; otherwise about the middle.
    void zoomByShortcut(double factor);
    // The same, keeping the point of the view under `anchor` where it is.
    void zoomAt(QPointF anchor, double factor);
    void rotateView(double degrees);
    void setOnionRange(int previous, int next);
    // How strongly the onion skin shows, 5 to 100 per cent.
    void setOnionOpacity(int percent);
    void setOnionStrength(bool after, int step, int percent);
    // Where the pivot of a layer is in the view on the current frame.
    QPointF pivotInView(int layer) const;
    // Ends the polyline being laid down: kept (when it has two points), or thrown away.
    void finishPolyline(bool keep);

    void setApplyToAllDrawings(bool on) { m_applyAll = on; }
    // Onion skin by frame: the frames just before and after, instead of the neighbouring drawings.
    void setOnionByFrame(bool on);
    // The drawings before and after as outlines only: the edges of their paint and the paths of their lines.
    void setOnionOutlines(bool on);
    void setOnionOriginal(bool on);
    void setOnionOthers(bool on);
    void setOnionWash(bool on);
    // How the onion skins are drawn: 0 in the onion colours, 1 in their own colours, 2 as coloured outlines.
    void setOnionStyle(int style);
    int onionStyle() const { return m_onionOutlines ? 2 : m_onionOriginal ? 1 : 0; }
    void setApplyToOnionRange(bool on);
    // The drawings of the layer worked on that the onion skin shows now (by index in the layer).
    QSet<int> onionDrawings() const;
    // Only the drawings marked so (Drawing::Mark) are shown before and after; NoMark: all of them.
    void setOnionMark(int mark);
    void setOnionColours(QColor const& before, QColor const& after);
    // The colour of the view around the frame of the camera (Preferences > Colours).
    void setBackdrop(QColor const& colour);
    // The light table: how strongly the layers that are not being drawn on show, 10 to 90 per cent.
    void setLightTableWash(int percent);
    void setOnionStepColour(bool after, int step, QColor const& colour);
    QColor onionStepColour(bool after, int step) const;
    void addText(QPointF layerPos, QString const& text);
    int gradientPicked() const { return gradientValid() ? m_gradFill : -1; }
    // ... or the line whose gradient it has in hand (-1: none, or an area).
    int gradientLinePicked() const { return gradientValid() ? m_gradStroke : -1; }

    int pointThickness() const;
    void setPointThickness(int percent);
    static bool canPaste();
    // The sheet of a drawing an art layer of the view stands for (Drawing::Sheet).
    static int sheetOf(Art art);

    // Renders one frame of a scene the way the canvas shows it, without the editing aids.
    static void paintFrame(QPainter& painter, Scene const& scene, int frame);
    // From the scene to what camera `camera` (a layer) would show, as the scene's own camera does with cameraTransform.
    static QTransform viewedThrough(Scene const& scene, int camera, int frame);
    static void setBlursEnabled(bool on);
    static bool blursEnabled();
    // Whether frames are painted for the view itself (on) or rendered (off, the default), for what shows only in one of them.
    static void setViewing(bool on);
    static bool viewing();
    static void setFocusEnabled(bool on);
    static bool focusEnabled();
    static void setToneHighlightEnabled(bool on);
    static bool toneHighlightEnabled();
    static void setColourOverrideEnabled(bool on);
    static bool colourOverrideEnabled();
    // Whether the preferences leave this kind of effect out (blurs, focus, tone and highlight, colour override).
    static bool switchedOff(Effect::Type type);
    static bool paintGraph(QPainter& painter, Scene const& scene, int frame);
    static void paintDrawing(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity,
                             QColor const* tint = nullptr);
    static void paintLayerDrawing(QPainter& painter, Scene const& scene, int layer, int frame, Drawing const& drawing,
                                  double opacity, QTransform const& sceneToDevice, bool everySheet = false, bool cameraClipping = true);
    static void paintStroke(QPainter& painter, Stroke const& stroke, QBrush const& color);
    // How strongly lines are painted just now (Preferences: Pencil Lines Opacity, while the view is painted).
    static inline double s_lineOpacity = 1.0;
    // Pasted artwork in colours the scene lacks: those colours made again (colour recovery), asking first when interactive.
    static inline bool s_colourRecovery = true;
    static inline bool s_interactiveRecovery = false;
    // Whether artwork (not cells) was copied last.
    static bool artworkCopiedLast();
    static void setArtworkCopiedLast(bool artwork);
    static inline bool s_fineFills = true;
    static QVector<Stroke> wallsOf(Drawing const& drawing, int sheet);
    static void underShapes(QVector<QPolygonF>& outlines, Drawing const& drawing);
    // Where new bucket paint goes among the fills: under the shapes with paint of their own (brush shapes, Auto Fill).
    static int underIndex(Drawing const& drawing);
    static quint64 wallsKey(QVector<Stroke> const& walls);
    static QVector<QPolygonF> fineOutlinesOf(Fill const& fill, QVector<Stroke> const& walls, quint64 key);
    static void paintFill(QPainter& painter, Fill const& fill, QBrush const& color);
    // The tips of a Photoshop brush file (.abr, versions 6 to 10: its sampled brushes), as pictures; none when it is not one.
    static QVector<QImage> readAbr(QString const& path, QString* why = nullptr);
    static void rasterize(Drawing& drawing, int sheet, Scene const& scene, double fineness, QRectF const& canvas);
    // The painted pixels of a drawing made finer or coarser by `factor` (2: twice as many each way); the picture stays as big.
    static void resampleBitmaps(Drawing& drawing, double factor);
    // Where the pixels of a new bitmap drawing of the current layer lie, on the layer; and how many to a unit of the scene.
    QRectF bitmapArea() const;
    double bitmapFineness() const;
    QRectF contourControlBox() const;
    QPointF contourControlInView(int which) const;
    // The gesture from a to b done on the lines of `drawing` (a CutterGesture); whether anything changed.
    static bool gestureCut(Drawing& drawing, QPointF a, QPointF b, int gesture, int tip);
    // Whether a path drawn with the Cutter is a gesture: long enough on the screen, and near enough straight.
    static bool isGesture(QPolygonF const& path, double zoom);
    void nudgeSelectionThickness(bool thicker, bool more);
    static bool pushLine(Drawing& drawing, Stroke const& highlight, int mode, double reach);
    static void multiplyThickness(Stroke& line, double multiplier, bool tips, double tipExtra);
    static QPointF insideOf(QVector<QPolygonF> const& outlines);
    static bool segmentAt(Stroke const& line, QPointF p, double reach, int& segment, double& t);
    static Stroke bentSegment(Stroke line, int segment, double t, QPointF by);
    // The smoothing for a line drawn pressing so hard on the whole (0 to 1).
    int smoothingFor(double pressure) const
    {
        if (!m_pressureSmoothing)
            return m_smoothing;
        double const share = m_minSmoothing / 100.0 + (1.0 - m_minSmoothing / 100.0) * std::clamp(pressure, 0.0, 1.0);
        return int(std::lround(m_smoothing * share));
    }
    static void smoothOpacity(Stroke& line, int percent);
    static QVector<StrokePoint> evenlySampled(QVector<StrokePoint> const& samples, double step);
    // The artwork of those layers on this frame, on the current layer (empty with none).
    QPainterPath overlayMask() const;
    // What is selected moved by so much, as one step (with Reposition All Drawings: all of every drawing of the layer).
    void moveSelectionBy(QPointF by, QString const& text) { transformSelection(QTransform::fromTranslate(by.x(), by.y()), text); }

public slots:
    // Rigging > Deformation > Animate Points: with Animate on, the Contour Editor moves the drawing's own points on a keyframe of this frame.
    void onActionAnimatePoints(bool on);
    void onActionResetPoints();
    // View > Show Contour Editor Controls.
    void onActionShowContourControls(bool on)
    {
        setContourControls(on);
        emit toolSettingsChanged();
    }
    // The Select tool's Scale Pencil Line Thickness, switched from the menu (or its shortcut).
    void onActionScalePencilThickness(bool on)
    {
        setScaleThickness(on);
        emit toolSettingsChanged();
    }
    // Drawing > Overlay Brush Mode.
    void onActionOverlayBrushMode(bool on)
    {
        m_overlayBrush = on;
        emit toolSettingsChanged();
    }
    // Copy Thickness: how thick the first line selected is along its length; Paste Thickness: the lines selected made so.
    void onActionCopyThickness();
    void onActionPasteThickness();
    void setCurrentCell(int layer, int frame);
    int morphHere() const;
    // The layer and the frame the view works on.
    int currentLayer() const { return m_layer; }
    int currentFrame() const { return m_frame; }
    int morphGroup() const { return m_morphGroup; }
    void setMorphGroup(int group);
    int morphShapeAt(QPointF drawingPos) const;

    // ---- what the menus call (this view is the "camera" responder) -------------------
    void cut() { cutSelection(); }
    void copy() { copySelection(); }
    void paste();
    void pasteInPlace();
    void deleteSelection();
    void selectAll();
    void deselectAll() { clearSelection(); }
    void onActionZoomIn() { zoomByShortcut(1.25); }
    void onActionZoomOut() { zoomByShortcut(0.8); }
    void onActionResetView() { fitToWindow(); }
    void onActionToggleOnionSkin(bool on) { setOnionSkin(on); }
    void onActionRotateViewCW() { rotateView(30.0); }
    void onActionRotateViewCCW() { rotateView(-30.0); }
    void onActionMirrorView(bool on);
    void onActionResetRotation() { rotateView(-m_viewRotation); }
    void onActionResetZoom() { setZoomPercent(m_defaultZoom); }
    // The zoom as shown (per cent): of the frame's real size when paper zoom is on.
    double zoomPercent() const;
    void setZoomPercent(double percent);
    double zoomUnit() const;
    // Shift and Trace and the Desk.
    void onActionShiftTrace(bool on);
    void onActionShiftTraceManipulator();
    void onActionShiftTraceReset();
    void onActionShiftTraceVisibility();
    void onActionShiftTraceMove(QString const& way);
    void onActionShiftTraceMoveTool() { setTool(ShiftTrace); }
    void onActionClearShiftTrace();
    void setTraceTarget(int layer, int drawing);
    // Where a drawing of the onion skin or the Desk is shown: its own place, or (Shift and Trace on) where it was moved to.
    QTransform traceOf(int layer, int drawing) const;
    bool traceHidden(int layer, int drawing) const { return m_traceHidden.contains({layer, drawing}); }
    void onActionPerformZoom(QString const& way) { zoomBy(way == QLatin1String("out") ? 1.0 / 1.5 : 1.5); }
    void onActionResetPan();
    void onActionSmoothFast();
    void onActionPumpThickness();
    void onActionMergePencilLines();
    void onActionJoinPencilLines();
    void onActionSplitPencilLine();
    void onActionReversePencilThickness();
    // Drawing > Pencil Line Wiggle...: asks how much, then wiggles the lines selected.
    void onActionPencilLineWiggle();
    // Soft tips: the lines selected (or every line of every drawing of the layer) fade in at their start and out at their end.
    void onActionSetPencilSoftTip();
    void onActionSetPencilSoftTipAllDrawings();
    static void softenTips(Stroke& line, double share);
    // The Eraser's tip: round, or square (a square swept along the eraser's path).
    void setEraserSquare(bool square) { m_eraserSquare = square; update(); }
    bool eraserSquare() const { return m_eraserSquare; }
    // The area a stretch of the eraser's path covers, with the tip it has.
    QVector<QPolygonF> eraserArea(Stroke const& path) const;
    void onActionDeflateThickness();
    void onActionSetThickness();
    void onActionDeleteNonSelection();
    void setPasteBehaviour(int how) { m_pasteBehaviour = std::clamp(how, 0, 2); }
    int pasteBehaviour() const { return m_pasteBehaviour; }
    // The view moved so that what is under the pointer is in the middle of it.
    void onActionRecentre();
    void onActionQuickCloseup();
    // What is selected, in the middle of the view and as big as it will go.
    void onActionFrameSelection();
    void onActionSnapAndAlign(bool on) { m_snapAlign = on; emit toolSettingsChanged(); }
    void onActionAutoFillInside(bool on) { m_lineAutoFill = on; emit toolSettingsChanged(); }
    bool inQuickCloseup() const { return m_closeup; }
    // The same, about a place of the view rather than the pointer.
    void recentreOn(QPointF widgetPos);
    void quickCloseupAt(QPointF widgetPos);
    void setMiddleButtonPans(bool on) { m_middlePans = on; }
    bool middleButtonPans() const { return m_middlePans; }
    // Preferences (Colours): of bones, and of the bone or joint picked.
    void setBoneColours(QColor const& bone, QColor const& selected)
    {
        m_boneColour = bone;
        m_boneSelectedColour = selected;
        update();
    }
    QColor boneColour() const { return m_boneColour; }
    QColor boneSelectedColour() const { return m_boneSelectedColour; }
    void setPerformanceReport(bool on)
    {
        m_performanceReport = on;
        update();
    }
    bool performanceReport() const { return m_performanceReport; }
    // How long the last picture of the view took to draw (milliseconds).
    double lastDrawTime() const { return m_lastDrawMs; }
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
    bool outlinesLocked() const { return m_outlineLocked; }
    void onActionShowSymbolPivot(bool on);
    // Show > BBox Selection Style: what is selected is told by its box alone - no line drawn over each thing.
    void onActionBBoxSelectionStyle(bool on)
    {
        m_bboxSelection = on;
        update();
    }
    bool bboxSelectionStyle() const { return m_bboxSelection; }
    // Hide Selected / Show Hidden: layers left out of the Camera and scene planning views (never out of what is rendered).
    void setViewHidden(QSet<int> const& ids);
    QSet<int> viewHidden() const { return m_viewHidden; }
    bool hiddenInView(int layer) const;
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
    void onActionOnionOthers(bool on) { setOnionOthers(on); }
    void onActionOnionStyle(QString const& style) { setOnionStyle(style.toInt()); }
    void onActionOnionWash(bool on) { setOnionWash(on); }
    void onActionApplyToOnionRange(bool on) { setApplyToOnionRange(on); }
    void onActionOnionReducePrevious() { setOnionRange(m_onionPrev - 1, m_onionNext); }
    void onActionOnionReduceNext() { setOnionRange(m_onionPrev, m_onionNext - 1); }
    void onActionOnionAddNext() { setOnionRange(m_onionPrev, m_onionNext + 1); }
    // Joins the selected painted areas of one colour into one area.
    void onActionFlatten();
    void onActionFlattenDrawing();
    void onActionFlattenAllDrawings();
    void onActionFlattenRemoveExtra();
    // The lines with no thickness of the art in hand (of the selection, when there is one) taken away.
    void onActionRemoveInvisibleLines();
    // Drawing > Convert > Convert to Bitmap Drawing: the art layer being worked on, painted into its pixels.
    void onActionConvertToBitmap();
    // Layer: every drawing of the layer made as its art layers are - those that are bitmap ones painted into pixels.
    void onActionMatchArtLayerType();
    // Drawing > Change Bitmap Drawing Resolution...: the pixels of the drawing finer or coarser.
    void onActionBitmapResolution();
    // The same, said how: so much finer (2: twice), on this drawing or every drawing of the layer.
    void changeBitmapResolution(double factor, bool allDrawings);
    // The Contour Editor's picked point taken out of its joint: a point of its own line only, from then on.
    void onActionBreakJoint();
    // Overlay art to underlay art and back (from the line or colour art: to the overlay).
    void onActionSwitchOverlayUnderlay() { setArt(m_art == OverlayArt ? UnderlayArt : OverlayArt); }
    void onActionAutoColourArt(bool on) { m_autoColourArt = on; }
    void onActionCopyArt();
    void onActionCopyArtSettings();
    // What is selected made one group (picked together from then on); and groups among it taken apart.
    void onActionGroupArtwork();
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
    void setSnapAlign(bool on) { m_snapAlign = on; }
    void setUsePreview(bool on)
    {
        m_usePreview = on;
        update();
    }
    // Points placed and shapes drawn go to the crossings of the grid when they come near one.
    void onActionSnapToGrid(bool on) { m_snapGrid = on; }
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
    void onActionAnimateOnionRange()
    {
        m_animate = false;
        m_animateRange = true;
        emit animateChanged(false);
    }
    bool animatesOnionRange() const { return m_animateRange; }
    bool animatesCurrentFrame() const { return m_animate; }
    QPointF sceneAtWidget(QPointF widgetPos) const { return toScene(widgetPos); }
    // Each thing selected (a group counts as one) goes to a layer of its own, attached to this one.
    void onActionDistributeToLayers();
    void onActionUngroupArtwork();
    // The selected lines with their wobbles evened out; and with the points they can do without taken out.
    void onActionSmooth();
    void onActionReducePoints();
    // The other sheet of the drawing; one by name; and both shown, or only the one worked on.
    void onActionLockInTime();
    void onActionRemoveControlPoint();
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
    // Every line of the drawing drawn with the pencil texture in hand.
    void onActionSelectWithCurrentTexture();
    // The selected lines: how much of their texture shows; and their thickness, texture and all in one window.
    void onActionAdjustLineTextureOpacity();
    void onActionPencilLineProperties();
    // Textures of your own: one from a picture, one put together here, and the one in hand thrown away.
    void onActionImportTexture();
    void onActionNewTexture();
    void onActionDeleteTexture();
    void onActionRenameTexture();
    void onActionExportTexture();
    void takeUpTexture(int id);
    void onActionInvertSelection();
    void onActionShowStrokes(bool on);
    void onActionShowStrokesWash(bool on);
    void onActionConvertBrushToPencil();
    void onActionConvertPencilToBrush();
    void onActionBreakApartText();
    void onActionRemoveDirt();
    void onActionRemoveHair();
    void onActionRemoveArtInside();
    void onActionRemoveArtOutside();
    void onActionSelectionArt(QString const& how);
    void onActionRespectProtected(bool on);
    // V2 stage 9: Auto Gap Closing (off, small, medium, large), Paint and Remove Line Texture, pencil textures set and taken.
    void onActionAutoGap(QString const& size);
    void onShowAutoGapMenu(QMenu* menu);
    void onActionPaintOverTexture(bool on);
    void onActionSetPencilTexture();
    void onActionSetPencilTextureAll();
    void onActionPencilTextureNone();
    void onActionPencilTextureEyedropper();
    void onActionCloseGaps();
    void onActionStrokesToPencil();
    void onActionCreateStamp();
    void onActionCreateDrawingFromSelection();
    void onActionAutoMatte();
    void onActionProportionGrid(bool on);
    void onActionHighlightColour(bool on);

signals:
    void snapshotChanged();
    // A setting of the tool in hand changed by a command (Tool Properties shows it again).
    void toolSettingsChanged();
    void selectionChanged();
    // The textures there are to choose from have changed, or the one in hand has.
    void texturesChanged();
    void toolChanged(int tool);
    // Where the pointer is over the view, in the scene (pixels from its top left corner).
    void pointerMoved(QPointF scenePos);
    // The transform tool was pressed on the artwork of another layer.
    void layerPicked(int layer);
    void transformLayersChanged(QVector<int> const& layers);
    void colourPicked(int swatch, QColor const& colour);
    // A point was picked with the Contour Editor, or none is any more (-1).
    void pointPicked(int thicknessPercent);
    // The view is closer or further than it was.
    void zoomChanged();
    void viewHiddenChanged();
    void showControlChanged(bool on);
    void planningControlsChanged();
    void artChanged(int art);
    void drawingViewChanged(bool on);
    void controlPointPicked(int key, int control);
    void selectBothArtsChanged(bool on);
    // A template dragged out of the Library was let go over the view.
    void templateDropped(QString const& path);
    // Pictures dragged in from outside, let go at `scenePos` (in the scene).
    void filesDropped(QStringList const& files, QPointF scenePos);
    void lassoChanged(bool on);
    void showGuidesChanged(bool on);
    // The Text tool was pressed on words: this is the font they are in.
    void textFontPicked(QFont const& font);
    void textLayoutPicked(int align, int spacingPercent, int kerningPercent);
    void animateChanged(bool on);
    // Onion skin is on or off, or shows more or fewer drawings, or fainter ones.
    void onionChanged();
    // Another layer or frame: the morphing the Morphing tool works on may be another.
    void morphHereChanged();
    // Something the user should know (shown in the status bar).
    void message(QString const& text);
    // Peg Selection Mode and Element Mode (one turns the other off), for the menus to show.
    void selectionModesChanged(bool pegSelection, bool elementMode);
    void permanentSelectionChanged(bool on);
    // The gap closed by the paint tools changed (from the Auto Gap Closing menu); a pencil texture taken by the eyedropper.
    void closeGapChanged(double size);
    void lineTextureTaken(int texture);
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
    void pasteWithPlacement(bool inPlace);
    // The colours of pasted artwork this scene lacks, recovered or unlinked (a macro is left open when any): theirs -> here.
    QHash<int, int> recoverPastedColours(Drawing const& placed);
    // The lines just inked put over or under the others (Ink tool properties); m_inkedNow follows them.
    void finishInk(Drawing& art, int order);
    // A colour taken by the Dropper: told, and (not sticky) the tool before it back.
    void tookColour(int swatch, QColor const& colour);
    // The Pencil Texture Eyedropper: the texture of the pencil line clicked taken into hand.
    bool takeTextureAt(QPointF widgetPos);
    void settingChanged() override { update(); }
    // One effect of a layer done to its picture (part of paintLayerDrawing).
    static void applyLayerEffect(QImage& image, int i, LayerEffectSpace const& space);

    // From the picture (what the camera sees) to the widget: the zoom and pan of the view.
    QTransform cameraViewTransform() const;
    // From the scene to the widget: through the camera of the scene, then the above.
    QTransform viewTransform() const;
    QPointF toScene(QPointF widgetPos) const;
    QTransform worldOf(int layer, int frame) const;
    // What is being dragged, for the scene to work with in place of what it has stored.
    PoseOverride dragged() const;
    QTransform parentSeen() const;
    // From the coordinates of the current layer to those of the widget.
    QTransform layerViewTransform() const;
    // Where the pointer is in the coordinates of the current layer: what the drawing tools use.
    QPointF toLayer(QPointF widgetPos) const;
    bool posing() const { return m_drag == DragPoseSkew || m_drag == DragPoseMove || m_drag == DragPoseScale || m_drag == DragPoseRotate || m_drag == DragBone || m_drag == DragIK || m_drag == DragWarp || m_drag == DragPoseTurn; }
    // The pose of a layer on a frame, with the one being dragged in place of the stored one.
    Pose poseOf(int layer, int frame) const;
    Drawing shown(int layer, int frame, Drawing const& drawing) const;
    bool showsRest(int layer) const;
    void riggingDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    // The chain weights are painted for, in the layer's own coordinates: its own bones, or the Game Bones it bends on.
    QVector<QPointF> weightJoints() const;
    // The point or handle of a deformer under the pointer (its place in Layer::warp), or -1.
    int warpAt(QVector<QPointF> const& points, QPointF widgetPos) const;
    // The deformer at rest with the point being dragged by the Rigging tool where the pointer has it.
    QVector<QPointF> warpRestDragged() const;
    void paintWarp(QPainter& painter, QVector<QPointF> const& points, bool closed, int picked);
    void ikDown(QPointF widgetPos);
    // The IK tool on rigid bones: the bone in hand and those it hangs on turn so that its tip follows the pointer.
    void boneIkMove(QPointF widgetPos);
    void paintBones(QPainter& painter, QVector<QPointF> const& joints, int picked);
    QRectF subjectBounds() const;
    // The topmost visible drawing layer with artwork under the point, or -1.
    int layerUnder(QPointF scenePos) const;
    void transformDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers);
    QRectF layerBounds(int layer) const;   // its artwork and that hung under it, in its own space; null with none
    QPointF transformKnob(QRectF const& bounds, QTransform const& toWidget) const;
    QVector<QPair<int, Pose>> posedOthers() const;
    bool penUpLayerPick();
    void drawingPivotDown(QPointF widgetPos);
    void paintDrawingPivot(QPainter& painter);
    void paintStaticCameraAid(QPainter& painter);
    void paintDrawingLabel(QPainter& painter);
    void paintDeformationMesh(QPainter& painter);
    // The line, rectangle or ellipse from where the drag began to `p`.
    void updateShape(QPointF p, Qt::KeyboardModifiers modifiers);
    void contourDown(QPointF p, Qt::KeyboardModifiers modifiers);
    void paintContourAids(QPainter& painter);
    void paintMarquee(QPainter& painter);
    void paintView();
    // Its parts: the frame's edge, grids, guides and each tool's aids (true when nothing more is to be painted), then the selection's.
    void paintViewLayer(QPainter& painter, int l, QRectF const& frameRect, QTransform const& view, bool transforming, bool erasing,
                        std::function<void(int)> const& onionsOf);
    void paintLockedOutline(QPainter& painter, int layer, Drawing const& drawing) const;
    bool paintViewAids(QPainter& painter, QRectF const& frameRect, QTransform const& view);
    void paintSelectionAids(QPainter& painter);
    bool contourValid() const;
    void paintTransformAids(QPainter& painter);
    void penDown(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers);
    void penDownTool(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers);
    void closeMorphMacro();
    bool canEditDrawing() const;
    void cancelInput();
    void penMove(QPointF widgetPos, float pressure, Qt::KeyboardModifiers modifiers);
    void penUp();
    void updateCursor();
    void zoomToolDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers);
    void zoomToolMove(QPointF widgetPos);
    void zoomToolUp();
    void traceDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers);
    void traceMove(QPointF widgetPos);
    void traceUp();
    void paintDesk(QPainter& painter, QTransform const& view) const;
    void paintTraceBox(QPainter& painter, QTransform const& view) const;
    QCursor toolCursor() const;
    void onDocumentChanged();

    // The whole drawing shown in the current cell (both sheets), or null.
    Drawing const* realDrawing() const;
    // The Contour Editor works on the points as this frame has them moved (Animate Points, with Animate on).
    bool pointAnimating() const;
    bool pointsPosed() const;
    void keepWholeDrawing(Drawing const& drawing, QString const& text);
    // A drawing of a layer as the current frame is worked on: with its moved points when they are being animated.
    Drawing const& layerDrawing(int layer, int index) const;
    void commitPointMoves(Drawing const& whole, QString const& text);
    Drawing const* pointViewOf(Drawing const& rest, int index) const;
    // What a drag on the current drawing shows: the paint following its lines (as the keyframe will have it, while points are animated).
    Drawing followedPreview() const;
    Drawing const* currentDrawing() const;
    Drawing selectionDrawing() const;
    // The whole drawing with the sheet worked on replaced by `art`.
    Drawing withArt(Drawing const& art) const;
    void addStrokeToArt(Stroke const& stroke);
    // What the pen drew, as the tool in hand keeps it: a brush shape, or a line.
    void commitPenStroke(Stroke const& stroke, double tolerance);
    void paintIntoBitmap(std::function<void(QPainter&)> const& paint, QString const& text, QRectF reach = QRectF());
    // Rubs `rubbed` out of the pixels of the drawing's pictures (those kept in the scene); true when one was reached.
    bool erasePictures(Drawing& art, QVector<QPolygonF> const& rubbed) const;
    // With a Symmetry guide in use: the turn-over about its axis, for what is on the layer.
    bool symmetry(QTransform* mirror) const;
    QVector<QTransform> symmetries() const;
    // Keeps a line drawn - and, with a Symmetry guide in use, its twin on the other side, as one step.
    void keepStroke(Stroke const& stroke, bool fromPen, double tolerance = 0.0);
    void eraseArtAt(QPointF p, double radius);
    // The current drawing with the transform being dragged applied to the selection.
    Drawing previewDrawing() const;
    void commitDrawing(Drawing const& drawing, QString const& text);

    void paintAt(QPointF scenePos);
    bool fillBitmapAt(QPointF p, bool clear);
    int liftedPieceAt(QPointF p) const;
    // The tip pressed along the stroke (its size `share` of the stroke's thickness at each place), in `brush`.
    void stampTip(QPainter& onto, Stroke const& stroke, double share, QBrush const& brush) const;
    void stampDabs(QPainter& onto, QVector<StrokePoint> const& run, Dabs const& dabs, QBrush const& brush, QImage const& picture) const;
    QRectF selectionBounds() const;
    QPointF handlePos(Handle handle, QRectF const& bounds) const;  // in scene coordinates
    Handle handleAt(QPointF widgetPos) const;
    // Selects (or with `toggle`, flips) the item under the point; false when there is none.
    bool selectAt(QPointF scenePos, bool toggle);
    void selectInRect(QRectF const& rect, bool add);

    Document* m_document;
    // A line that fades along its length, painted: solid into a picture of its own, then made fainter there.
    static void paintFadingStroke(QPainter& painter, Stroke const& stroke, QBrush const& color);
    // After a line is drawn: the invisible stroke that closes it, and its joining to a line it was built on.
    void finishDrawnLine(double reach);
    // A painted area under `line`, the shape it goes round (one step to undo with what is drawn after it).
    void fillUnder(Stroke& line);
    void mergeSelectedLines(bool bridge, QString const& text);
    // A line turned end for end (its handles with it); a line of straight pieces made a curve with straight runs.
    static Stroke reversedLine(Stroke line);
    // A line with the look of `look` and the course and thickness of `before` (to have more joined on after it).
    static Stroke startedFrom(Stroke const& look, Stroke const& before);
    static void makeCurve(Stroke& line);
    QPointF pointMagnet(QPointF p) const;
    QVector<QPair<int, int>> sharedPoints(Drawing const& d) const;
    QVector<QPair<int, int>> stackedPoints(Drawing const& d) const;
    // Where a handle of one of them is in the view (null: it has none that way).
    QPointF stackedHandleKnob(Drawing const& d, int stroke, int point, int handle) const;
    // The line and point whose handle is in hand.
    Stroke* handleLine(Drawing& d, int& point) const;
    bool joinDroppedEnd(Drawing& edited);
    bool jointDroppedPoint(Drawing& edited);
    // Two joints made one (the points of `from` join `into`).
    static void mergeJoints(Drawing& drawing, int into, int from);
    void bendLine(QPointF at);
    void keepShape();
    void retextureSelectedFills(QString const& text, bool kindToo);
    // Whether the tool in hand draws lines that take the pencil texture (the brush and the Stroke tool do not).
    void textureNewLine(Stroke& stroke) const;
    template <class Change> void restyleSelection(QString const& text, Change change);

    void scaleSelectionThickness(double factor, double set, QString const& text);
    // The Contour Editor's copied points (in the drawing's coordinates), for Paste to add after the picked one.
    static QVector<StrokePoint> s_pointClipboard;
    void paintGrid(QPainter& painter, QRectF const& frameRect) const;
    void stepSelection(int direction);

    // penUp's parts, one for each kind of drag: true when the pen's work is all done there.
    bool penUpTool(Drag drag);
    bool penUpDraw(Drag drag);
    bool penUpShape(Drag drag);
    bool penUpSegment(Drag drag);
    bool penUpContourBox(Drag drag);
    bool penUpPoint(Drag drag);
    bool penUpBothHandles(Drag drag);
    bool penUpHandle(Drag drag);
    bool penUpPaint(Drag drag);
    bool penUpPointMarquee(Drag drag);
    bool penUpMarquee(Drag drag);
    bool penUpWeights(Drag drag);
    bool penUpJoint(Drag drag);
    bool penUpWarpRest(Drag drag);
    bool penUpPose(Drag drag);
    bool penUpPivot(Drag drag);
    bool penUpQuad(Drag drag);
    bool penUpSelection(Drag drag);
    void pickColourAt(QPointF widgetPos);
    void paintOnion(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const& tint, double wash = 1.0) const;
    // What a symbol shows on one of its frames, painted where the painter stands (the sheet of the layer that shows it).
    static void paintSymbol(QPainter& painter, Scene const& scene, int symbolId, int frame);
    void alsoInOtherDrawings(QRectF const& region, QTransform const* transform, QString const& text, QSet<int> const* only = nullptr);
    void paintBoneLayers(QPainter& painter);
    void paintQuadAids(QPainter& painter);
    void beginText(QPointF widgetPos, QPointF layerPos, QString const& words = QString());
    void endText(bool keep);
    void transformSelection(QTransform const& about, QString const& text);
    void repositionOthers(QTransform const& done, QString const& text);
    void arrangeSelection(bool front);

    QVector<int> drawingShape() const;
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
    void paintMotionPath(QPainter& painter);
    QPointF snapped(QPointF p) const;
    void selectInside(std::function<bool(QPointF)> const& inside, bool add);
    // The Cutter: what lies inside the shape is cut loose from what lies outside, and selected.
    void cutInside(QPolygonF const& shape);
    // The paint bucket dragged: every line and painted area with a point inside is painted (or unpainted).
    void paintInside(std::function<bool(QPointF)> const& inside, QRectF const& reach);

    void keepStamp(QVector<Drawing> const& drawings);
    Drawing stampNow() const;
    void stampAt(QPointF p);
    // The Line tool and the Polyline: the thickness stencil laid on the line drawn, as its thickness along it.
    void stencilLine(Stroke& line) const;
    void thicknessPointDown(QPointF p, Qt::KeyboardModifiers modifiers);
    void thicknessDown(QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void inkAt(QPointF p);
    QPointF splineOffset() const;
    void repaintTo(QPointF p, double tolerance);
    void smoothAt(QPointF p);
    void retouchAt(QPointF p, int mode);
    QVector<QPointF> selectionEnvelope() const;
    bool envelopeDown(QPointF widgetPos);
    void envelopeMove(QPointF p);
    // The Morphing tool.  The morphings that have a key drawing on this frame: which, and whether it is their second.
    QVector<QPair<int, bool>> morphsHere() const;
    void hintDown(QPointF widgetPos, QPointF p, Qt::KeyboardModifiers modifiers);
    void hintUp();
    void paintMorphHints(QPainter& painter);
public:
    // Where a point of that outline is in the view (a null point when there is none): to take hold of it.
    QPointF envelopePointInView(int point) const;
    // A handle of the point of the outline pressed last (the one before it, or the one after); null when none shows.
    QPointF envelopeHandleInView(bool after) const;
private:
    QPointF endMagnet(QPointF p) const;
    bool keepJoined(Stroke const& line);
    Drawing followLines(Drawing whole, Drawing const* before, bool quick = false) const;
    bool startBend(Stroke const& line, QPointF p, Qt::KeyboardModifiers modifiers);     // a press on a curve: bent if dragged
    // Auto-Create Colour Art for a line kept by a shape tool or the Polyline: its twin, with no thickness, in the colour art.
    void colourArtTwin(Stroke const& line);
    QTransform contourBoxTransform() const;     // what the drag does to the points picked
    // Cuts what the eraser passed over (the line `m_stroke`, as thick as the eraser) out of the drawing.
    void cutWithEraser();
    void eraseStretch(int from);    // cuts the eraser's way from that point on out of m_eraseArt
    bool gradientValid() const;
    void gradientDown(QPointF widgetPos, QPointF p);
    void paintGradientAids(QPainter& painter);
    // The curve the Contour Editor is working on, in a drawing; null when there is none.
    Stroke const* contourLine(Drawing const& drawing) const;
    Stroke* contourLine(Drawing& drawing) const;
    // After that curve was changed: a brush shape draws its outline out again.
    void contourEdited(Drawing& drawing) const;
    // Alt-pressed on a point: whether the pen has gone far enough from it to pull its handles out (or only clicked).
    bool pulledHandles() const { return std::hypot(m_dragNow.x() - m_dragStart.x(), m_dragNow.y() - m_dragStart.y()) * m_zoom > 3.0; }

};
