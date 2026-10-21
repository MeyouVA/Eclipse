// Eclipse - the drawing view: shows the current frame and takes pen / mouse input.
#pragma once

#include <cmath>

#include "EclipseExport.h"

#include <QTransform>
#include <QFont>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <QPainterPath>

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
        // Rubbed over painted areas and brush shapes, it gives the part of them it covers the colour in hand: the rest
        // keeps its colour.  Lines are left alone (the Ink tool is for those).
        RepaintBrush,
        // Dragged anywhere, the current layer's whole motion path goes with the pen: every keyframe and control point.
        SplineOffset,
        // Dragged across, the current layer turns about the line up it; dragged up or down, about the line across it
        // (in space, as Layer Properties' turn across and up): keyframed on the current frame.
        Turn3D,
        // Rubbed along pencil lines, it makes them thicker there (or thinner: its mode, or Alt), more the longer it is rubbed.
        PencilRetouch,
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
    // A second view (the Drawing window) works with the tool and the settings of the main one: they are taken from
    // `leader` each time the pen goes down here.
    void setLeader(Canvas* leader);
    void followSettings(Canvas const& leader);
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
    // Preferences: the Brush, the Eraser and the Pencil show a circle as big as what they draw round the pointer.
    void setBrushOutline(bool on);
    bool brushOutline() const { return m_brushOutline; }
    double brushWidth() const { return m_brushWidth; }
    // The pen stabiliser of the Brush and the Pencil: the line follows the pen at a distance, smoothing out its shake.
    // Catch Up: when the pen lifts, the line is carried on to where it was; No Catch Up: it ends where it had got to.
    enum Stabiliser { StabiliserOff, StabiliserCatchUp, StabiliserNoCatchUp };
    void setStabiliser(int mode) { m_stabiliser = std::clamp(mode, 0, 2); }
    int stabiliser() const { return m_stabiliser; }
    // 0 to 100: how far behind the pen the line keeps.
    void setStabiliserStrength(int strength) { m_stabiliserStrength = std::clamp(strength, 0, 100); }
    int stabiliserStrength() const { return m_stabiliserStrength; }
    // Where the line goes next, from where it is towards the pen, at that strength.
    static QPointF stabilised(QPointF line, QPointF pen, int strength);
    // The Line tool: a straight line; or, once dragged, a curve - bent by moving the pointer, put down by a click - that
    // bows one way (Curve) or both ways (S-Curve).
    enum LineMode { LineStraight, LineCurve, LineSCurve };
    void setLineMode(int mode) { m_lineMode = std::clamp(mode, 0, 2); }
    int lineMode() const { return m_lineMode; }
    bool bendingLine() const { return m_bending; }
    // The thickness stencils of the Pencil: how thick a new pencil line is along its length.
    enum Stencil { StencilUniform, StencilTapered, StencilTaperedStart, StencilTaperedEnd, StencilMediumTapered, StencilPointy, StencilFatEnds,
                   StencilCurvedAbove, StencilCurvedBelow, StencilCount };
    void setThicknessStencil(int stencil) { m_stencil = std::clamp(int(stencil), 0, int(StencilCount)); }
    // A stencil of your own (StencilCount): the thickness at evenly spaced places from the start of the line to its end.
    void setCustomStencil(QVector<float> const& profile) { m_customStencil = profile; }
    static float profileThickness(QVector<float> const& profile, double along);
    static void applyProfile(Stroke& stroke, QVector<float> const& profile);
    // The thickness of the first line selected along its length (32 places), for a stencil of your own; empty with none.
    QVector<float> selectedLineProfile() const;
    // The Pencil Retouch tool: thicker (0) or thinner (1) where it is rubbed; more solid (2) or fainter (3).
    void setRetouchMode(int mode) { m_retouchMode = std::clamp(mode, 0, 3); }
    int retouchMode() const { return m_retouchMode; }
    // Preferences: how strongly pencil lines show in the views (per cent; what is exported is not changed).
    void setPencilLinesOpacity(int percent) { m_pencilOpacity = std::clamp(percent, 0, 100); update(); }
    // The lines selected made wiggly: bent this far to each side (scene units), every so far along; one step to undo.
    void wiggleSelectedLines(double amount, double every, quint32 seed);
    int thicknessStencil() const { return m_stencil; }
    // A stencil's thickness at `along` (0: the start of the line, 1: its end), as a share of the full thickness.
    static float stencilThickness(int stencil, double along);
    // The stencil laid on a line: each point as thick as the stencil says at its place along it.
    static void applyStencil(Stroke& stroke, int stencil);
    // How a line drawn with the pen is finished (Tool Properties of the Brush, the Pencil and the Stroke tool):
    //  Start / End Taper: the Brush's line thins to a point over that share of its length (per cent, 0: none);
    //  Speed Sensitivity: the faster the pen, the thinner the Brush's line (inverted: the thicker);
    //  Trim Extra Lines: an end that runs a little past a line it crosses is cut off there;
    //  Connect Line Ends: an end that stops a little short of a line is carried on to touch it;
    //  Auto Close Gap: a line whose ends nearly meet is closed by an invisible stroke (paint stays in);
    //  Line Building: a line begun or ended at the end of a line of its colour is joined to it, one line;
    //  Straight Lines: the Stroke tool draws straight pieces, not curves.
    struct LineOptions {
        int startTaper = 0, endTaper = 0, speed = 0;
        // Fade Distance: the Brush's line thins away to nothing over so far from where it began (scene units; 0: no fade).
        int fade = 0;
        bool speedInvert = false, trim = false, connect = false, closeGap = false, building = false, straight = false;
    };
    void setLineOptions(LineOptions const& options) { m_lineOptions = options; }
    // Copy Art: the lines and areas of one art layer copied into another (lines as lines with no thickness, when the
    // other is the colour art), as Art Layer > Copy Art Settings... says; Create Colour Art from Line Art is its usual case.
    struct CopyArt {
        int from = 2, to = 1;           // Drawing::Sheet: Line art into colour art
        bool clearFirst = false, allDrawings = false;
    };
    void setCopyArt(CopyArt const& how) { m_copyArt = how; }
    CopyArt const& copyArt() const { return m_copyArt; }
    // Points where lines cross: a point put on each line at each place two of them cross (lying on one another - in the
    // Contour Editor they move as one).  The number of places.
    static int addCrossingPoints(QVector<Stroke>& strokes, QVector<int> const& which);
    bool autoColourArt() const { return m_autoColourArt; }
    LineOptions const& lineOptions() const { return m_lineOptions; }
    // The steps, for the tests too: on the pen's samples (before they are fitted), and in `art` (the drawing it goes into).
    static void applyTaperAndSpeed(Stroke& stroke, LineOptions const& options, double zoom);
    static bool trimExtra(Stroke& stroke, Drawing const& art, double reach);
    static bool connectEnds(Stroke& stroke, Drawing const& art, double reach);
    // The Brush and the Pencil: a line whose ends meet is painted inside with the colour in hand.
    void setLineAutoFill(bool on) { m_lineAutoFill = on; }
    bool lineAutoFill() const { return m_lineAutoFill; }
    // The Select tool: lines scaled get thicker or thinner with it (off: they keep their thickness).
    void setScaleThickness(bool on) { m_scaleThickness = on; }
    bool scaleThickness() const { return m_scaleThickness; }
    // The Rectangle and the Ellipse: the inside is painted with the colour in hand as the shape is drawn.
    void setShapeAutoFill(bool on) { m_shapeAutoFill = on; }
    bool shapeAutoFill() const { return m_shapeAutoFill; }
    // How much a drawn line is smoothed when the pen lifts: 0 keeps every wobble, 100
    // irons out a lot.
    void setSmoothing(int percent) { m_smoothing = percent; }
    // The Brush: how thick its line is where the pen barely touches, as a percentage of its size
    // (pressing harder goes from there up to the full size).
    int minimumBrushSize() const { return m_minSize; }
    void setMinimumBrushSize(int percent) { m_minSize = percent; }
    // The Brush on a bitmap layer: how hard its edge is (100: crisp; less: it fades out over that much of the way in)
    // and how much paint it puts down at once (its flow, per cent).
    void setBitmapHardness(int percent) { m_bitmapHardness = std::clamp(percent, 0, 100); }
    void setBitmapFlow(int percent) { m_bitmapFlow = std::clamp(percent, 1, 100); }
    int bitmapHardness() const { return m_bitmapHardness; }
    // The Brush on a bitmap layer: the shape of its tip, pressed down again and again along the stroke.
    enum BitmapTip { BitmapRound, BitmapSquare, BitmapChisel, BitmapSpray, BitmapImage };
    BitmapTip bitmapTip() const { return m_bitmapTip; }
    void setBitmapTip(BitmapTip tip) { m_bitmapTip = tip; }
    int bitmapFlow() const { return m_bitmapFlow; }
    // What the tool in hand makes of how hard the pen is pressed (0 to 1).
    float pressureFor(float raw) const
    {
        float const least = m_tool == Brush ? float(m_minSize) / 100.0f : m_tool == Eraser ? float(m_eraserMinSize) / 100.0f : 0.0f;
        return least + (1.0f - least) * feltPressure(raw);
    }
    // The Brush with a pen that tells its tilt: the more the pen leans, the thicker the line (Tilt Sensitivity, per cent).
    void setTiltSensitivity(int percent) { m_tiltSensitivity = std::clamp(percent, 0, 100); }
    int tiltSensitivity() const { return m_tiltSensitivity; }
    static float tiltedPressure(float pressure, double xTilt, double yTilt, int sensitivity)
    {
        double const lean = std::clamp(std::hypot(xTilt, yTilt) / 60.0, 0.0, 1.0);
        return float(pressure * (1.0 + sensitivity / 100.0 * lean * 1.5));
    }
    // Preferences: the feel of the pen - 50 as it presses, more for a light touch (a little pressure goes a long way),
    // less for a heavy one.
    void setPressureFeel(int feel) { m_pressureFeel = std::clamp(feel, 1, 100); }
    int pressureFeel() const { return m_pressureFeel; }
    float feltPressure(float raw) const
    {
        return std::pow(std::clamp(raw, 0.0f, 1.0f), std::pow(2.0f, (50.0f - float(m_pressureFeel)) / 25.0f));
    }
    // Turns sharper than this stay corners; gentler ones are rounded.
    void setCornerAngle(int degrees) { m_cornerAngle = degrees; }
    // How new lines are finished at corners and ends.  Lines that are selected (or shown
    // in the contour editor) are changed too.
    void setJoinStyle(int join);
    void setStartCap(int cap);
    void setEndCap(int cap);
    // The pencil texture new lines are drawn with (0: solid ink), and how much of it shows (per cent).
    // Lines that are selected are changed too.
    void setLineTexture(int texture);
    int lineTexture() const { return m_lineTexture; }
    void setLineTextureAmount(int percent);
    int lineTextureAmount() const { return m_lineTextureAmount; }
    // A crisp edge: the grain frays at the edge of the line but does not fade there, and is not blurred.
    void setLineTextureCrisp(bool crisp);
    bool lineTextureCrisp() const { return m_lineTextureCrisp; }
    // The paper texture of the brush: the grain its strokes show (0: solid paint), and how much of it.
    // Brush strokes that are selected are changed too.
    void setBrushTexture(int texture);
    int brushTexture() const { return m_brushTexture; }
    void setBrushTextureAmount(int percent);
    int brushTextureAmount() const { return m_brushTextureAmount; }
    void setBrushTextureCrisp(bool crisp);
    // The ramp of the texture, per cent: grain fainter than `low` is no ink, denser than `high` solid ink.
    void setLineTextureRamp(int low, int high);
    int lineTextureLow() const { return m_lineRampLow; }
    int lineTextureHigh() const { return m_lineRampHigh; }
    void setBrushTextureRamp(int low, int high);
    int brushTextureLow() const { return m_brushRampLow; }
    int brushTextureHigh() const { return m_brushRampHigh; }
    bool brushTextureCrisp() const { return m_brushTextureCrisp; }
    void setOnionSkin(bool on);
    bool onionSkin() const { return m_onionSkin; }
    // The Drawing view: the drawing of the current cell alone and flat - not moved by its
    // layer, its pegs or the camera - on white paper.  The other layers show, faint and as
    // flat, only with the light table on.
    bool drawingView() const { return m_drawingView; }
    void setDrawingView(bool on);
    int gridKind() const { return m_gridKind; }
    // Preferences: how many fields the standard field grid has from the middle to each side, across and down (12 each).
    void setFieldChart(int across, int down)
    {
        m_fieldsAcross = std::clamp(across, 2, 64);
        m_fieldsDown = std::clamp(down, 2, 64);
        update();
    }
    int fieldsAcross() const { return m_fieldsAcross; }
    int fieldsDown() const { return m_fieldsDown; }
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
    // The Select tool and the Cutter take from the line art and the colour art at once.
    Q_INVOKABLE void setSelectBothArts(bool on);
    bool selectBothArts() const { return m_selectBothArts; }
    // ... which is so just now (with one of those tools, on one of those two sheets).
    bool bothArtsNow() const { return m_selectBothArts && (m_tool == Select || m_tool == Cutter) && (m_art == LineArt || m_art == ColourArt); }
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
    // Control points of the current layer's motion path.  `key`: the keyframe the stretch starts at.
    // Where one is in the view; which one is at a place of the view; and where in the layer's own
    // measure a place of the view is, for that control point.
    QPointF controlPointPos(int key, int control) const;
    bool controlPointAt(QPointF widgetPos, int* key, int* control) const;
    QPointF controlPointFromView(int key, int control, QPointF widgetPos) const;
    // The one picked last (-1, -1: none).
    int pickedControlKey() const { return m_cpKey; }
    int pickedControl() const { return m_cpIndex; }
    void pickControlPoint(int key, int control);
    // From the scene to the view (for checking where things are shown).
    QTransform viewTransformForTest() const { return viewTransform(); }
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
    // The Stamp tool: each stamp made smaller by up to this much at random (per cent; 0: all as big as the scale says).
    void setStampRandomSize(int percent) { m_stampRandomSize = std::clamp(percent, 0, 90); }
    int stampRandomSize() const { return m_stampRandomSize; }
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
    // From Preferences: how many times round the chain the IK tool goes at most, and how near the pointer the tip must come
    // for it to stop (scene units).
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
    enum RigMode { RigBones, RigCurve, RigEnvelope, RigWeights };
    // Weights: the bone (0: the first) whose pull is painted, how big the dab is (scene units), and how strongly each one adds.
    int weightBone() const { return m_weightBone; }
    void setWeightBone(int bone) { m_weightBone = std::max(0, bone); update(); }
    double weightRadius() const { return m_weightRadius; }
    void setWeightRadius(double radius) { m_weightRadius = std::max(1.0, radius); }
    void setWeightStrength(double strength) { m_weightStrength = std::clamp(strength, 0.01, 1.0); }
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
    // How the Camera view shows the frame: as one works on it (Normal), as it will be rendered (Render: no onion skin,
    // guides or tool marks), its matte (white where it is painted, black where it is clear), or its depth (each layer as
    // light as it is near the camera).
    enum ViewMode { ViewNormal, ViewRender, ViewMatte, ViewDepth };
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode) { m_viewMode = mode; update(); }
    // The frame as that mode shows it, the size of the scene's frame.
    static QImage modeImage(Scene const& scene, int frame, ViewMode mode);
    // Snapshots: the frame as rendered, kept to compare later work with - shown in the view in place of the frame.
    void takeSnapshot();
    bool hasSnapshot() const { return !m_snapshot.isNull(); }
    QImage snapshot() const { return m_snapshot; }
    bool showsSnapshot() const { return m_showSnapshot; }
    void setShowSnapshot(bool on) { m_showSnapshot = on && hasSnapshot(); update(); }
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
    bool zoomsOnMouse() const { return m_zoomOnMouse; }
    void setZoomOnMouse(bool on) { m_zoomOnMouse = on; }
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
    bool showsSymbolPivots() const { return m_symbolPivots; }
    // Where the pivot of a layer is in the view on the current frame.
    QPointF pivotInView(int layer) const;
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
    // Preferences: onion skins in which the lighter a colour is, the more see-through it is (light fills fade away, the
    // dark lines stay).
    void setOnionBrightnessAlpha(bool on)
    {
        m_onionBrightnessAlpha = on;
        update();
    }
    bool onionBrightnessAlpha() const { return m_onionBrightnessAlpha; }
    // Preferences: the onion skins over everything rather than under the drawing they are of.
    void setOnionOver(bool on)
    {
        m_onionOver = on;
        update();
    }
    bool onionOver() const { return m_onionOver; }
    bool onionByFrame() const { return m_onionByFrame; }
    void setOnionColours(QColor const& before, QColor const& after);
    QColor onionBefore() const { return m_onionBefore; }
    QColor onionAfter() const { return m_onionAfter; }
    // The colour of the view around the frame of the camera (Preferences > Colours).
    void setBackdrop(QColor const& colour);
    QColor backdrop() const { return m_backdrop; }
    // The light table: how strongly the layers that are not being drawn on show, 10 to 90 per cent.
    void setLightTableWash(int percent);
    int lightTableWash() const { return m_lightWash; }
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
    // ... or the line whose gradient it has in hand (-1: none, or an area).
    int gradientLinePicked() const { return gradientValid() ? m_gradStroke : -1; }

    // The thickness of the point picked with the Contour Editor, as a percentage of the
    // width of its line; -1 when no point is picked.
    int pointThickness() const;
    void setPointThickness(int percent);
    static bool canPaste();
    // The sheet of a drawing an art layer of the view stands for (Drawing::Sheet).
    static int sheetOf(Art art);

    // Renders one frame of a scene the way the canvas shows it, without the editing aids.
    static void paintFrame(QPainter& painter, Scene const& scene, int frame);
    // Preferences (Render): blurs and focus (depth of field) effects worked out, or let through as they are - everywhere a
    // frame is drawn - for speed while working.
    static void setBlursEnabled(bool on);
    static bool blursEnabled();
    static void setFocusEnabled(bool on);
    static bool focusEnabled();
    // The frame as the scene's node graph puts it together (its Display's picture, over the paper); false, nothing
    // painted, when the scene has no graph wired (or no Display in it).
    static bool paintGraph(QPainter& painter, Scene const& scene, int frame);
    static void paintDrawing(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity,
                             QColor const* tint = nullptr);
    // The drawing of a layer with the effects of the layer applied.  The painter is set up
    // for the coordinates of the layer; `sceneToDevice` is what it would be for those of
    // the scene (the matte of a cutter is another layer, found from there).
    // (`everySheet`: the art layers the layer leaves out of the picture are painted all the same.)
    static void paintLayerDrawing(QPainter& painter, Scene const& scene, int layer, int frame, Drawing const& drawing,
                                  double opacity, QTransform const& sceneToDevice, bool everySheet = false);
    static void paintStroke(QPainter& painter, Stroke const& stroke, QBrush const& color);
    // How strongly lines are painted just now (Preferences: Pencil Lines Opacity, while the view is painted).
    static inline double s_lineOpacity = 1.0;
    static void paintFill(QPainter& painter, Fill const& fill, QBrush const& color);
    // The bitmap brush's tip, as its editor (Tool Properties: Edit Tip...) sets it: turned, flattened, flipped; how far apart
    // its dabs are and how many at each place; how much each of these varies at random; and how faint it is where the pen
    // barely presses.
    struct TipShape {
        int angle = 0;              // degrees
        int roundness = 100;        // per cent: how tall the tip is beside how wide
        int spacing = 0;            // per cent of the size from dab to dab (0: as the tip has it)
        int count = 1;              // dabs at each place
        bool flipX = false, flipY = false;
        int randomAngle = 0, randomSpacing = 0, randomCount = 0, randomSize = 0, randomOffset = 0;
        int minOpacity = 100;       // per cent of solid at the lightest pressure (100: pressure leaves it solid)
        // The tip turns with the stroke: Angle is then from the way the stroke goes.
        bool followDirection = false;
        // How the paint goes onto the pixels already there: over them; only where there is paint already (a stencil:
        // its colour changes, its shape does not); behind them; or taking them away (an eraser with this tip).
        enum Mode { Normal, Stencil, Behind, Erase };
        int mode = Normal;
        // A second tip (Dual Tip): the stroke shows only where it, stamped along the same way, leaves its mark.
        bool dual = false;
        int dualKind = 0;           // BitmapRound, BitmapSquare, BitmapChisel, BitmapSpray, BitmapImage (the dual tip picture)
        int dualSize = 50;          // per cent of the brush's size
        int dualSpacing = 25, dualCount = 1, dualRoundness = 100, dualAngle = 0;
        bool dualFlipX = false, dualFlipY = false;
        int dualRandomAngle = 0, dualRandomSpacing = 0, dualRandomCount = 0, dualRandomSize = 0, dualRandomOffset = 0;
        bool engaged() const
        {
            return angle != 0 || roundness != 100 || spacing != 0 || count != 1 || flipX || flipY || randomAngle != 0 || randomSpacing != 0
                   || randomCount != 0 || randomSize != 0 || randomOffset != 0 || minOpacity != 100 || followDirection || mode != Normal || dual;
        }
    };
    void setTipShape(TipShape const& shape) { m_tipShape = shape; }
    TipShape const& tipShape() const { return m_tipShape; }
    // A tip of your own (BitmapImage): a picture whose light parts paint (and whose clear parts do not).
    void setTipImage(QImage const& image) { m_tipImage = image; }
    // The dual tip's picture (a dualKind of BitmapImage).
    void setDualTipImage(QImage const& image) { m_dualImage = image; }
    QImage const& tipImage() const { return m_tipImage; }
    // The tips of a Photoshop brush file (.abr, versions 6 to 10: its sampled brushes), as pictures; none when it is not one.
    static QVector<QImage> readAbr(QString const& path, QString* why = nullptr);
    // The lines and areas of one art layer of a drawing painted into that art layer's pixels (made if it has none), and
    // taken away: the art layer is a bitmap one from then on.  `fineness`: pixels for each unit of the scene; `canvas`: how
    // much of the scene's frame the pixels cover (1.5: the frame and a quarter more on every side).
    static void rasterize(Drawing& drawing, int sheet, Scene const& scene, double fineness, QRectF const& canvas);
    // The painted pixels of a drawing made finer or coarser by `factor` (2: twice as many each way); the picture stays as big.
    static void resampleBitmaps(Drawing& drawing, double factor);
    // Preferences: how much of the scene's frame a new drawing's pixels cover, per cent (150: the frame and a quarter more
    // on every side).
    void setBitmapCanvas(int percent) { m_bitmapCanvas = std::clamp(percent, 100, 400); }
    // Preferences: the new pixels' canvas as wide and as tall as so much of the area it is based on (per cent, apart), how
    // fine they are (ppi: 72 is a pixel for each unit of the scene, at the layer's own resolution of 100 %), and what the area
    // is - the scene's frame; the area the camera covers on the frame; or all it covers as it moves over the scene.
    enum CanvasBasis { CanvasScene, CanvasCamera, CanvasCameraMove };
    void setBitmapCanvasSize(int widthPercent, int heightPercent)
    {
        m_bitmapCanvas = std::clamp(widthPercent, 100, 400);
        m_bitmapCanvasHeight = std::clamp(heightPercent, 100, 400);
    }
    void setBitmapPpi(int ppi) { m_bitmapPpi = std::clamp(ppi, 18, 1200); }
    void setBitmapCanvasBasis(int basis) { m_bitmapBasis = std::clamp(basis, 0, 2); }
    // Where the pixels of a new bitmap drawing of the current layer lie, on the layer; and how many to a unit of the scene.
    QRectF bitmapArea() const;
    double bitmapFineness() const;
    // The master switch of snapping (Tool Properties of the Select tool, the Contour Editor, the Envelope and the
    // Perspective tool): off, nothing snaps - to the grid, to contours and points, to guides, or into line with the rest -
    // whatever each of those is set to.
    void setSnapping(bool on) { m_snapping = on; }
    bool snapping() const { return m_snapping; }
    // The Select tool: no box and no handles round what is selected (it is still moved by dragging it, and by the keys).
    void setHideManipulator(bool on) { m_hideManipulator = on; update(); }
    bool hidesManipulator() const { return m_hideManipulator; }
    // The Envelope tool: the handles of the outline's points (its advanced controls), shown and pulled, or not.
    void setEnvelopeAdvanced(bool on) { m_envAdvanced = on; update(); }
    bool envelopeAdvanced() const { return m_envAdvanced; }
    // The Contour Editor's controls: with two points or more picked, a box round them - a corner pulled scales them
    // (from the corner across from it), the knob over it turns them about its middle.
    void setContourControls(bool on) { m_ceControls = on; update(); }
    bool contourControls() const { return m_ceControls; }
    // That box on the layer (null when it is not shown); its corners (0 to 3: from the top left, clockwise) and its knob (4)
    // in the view.
    QRectF contourControlBox() const;
    QPointF contourControlInView(int which) const;
    // The Cutter's gestures (Use Mouse Gesture): a quick straight stroke across lines - not round them - takes away the
    // piece of the first line it crosses between the lines that cross it (Single Line), of every line it crosses (Multiple
    // Lines), or cuts each line it crosses in two there (Breaker: the line becomes two lines).
    enum CutterGesture { GestureOff, GestureSingle, GestureMultiple, GestureBreak };
    void setCutterGesture(int gesture) { m_cutterGesture = std::clamp(gesture, 0, 3); }
    int cutterGesture() const { return m_cutterGesture; }
    // How the Cutter leaves the ends of a pencil line it cuts: cut square (Stroke::FlatCap, as Harmony's bevel tip) or round.
    void setCutterTip(int cap) { m_cutterTip = std::clamp(cap, 0, 1); }
    int cutterTip() const { return m_cutterTip; }
    // The gesture from a to b done on the lines of `drawing` (a CutterGesture); whether anything changed.
    static bool gestureCut(Drawing& drawing, QPointF a, QPointF b, int gesture, int tip);
    // Whether a path drawn with the Cutter is a gesture: long enough on the screen, and near enough straight.
    static bool isGesture(QPolygonF const& path, double zoom);
    // Nudge Selection Thickness: the lines selected a little thicker or thinner (Alt and the up or down arrow; a lot with
    // Shift too).
    void nudgeSelectionThickness(bool thicker, bool more);
    // The Pencil's ways of drawing over a line already there (Tool Properties): Line Pushing - a stroke drawn over a stretch
    // of the line moves that stretch onto it; Auto Adjust Thickness - the stretch is made thicker, out to the stroke.  The
    // stroke is shown in the highlight colour while it is drawn, and is not kept.
    enum PencilMode { PencilDraw, PencilPush, PencilThicken };
    void setPencilMode(int mode) { m_pencilMode = std::clamp(mode, 0, 2); }
    int pencilMode() const { return m_pencilMode; }
    void setPushHighlight(QColor const& colour) { m_pushHighlight = colour; }
    QColor pushHighlight() const { return m_pushHighlight; }
    // For the tests too: the stroke `highlight` (its points as the pen made them) done to the line of `drawing` it runs along
    // (within `reach` of its edge); whether there was one.
    static bool pushLine(Drawing& drawing, Stroke const& highlight, int mode, double reach);
    // The Polyline tool: its lines go under everything else of the art layer (Draw Behind Current Layers).
    void setPolylineBehind(bool on) { m_polyBehind = on; }
    bool polylineBehind() const { return m_polyBehind; }
    // Edit Pencil Lines Properties: a line made thicker or thinner by `multiplier`, its tips (where it thins to its ends) too
    // or not - not, they keep their thickness at the very ends and grow towards the body; and the tips grown by
    // `tipExtra` besides (1: no more), never past the thickest of the line.
    static void multiplyThickness(Stroke& line, double multiplier, bool tips, double tipExtra);
    // The Contour Editor: a line taken hold of between two of its points and dragged bends there - the stretch between
    // the two points follows the pen, the handles of the two points pulled out to make it so.  For the tests too: the piece
    // of `line` (from its point `segment` to the next) nearest `p`, and how far along it (0 to 1); false when none is
    // within `reach`.  And the line with that place on that piece moved by `by`.
    // A point well inside an area (outlines, odd-even); null for none.
    static QPointF insideOf(QVector<QPolygonF> const& outlines);
    static bool segmentAt(Stroke const& line, QPointF p, double reach, int& segment, double& t);
    static Stroke bentSegment(Stroke line, int segment, double t, QPointF by);
    // The Eraser with a pen: how wide it rubs where the pen barely touches, per cent of its size (100: pressure leaves it be).
    void setEraserMinimumSize(int percent) { m_eraserMinSize = std::clamp(percent, 0, 100); }
    int eraserMinimumSize() const { return m_eraserMinSize; }
    // Smoothing with pressure (the Brush and the Pencil): the Smoothing set is the most, at full pressure; at the lightest
    // the line is smoothed `minimumPercent` of that - between, as hard as the pen pressed on the whole.
    void setPressureSmoothing(bool on, int minimumPercent)
    {
        m_pressureSmoothing = on;
        m_minSmoothing = std::clamp(minimumPercent, 0, 100);
    }
    // The smoothing for a line drawn pressing so hard on the whole (0 to 1).
    int smoothingFor(double pressure) const
    {
        if (!m_pressureSmoothing)
            return m_smoothing;
        double const share = m_minSmoothing / 100.0 + (1.0 - m_minSmoothing / 100.0) * std::clamp(pressure, 0.0, 1.0);
        return int(std::lround(m_smoothing * share));
    }
    // The Pencil with a pen: how solid its line is where the pen barely presses (per cent; 100: pressure leaves it solid),
    // and how much those changes are smoothed along the line (Opacity Smoothing, per cent).
    void setPencilOpacity(int minimumPercent, int smoothing)
    {
        m_pencilMinOpacity = std::clamp(minimumPercent, 0, 100);
        m_opacitySmoothing = std::clamp(smoothing, 0, 100);
    }
    static void smoothOpacity(Stroke& line, int percent);
    // The Smooth Editor and the Pencil Retouch tool: Apply to Single Stroke - a rub works on the first line it touches only
    // (Shift, held as the pen goes down, the other way about).
    void setSingleStroke(bool on) { m_singleStroke = on; }
    bool singleStroke() const { return m_singleStroke; }
    // Edge Type of what the shape tools draw on a bitmap layer: smooth (antialiased) or hard (every pixel all or nothing).
    void setHardEdges(bool on) { m_hardEdges = on; }
    bool hardEdges() const { return m_hardEdges; }
    // Overlay Brush Mode: what the Brush and the Pencil draw is kept only where the artwork of the layers chosen (`masks`
    // says which: the layers selected, the current one left out) is - a mask for the new strokes.
    void setMaskLayers(std::function<QVector<int>()> masks) { m_maskLayers = std::move(masks); }
    bool overlayBrush() const { return m_overlayBrush; }
    // The artwork of those layers on this frame, on the current layer (empty with none).
    QPainterPath overlayMask() const;
    // What is selected moved by so much, as one step (with Reposition All Drawings: all of every drawing of the layer).
    void moveSelectionBy(QPointF by, QString const& text) { transformSelection(QTransform::fromTranslate(by.x(), by.y()), text); }

public slots:
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
    // The Morphing tool: the morphing it works on here (its place among the current layer's; -1 when the frame is not the
    // first or the last drawing of one), and the morphing layer of it that Ctrl+click puts shapes in or takes them out of
    // (-1: none).  A shape of the drawing under a place of it (Morphing::shapeKey), or -1.
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
    void deleteSelection();
    void selectAll();
    void deselectAll() { clearSelection(); }
    void onActionZoomIn() { zoomByShortcut(1.25); }
    void onActionZoomOut() { zoomByShortcut(0.8); }
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
    // Smooth Fast: a lighter smoothing of what is selected.  Pump / Deflate Selection Thickness: the lines selected a quarter
    // thicker or thinner; Set Selection Thickness: so thick.  Delete Non Selection: all of the drawing but what is selected
    // goes.
    void onActionSmoothFast();
    void onActionPumpThickness();
    // The pencil lines selected made one line, end to end - Merge across any gap, Join only where their ends meet; the
    // Contour Editor's line cut in two at its picked point; the thickness of the lines selected turned end for end.
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
    // Preferences: where Paste puts what it pastes - where it was cut or copied from (0), always in the middle of the view
    // (1), or in the middle when it would land out of sight (2).
    void setPasteBehaviour(int how) { m_pasteBehaviour = std::clamp(how, 0, 2); }
    int pasteBehaviour() const { return m_pasteBehaviour; }
    // The view moved so that what is under the pointer is in the middle of it.
    void onActionRecentre();
    // Quick Close-up: a close-up of what is under the pointer (four times as near, in the middle); again, back to the view
    // as it was.
    void onActionQuickCloseup();
    // What is selected, in the middle of the view and as big as it will go.
    void onActionFrameSelection();
    void onActionSnapAndAlign(bool on) { m_snapAlign = on; emit toolSettingsChanged(); }
    void onActionAutoFillInside(bool on) { m_lineAutoFill = on; emit toolSettingsChanged(); }
    bool inQuickCloseup() const { return m_closeup; }
    // The same, about a place of the view rather than the pointer.
    void recentreOn(QPointF widgetPos);
    void quickCloseupAt(QPointF widgetPos);
    // Preferences: the middle mouse button pans the view (on), or does nothing here; and the time each picture of the
    // view takes to draw, shown in its corner (the Camera View performance report).
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
    void onActionShowSymbolPivot(bool on);
    // Show > BBox Selection Style: what is selected is told by its box alone - no line drawn over each thing.
    void onActionBBoxSelectionStyle(bool on)
    {
        m_bboxSelection = on;
        update();
    }
    bool bboxSelectionStyle() const { return m_bboxSelection; }
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
    // Flatten on the whole drawing (nothing need be selected), on every drawing of the layer, and Flatten then Remove
    // Extra Strokes.
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
    // Art Layer > Auto-Create Colour Art from Brush: a line drawn in the line art makes a line with no thickness on the
    // same points in the colour art, so that it bounds the paint there too.
    void onActionAutoColourArt(bool on) { m_autoColourArt = on; }
    void onActionCopyArt();
    void onActionCopyArtSettings();
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
    void controlPointPicked(int key, int control);
    void selectBothArtsChanged(bool on);
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
    // Another layer or frame: the morphing the Morphing tool works on may be another.
    void morphHereChanged();
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
    bool posing() const { return m_drag == DragPoseSkew || m_drag == DragPoseMove || m_drag == DragPoseScale || m_drag == DragPoseRotate || m_drag == DragBone || m_drag == DragIK || m_drag == DragWarp || m_drag == DragPoseTurn; }
    // The pose of a layer on a frame, with the one being dragged in place of the stored one.
    Pose poseOf(int layer, int frame) const;
    // A drawing as it is shown: bent along the bones of its layer, except on the layer
    // being drawn on, which is shown at rest so that the pen lands where it is put.
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
    void paintMarquee(QPainter& painter);
    void paintView();
    bool contourValid() const;
    void paintTransformAids(QPainter& painter);
    void penDown(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers);
    void penDownTool(QPointF widgetPos, float pressure, bool panning, Qt::KeyboardModifiers modifiers);
    // Drawing on a morphing's frame in between: the step that makes its frames drawings and what the tool then does,
    // gathered into one until the pen comes up.
    bool m_morphMacro = false;
    void closeMorphMacro();
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
    // On a bitmap layer: what `paint` draws (in the coordinates of the drawing) goes into the pixels of the drawing.
    // `reach`, when given, is how far the paint goes: pixels too few to take it in grow first.
    void paintIntoBitmap(std::function<void(QPainter&)> const& paint, QString const& text, QRectF reach = QRectF());
    // Rubs `rubbed` out of the pixels of the drawing's pictures (those kept in the scene); true when one was reached.
    bool erasePictures(Drawing& art, QVector<QPolygonF> const& rubbed) const;
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
    // On a bitmap layer: the pixels joined to the one clicked that are near enough its colour take the colour in hand
    // (or, `clear`, are cleared).  False when there was nothing to change.
    bool fillBitmapAt(QPointF p, bool clear);
    // On a bitmap layer: a piece of its pixels cut loose (with the Cutter, or picked with the Select tool) that has a
    // pixel at `p` (in the coordinates of the drawing); its place among the pictures, or -1.
    int liftedPieceAt(QPointF p) const;
    QPointer<Canvas> m_leader;     // the view whose tool settings this one follows (none: its own)
    int m_bitmapHardness = 100, m_bitmapFlow = 100;
    BitmapTip m_bitmapTip = BitmapRound;
    // The tip pressed along the stroke (its size `share` of the stroke's thickness at each place), in `brush`.
    void stampTip(QPainter& onto, Stroke const& stroke, double share, QBrush const& brush) const;
    // The dabs of a tip along a stroke (the stroke's own tip, or the dual one): `size` the width of the line, the rest as TipShape.
    struct Dabs {
        int kind = 0, roundness = 100, angle = 0, count = 1, spacing = 0;
        bool flipX = false, flipY = false, follow = false;
        int randomAngle = 0, randomSpacing = 0, randomCount = 0, randomSize = 0, randomOffset = 0, minOpacity = 100;
        double size = 1.0;
    };
    void stampDabs(QPainter& onto, QVector<StrokePoint> const& run, Dabs const& dabs, QBrush const& brush, QImage const& picture) const;
    bool m_bitmapUnchanged = false;     // set while painting into a bitmap: nothing changed, so nothing is kept
    QRectF selectionBounds() const;
    QPointF handlePos(Handle handle, QRectF const& bounds) const;  // in scene coordinates
    Handle handleAt(QPointF widgetPos) const;
    // Selects (or with `toggle`, flips) the item under the point; false when there is none.
    bool selectAt(QPointF scenePos, bool toggle);
    void selectInRect(QRectF const& rect, bool add);

    Document* m_document;
    Tool m_tool = Brush;
    int m_swatch = Scene::kNoSwatch;
    double m_brushWidth = 6.0;
    bool m_brushOutline = false;
    int m_stabiliser = StabiliserOff;
    int m_pressureFeel = 50;
    bool m_eraserSquare = false;
    TipShape m_tipShape;
    QImage m_tipImage;
    QImage m_dualImage;
    int m_bitmapCanvas = 150;
    int m_bitmapCanvasHeight = 150;
    int m_bitmapPpi = 72;
    int m_bitmapBasis = CanvasScene;
    bool m_autoColourArt = false;
    CopyArt m_copyArt;
    bool m_quietFlatten = false;
    // A line that fades along its length, painted: solid into a picture of its own, then made fainter there.
    static void paintFadingStroke(QPainter& painter, Stroke const& stroke, QBrush const& color);
    int m_tiltSensitivity = 0;
    int m_stencil = StencilUniform;
    QVector<float> m_customStencil;
    int m_retouchMode = 0;
    int m_retouchNow = 0;      // the mode of this rub (Alt turns it round)
    int m_pencilOpacity = 100;
    bool m_lineAutoFill = false;
    LineOptions m_lineOptions;
    // After a line is drawn: the invisible stroke that closes it, and its joining to a line it was built on.
    void finishDrawnLine(double reach);
    bool m_scaleThickness = false;
    // A painted area under `line`, the shape it goes round (one step to undo with what is drawn after it).
    void fillUnder(Stroke const& line);
    void mergeSelectedLines(bool bridge, QString const& text);
    // A line turned end for end (its handles with it); a line of straight pieces made a curve with straight runs.
    static Stroke reversedLine(Stroke line);
    // A line with the look of `look` and the course and thickness of `before` (to have more joined on after it).
    static Stroke startedFrom(Stroke const& look, Stroke const& before);
    static void makeCurve(Stroke& line);
    // The Contour Editor: where the point in hand goes when it is let go near another point (on it); the points of
    // lines that lie where it lay (they go with it, as one point); and two lines joined where an end was dropped on an end.
    QPointF pointMagnet(QPointF p) const;
    QVector<QPair<int, int>> sharedPoints(Drawing const& d) const;
    // The picked point and the points lying on it, as (line, point): one point to the eye, so all their handles show and
    // can be pulled (only for lines; a brush shape's contour has just the picked point).
    QVector<QPair<int, int>> stackedPoints(Drawing const& d) const;
    // Where a handle of one of them is in the view (null: it has none that way).
    QPointF stackedHandleKnob(Drawing const& d, int stroke, int point, int handle) const;
    // The line and point whose handle is in hand.
    Stroke* handleLine(Drawing& d, int& point) const;
    int m_ceHandleStroke = -1, m_ceHandlePoint = -1;     // -1: the picked point's own handle
    bool joinDroppedEnd(Drawing& edited);
    // A point let go on another point - or on a line (Snap to Contour puts it there): it is one point with it from then on
    // (a joint; on a line, a point is made there first).
    bool jointDroppedPoint(Drawing& edited);
    // Two joints made one (the points of `from` join `into`).
    static void mergeJoints(Drawing& drawing, int into, int from);
    int m_stabiliserStrength = 50;
    QPointF m_stabiliserLine, m_stabiliserPen;
    int m_lineMode = LineStraight;
    bool m_bending = false;
    QPointF m_bendFrom, m_bendTo;
    bool m_shapeAutoFill = false;
    void bendLine(QPointF at);
    void keepShape();
    int m_smoothing = 25;
    int m_cornerAngle = 50;
    Stroke::Join m_join = Stroke::RoundJoin;
    Stroke::Cap m_capStart = Stroke::RoundCap;
    Stroke::Cap m_capEnd = Stroke::RoundCap;
    int m_lineTexture = 0;
    int m_lineTextureAmount = 100;
    bool m_lineTextureCrisp = false;
    bool m_brushTextureCrisp = false;
    int m_lineRampLow = 0, m_lineRampHigh = 100, m_brushRampLow = 0, m_brushRampHigh = 100;
    int m_brushTexture = 0;
    int m_brushTextureAmount = 100;
    void retextureSelectedFills(QString const& text, bool kindToo);
    // Whether the tool in hand draws lines that take the pencil texture (the brush and the Stroke tool do not).
    void textureNewLine(Stroke& stroke) const;
    template <class Change> void restyleSelection(QString const& text, Change change);
    bool m_onionSkin = true;
    bool m_playing = false;
    int m_layer = 0;
    int m_frame = 0;

    // view
    double m_zoom = 1.0;
    QPointF m_pan;          // widget-space offset of the centre of the scene from that of the widget
    // Quick Close-up: on, and the view to go back to.
    bool m_closeup = false;
    double m_smoothWeight = 0.5;
    int m_pasteBehaviour = 0;
    void scaleSelectionThickness(double factor, double set, QString const& text);
    // The Contour Editor's copied points (in the drawing's coordinates), for Paste to add after the picked one.
    static QVector<StrokePoint> s_pointClipboard;
    bool m_middlePans = true;
    QColor m_boneColour = QColor(90, 220, 140);
    QColor m_boneSelectedColour = QColor(Qt::white);
    bool m_performanceReport = false;
    double m_lastDrawMs = 0.0;
    double m_averageDrawMs = 0.0;
    double m_closeupZoom = 1.0;
    QPointF m_closeupPan;
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
    int m_fieldsAcross = 12;
    int m_fieldsDown = 12;
    bool m_gridOutline = false;
    bool m_gridUnder = false;
    bool m_backlight = false;
    bool m_outlineLocked = false;
    bool m_symbolPivots = false;    // the pivot of every layer that shows a symbol is marked
    bool m_bboxSelection = false;
    void paintGrid(QPainter& painter, QRectF const& frameRect) const;
    bool m_safeArea = false;
    bool m_lightTable = false;
    bool m_currentOnTop = false;
    bool m_cameraMask = false;
    Art m_art = LineArt;
    bool m_previewAllArt = true;
    mutable Drawing m_artView;   // the colour art of the current drawing, for currentDrawing()
    mutable Picture const* m_artPictures = nullptr;     // (the pictures it was taken from)
    mutable qsizetype m_artPictureCount = 0;
    mutable Drawing m_bothView;  // the line art and the colour art as one drawing, while both are worked on
    mutable void const* m_bothKey[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    mutable qsizetype m_bothSize[5] = {-1, -1, -1, -1, -1};
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
        DragPoseMove, DragPoseScale, DragPoseRotate, DragPivot, DragShape, DragPoint, DragBone, DragJoint, DragIK, DragQuad, DragRotateView, DragHandle, DragPaint, DragPointMarquee, DragPolyHandle, DragWarpRest, DragWarp, DragGradient, DragGuide, DragThickness, DragInk, DragSmooth, DragPoseSkew, DragEnvelope, DragBoneIK, DragHint, DragControlPoint, DragRepaint, DragSplineOffset, DragPoseTurn, DragWeights, DragRetouch, DragCeBox, DragSegment
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
    bool m_onionBrightnessAlpha = false;
    bool m_onionOver = false;
    int m_onionMark = 0;
    void paintOnion(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const& tint) const;
    // What a symbol shows on one of its frames, painted where the painter stands (the sheet of the layer that shows it).
    static void paintSymbol(QPainter& painter, Scene const& scene, int symbolId, int frame);
    QColor m_onionBefore = QColor(220, 60, 60);
    QColor m_onionAfter = QColor(50, 160, 80);
    QColor m_backdrop = QColor(127, 127, 127);
    int m_lightWash = 30;
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
    // The shape of the current drawing when the document last changed (its layer, frame, and how many lines, areas and
    // pictures each art layer had): an undo that leaves it so keeps what is selected and the Contour Editor's point.
    QVector<int> m_seenShape;
    QVector<int> drawingShape() const;
    QPointF m_dragStart;          // scene position where the drag began
    QPointF m_dragNow;
    QRectF m_dragBounds;          // selection bounds when the drag began
    Handle m_dragHandle = NoHandle;
    QTransform m_dragTransform;   // applied to the selection while dragging
    bool m_marqueeAdds = false;
    bool m_lasso = true;
    bool m_zoomOnMouse = false;
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
    bool m_selectBothArts = false;
    int m_cpKey = -1, m_cpIndex = -1;       // the control point picked
    QPointF m_cpDragPos;                    // where the pointer has it while it is dragged (view)
    bool m_cpMoved = false;
    bool m_cpDepth = false;                 // taken with Alt: dragged up or down, it goes away from the camera or towards it
    QPointF m_cpPressPos;                   // where it was taken (view)
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
    ViewMode m_viewMode = ViewNormal;
    // The last picture a view mode made, for the frame and mode it was made for (made again when the scene changes).
    QImage m_modeCache;
    int m_modeCacheFrame = -1;
    ViewMode m_modeCacheMode = ViewNormal;
    QImage m_snapshot;
    bool m_showSnapshot = false;
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
    int m_stampRandomSize = 0;
    QPointF m_hoverWidget;       // where the pointer is (in the view)
    Drawing stampNow() const;
    void stampAt(QPointF p);
    int m_thickStroke = -1;      // the Pencil Editor: the line and the thickness point of it in hand (in m_eraseArt)
    int m_thickKey = -1;
    QPointF m_thickPos0;         // where on the line that is, and which way its left is
    QPointF m_thickAcross;
    QPointF m_thickPress;        // where the pen went down
    bool m_thickOneSide = false; // Alt: only the side pulled on
    bool m_thickMoved = false;
    bool m_thickExisting = false;   // a thickness point that was there (not one put down by this press)
    // The Line tool and the Polyline: the thickness stencil laid on the line drawn, as its thickness along it.
    void stencilLine(Stroke& line) const;
    void thicknessPointDown(QPointF p, Qt::KeyboardModifiers modifiers);
    void thicknessDown(QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    bool m_inked = false;        // the Ink tool: whether the drag has coloured a line yet
    void inkAt(QPointF p);
    // The Repaint Brush: the drawing as it was, the way the pen has gone, and that way's colour put on it.
    Drawing m_repaintFrom;
    Stroke m_repaintPath;
    // Spline Offset: where the pen went down and where it is (in the view), and that as a move of the path.
    QPointF m_splineFrom, m_splineNow;
    QPointF m_turnFrom;          // Turn in Space: where the pen went down (in the view)
    QPointF splineOffset() const;
    void repaintTo(QPointF p, double tolerance);
    void smoothAt(QPointF p);
    void retouchAt(QPointF p, int mode);
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
    int m_morphGroup = -1;
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
    bool m_snapping = true;
    int m_eraserMinSize = 100;
    bool m_pressureSmoothing = false;
    int m_minSmoothing = 30;
    int m_pencilMinOpacity = 100;
    int m_opacitySmoothing = 0;
    QVector<float> m_rawPressure;   // the pen's pressure along the line being drawn (for smoothing with pressure)
    bool m_singleStroke = false;
    bool m_singleNow = false;       // for the rub going on (Shift turns it the other way)
    int m_onlyLine = -1;            // ... the line it keeps to, or the brush shape (-2 - index)
    bool m_hardEdges = false;
    bool m_overlayBrush = false;
    std::function<QVector<int>()> m_maskLayers;
    QPainterPath m_paintClip;       // while a stroke is painted into pixels: only within this (Overlay Brush Mode)
    // The Line tool and the Polyline: a point put near the end of a line already drawn goes onto that end (so that lines
    // drawn to meet do meet, and the area they close round can be painted).  Off with the master switch of snapping.
    QPointF endMagnet(QPointF p) const;
    // The line kept joined to the lines whose ends its ends lie on (a joint at each: moved, they move together); false
    // when there are none (and nothing was done).
    bool keepJoined(Stroke const& line);
    // Paint that follows its lines: each such area of `whole` near lines that changed from `before` (all of them, with none)
    // found again from its lines.
    Drawing followLines(Drawing whole, Drawing const* before) const;
    bool startBend(Stroke const& line, QPointF p, Qt::KeyboardModifiers modifiers);     // a press on a curve: bent if dragged
    int m_segIndex = -1;            // the Contour Editor: the piece of the line being bent, and where on it it was taken
    double m_segT = 0.5;
    int m_pencilMode = PencilDraw;
    QColor m_pushHighlight = QColor(255, 140, 0);
    bool m_polyBehind = false;
    // Auto-Create Colour Art for a line kept by a shape tool or the Polyline: its twin, with no thickness, in the colour art.
    void colourArtTwin(Stroke const& line);
    bool m_hideManipulator = false;
    bool m_envAdvanced = true;
    bool m_ceControls = false;
    int m_ceBoxCorner = -1;         // the control of that box being dragged (0 to 3 a corner, 4 the knob)
    QRectF m_ceBoxStart;            // the box when it was taken
    QTransform contourBoxTransform() const;     // what the drag does to the points picked
    int m_cutterGesture = GestureOff;
    int m_cutterTip = 1;            // Stroke::FlatCap
    QVector<float> m_copiedProfile; // Copy Thickness: the profile (shares of the thickest) and how thick the thickest is
    float m_copiedWidth = 0.0f;
    double m_nudgeStep = 1.0;
    int m_ikIterations = 24;
    double m_ikError = 0.2;
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
    int m_gradStroke = -1;      // a line instead of an area
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
    int m_ceHandle = -1;      // the handle of that point being dragged: 0 the one before it, 1 the one after, 2 both pulled out of it (Alt)
    // Alt-pressed on a point: whether the pen has gone far enough from it to pull its handles out (or only clicked).
    bool pulledHandles() const { return std::hypot(m_dragNow.x() - m_dragStart.x(), m_dragNow.y() - m_dragStart.y()) * m_zoom > 3.0; }
    bool m_ceBreak = false;   // Alt: the handle moves alone, and the point becomes a cusp

    // bones: the joint being dragged (rigging tool) or the bone being turned (transform tool)
    int m_bone = -1;
    QVector<QPair<int, double>> m_ikHeld;   // IK: the bones holding their orientation, and the angle on screen each keeps
    // deformers: the point or handle being dragged (its place in Layer::warp)
    int m_warpIndex = -1;
    RigMode m_rigMode = RigBones;
    int m_weightBone = 0;
    double m_weightRadius = 30.0;
    double m_weightStrength = 0.5;
    QVector<SkinDab> m_weightDabs;      // painted in this stroke, not yet in the scene

    // transform tool
    Pose m_poseStart;             // the pose of the layer when the drag began
    Pose m_dragPose;              // the pose while dragging
    QPointF m_dragPivot;          // the pivot while dragging it, in layer coordinates
    QTransform m_world0;          // layer to scene when the drag began
    QPointF m_dragStartScene;
};
