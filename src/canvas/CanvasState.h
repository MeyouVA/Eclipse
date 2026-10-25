// Eclipse - what the Camera view keeps: the settings of its tools and views, and where a drag or an edit stands.
#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QTransform>
#include <QVector>

#include <functional>

#include "canvas/CanvasTypes.h"
#include "model/scene/Scene.h"

class Canvas;
class TextEntry;

class CanvasState : public CanvasTypes {
protected:
    bool m_morphMacro = false;
    int m_bitmapHardness = 100, m_bitmapFlow = 100;
    BitmapTip m_bitmapTip = BitmapRound;
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
    int m_tiltSensitivity = 0;
    QVector<float> m_customStencils[3];
    int m_retouchMode = 0;
    int m_pencilOpacity = 100;
    bool m_lineAutoFill = false;
    LineOptions m_lineOptions;
    bool m_scaleThickness = false;
    int m_stabiliserStrength = 50;
    QPointF m_stabiliserLine, m_stabiliserPen;
    int m_lineMode = LineStraight;
    bool m_bending = false;
    QPointF m_bendFrom, m_bendTo;
    bool m_shapeAutoFill = false;
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
    bool m_onionSkin = true;
    bool m_playing = false;
    int m_layer = 0;
    int m_frame = 0;
    // view
    double m_zoom = 1.0;
    // Quick Close-up: on, and the view to go back to.
    bool m_closeup = false;
    double m_smoothWeight = 0.5;
    int m_pasteBehaviour = 0;
    bool m_middlePans = true;
    QColor m_boneColour = QColor(90, 220, 140);
    QColor m_boneSelectedColour = QColor(Qt::white);
    bool m_performanceReport = false;
    double m_lastDrawMs = 0.0;
    double m_averageDrawMs = 0.0;
    double m_closeupZoom = 1.0;
    QPointF m_closeupPan;
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
    bool m_bboxSelection = false;
    bool m_safeArea = false;
    bool m_lightTable = false;
    bool m_currentOnTop = false;
    bool m_cameraMask = false;
    Art m_art = LineArt;
    bool m_previewAllArt = true;
    mutable qsizetype m_artPictureCount = 0;
    mutable void const* m_bothKey[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    mutable qsizetype m_bothSize[5] = {-1, -1, -1, -1, -1};
    bool m_showStrokes = false;
    bool m_strokesWash = false;
    bool m_showControl = false;
    int m_onionPrev = 1;
    int m_onionNext = 1;
    int m_onionOpacity = 35;
    int m_onionStrength[2][kOnionSteps] = {{100, 100, 100, 100, 100, 100}, {100, 100, 100, 100, 100, 100}};
    bool m_spaceDown = false;
    QFont m_textFont;
    int m_textAlign = 0;
    int m_textSpacing = 100;
    int m_textKerning = 100;
    bool m_applyAll = false;
    bool m_onionByFrame = false;
    bool m_onionOutlines = false;
    bool m_onionBrightnessAlpha = false;
    bool m_onionOver = false;
    bool m_onionOriginal = false;   // the onion skins in the drawings' own colours, not tinted
    bool m_onionOthers = true;      // other layers added to the onion skin show theirs too
    bool m_onionEnhanced = false;   // with the light table on, every layer it shows has its onion skin too
    bool m_applyOnion = false;      // the Select tool's changes also made in the drawings the onion skin shows
    int m_onionMark = 0;
    QColor m_onionBefore = QColor(220, 60, 60);
    QColor m_onionAfter = QColor(50, 160, 80);
    QColor m_backdrop = QColor(127, 127, 127);
    int m_lightWash = 30;
    int m_lightWhite = 0;           // the light table: how far the other layers are washed toward white
    bool m_lightShade = false;      // the light table: a veil over the layers under the one drawn on
    bool m_lightShadeCamera = false;    // ... in the Camera view too, not only the Drawing view
    bool m_onionWash = false;       // onion skins in their own colours, washed with the onion colours
    int m_onionWashMin = 20, m_onionWashMax = 60;
    QColor m_onionStepColour[2][kOnionSteps];
    QPointF m_lastWidgetPos;
    // selection: indices into the current drawing
    QVector<int> m_selStrokes;
    QVector<int> m_selFills;
    QVector<int> m_selPictures;
    QVector<int> m_seenShape;
    QPointF m_dragNow;
    Handle m_dragHandle = NoHandle;
    bool m_marqueeAdds = false;
    bool m_lasso = true;
    bool m_zoomOnMouse = false;
    // Zoom: its limits, the zoom Reset Zoom goes to, the Zoom tool's way, Quick Close Up, paper zoom (100 %: the frame its real size).
    double m_zoomMin = 0.02, m_zoomMax = 64.0;
    int m_defaultZoom = 100;
    bool m_zoomToolOut = false;
    double m_quickFactor = 4.0;
    bool m_paperZoom = false;
    int m_screenPpi = 96;
    QPointF m_zoomFrom;
    double m_zoomStart = 1.0;
    bool m_zoomMoved = false, m_zoomOutClick = false;
    // Shift and Trace: on or off, the box round the drawing being moved, which drawing that is, and the drag.
    bool m_shiftTrace = false;
    bool m_traceManipulator = true;
    int m_traceLayer = -1, m_traceDrawing = -1;
    QSet<QPair<int, int>> m_traceHidden;
    QTransform m_traceStart, m_traceLive;
    QPointF m_traceFrom, m_traceCentre;
    int m_traceWay = 0;     // 0 move, 1 turn (Ctrl), 2 scale (Alt)
    bool m_snapContour = false;
    bool m_showGuides = true;
    int m_activeGuide = -1;
    QPointF m_guideCentre;
    double m_guideRadius = 0.0;
    bool m_hoverKnown = false;
    int m_guidePoint = -1;
    bool m_guidesLocked = false;
    bool m_selectByColour = false;
    int m_envCount = 8;
    double m_gapSize = 2.0;
    bool m_paintAll = false;
    bool m_paintLayers = false;
    bool m_bothArts = false;
    bool m_selectBothArts = false;
    bool m_cpMoved = false;
    bool m_permanentSelection = false;
    bool m_snapAlign = false;
    QVector<QImage> m_preview;
    bool m_usePreview = true;
    ViewMode m_viewMode = ViewNormal;
    // The last picture a view mode made, for the frame and mode it was made for (made again when the scene changes).
    QImage m_modeCache;
    int m_modeCacheFrame = -1;
    ViewMode m_modeCacheMode = ViewNormal;
    QImage m_snapshot;
    QImage m_snapshotSlots[5];
    bool m_showSnapshot = false;
    bool m_snapGrid = false;
    bool m_animate = true;
    bool m_keyFrameOne = false;
    bool m_animateRange = false;
    int m_transformMode = TransformAll;
    bool m_noZDrag = false;
    // contour editor: the stroke whose points are shown, and the point picked on it
    int m_ceStroke = -1;
    int m_cePoint = -1;
    int m_ceFill = -1;
    int m_ceContour = 0;
    bool m_brushShapes = true;
    bool m_autoFlatten = false;
    bool m_drawBehind = false;
    bool m_dropperAllLayers = true;
    // V2 stage 9: the paint tools' options (the Paint and Ink tool properties), the dropper's, and gap closing.
    bool m_paintSelectNew = false;
    bool m_paintOverTexture = false;
    bool m_useStoredGradient = false;
    bool m_hasStoredGradient = false;
    QPointF m_storedGradFrom, m_storedGradTo;
    bool m_gapInPixels = false;
    bool m_dropperNoTransparency = false;
    bool m_stickyDropper = false;
    int m_toolBeforeDropper = -1;
    bool m_inkShowInkable = false;
    bool m_inkSelectNew = false;
    bool m_inkSmart = false;
    bool m_inkSelectionMode = false;
    bool m_inkJoinChange = false;
    int m_inkOrder = 0;
    int m_inkOrderNow = 0;
    bool m_dropperTexture = false;
    QVector<int> m_inkedNow;
    bool m_highlightColour = false;
    bool m_respectProtected = true;
    bool m_proportionGrid = false;
    int m_minSize = 0;
    bool m_stampOwn = false;
    int m_stampTurn = 0;
    bool m_stampRandom = false;
    int m_stampScale = 100;
    int m_stampRandomSize = 0;
    int m_thickKey = -1;
    QPointF m_thickAcross;
    bool m_thickMoved = false;
    // The Repaint Brush: the drawing as it was, the way the pen has gone, and that way's colour put on it.
    Drawing m_repaintFrom;
    Stroke m_repaintPath;
    // Spline Offset: where the pen went down and where it is (in the view), and that as a move of the path.
    QPointF m_splineFrom, m_splineNow;
    QVector<QPointF> m_envRest, m_envNow;
    int m_envPoint = -1;
    int m_morphGroup = -1;
    int m_hintIndex = -1;
    QPointF m_hintNow;
    bool m_snapping = true;
    int m_eraserMinSize = 100;
    int m_pencilMinSize = 100;
    bool m_pressureSmoothing = false;
    int m_minSmoothing = 30;
    int m_pencilMinOpacity = 100;
    int m_opacitySmoothing = 0;
    bool m_singleStroke = false;
    bool m_hardEdges = false;
    bool m_overlayBrush = false;
    std::function<QVector<int>()> m_maskLayers;
    double m_segT = 0.5;
    int m_pencilMode = PencilDraw;
    QColor m_pushHighlight = QColor(255, 140, 0);
    bool m_polyBehind = false;
    bool m_hideManipulator = false;
    bool m_envAdvanced = true;
    bool m_ceControls = false;
    int m_cutterGesture = GestureOff;
    float m_copiedWidth = 0.0f;
    double m_nudgeStep = 1.0;
    int m_ikIterations = 24;
    double m_ikError = 0.2;
    int m_propAcross = 3;
    int m_propDown = 3;
    double m_safeMargin = 0.1;
    bool m_eraserCuts = true;
    // the Edit Gradient tool
    int m_gradFill = -1;
    bool m_gradMoved = false;
    QPointF m_gradFrom, m_gradTo, m_gradStart;
    QPointF m_gradStartWidget;
    // bones: the joint being dragged (rigging tool) or the bone being turned (transform tool)
    int m_bone = -1;
    // deformers: the point or handle being dragged (its place in Layer::warp)
    int m_warpIndex = -1;
    RigMode m_rigMode = RigBones;
    int m_weightBone = 0;
    double m_weightRadius = 30.0;
    double m_weightStrength = 0.5;
    QPointF m_dragStartScene;
    Drag m_drag = DragNone;
    QVector<int> m_ikChain;     // the bone taken hold of, then the bones it hangs on, nearest first
    bool m_ikBegun = false;     // whether anything has been turned yet in this drag
    QPointer<Canvas> m_leader;     // the view whose tool settings this one follows (none: its own)
    // The bucket on pixels: how near a colour fills (0-255 a channel), how far the fill reaches under the edge, and what it looks at.
    int m_bitmapTolerance = 40;
    int m_bitmapOverlap = 0;
    int m_bitmapSource = 0;    // 0: the art layer painted; 1: every art layer of the drawing
    bool m_bitmapUnchanged = false;     // set while painting into a bitmap: nothing changed, so nothing is kept
    int m_stencils[3] = {StencilUniform, StencilUniform, StencilUniform};     // the Pencil's, the Line tool's, the Polyline's
    int m_retouchNow = 0;      // the mode of this rub (Alt turns it round)
    int m_ceHandleStroke = -1, m_ceHandlePoint = -1;     // -1: the picked point's own handle
    QPointF m_pan;          // widget-space offset of the centre of the scene from that of the widget
    bool m_fitted = true;   // keep fitting on resize until the user zooms or pans
    double m_viewRotation = 0.0;   // degrees, clockwise on the screen
    bool m_symbolPivots = false;    // the pivot of every layer that shows a symbol is marked
    bool m_animatePoints = false;
    // The current drawing with its points moved as this frame has them (Animate Points), and what it was made from.
    mutable Drawing m_pointView;
    mutable int m_pointViewLayer = -1, m_pointViewIndex = -1;
    mutable QVector<void const*> m_pointViewRest;
    mutable QVector<PointMove> m_pointViewMoves;
    mutable Drawing m_artView;   // the colour art of the current drawing, for currentDrawing()
    mutable Picture const* m_artPictures = nullptr;     // (the pictures it was taken from)
    mutable Drawing m_bothView;  // the line art and the colour art as one drawing, while both are worked on
    bool m_polyActive = false;   // a polyline is being laid down, in m_stroke
    QPointF m_polyHover;         // where its next point would go (layer coordinates)
    int m_quadCorner = -1;          // the corner of the quadmap being dragged
    QVector<QPointF> m_dragQuad;    // and where its corners are meanwhile
    class TextEntry* m_textEntry = nullptr;   // where the Text tool is being typed into
    QPointF m_textAt;                         // and where the words will stand (layer coordinates)
    int m_textEdit = -1;                      // the painted area whose words are being changed; -1: new words
    bool m_tabletActive = false;  // a pen is driving: ignore the mouse events Qt synthesises
    Stroke m_stroke;        // the stroke being drawn
    bool m_keepNoPictures = false;      // the drawing being committed has had its last picture taken away
    bool m_committing = false;    // our own edit is going through: keep the selection
    QPointF m_dragStart;          // scene position where the drag began
    QRectF m_dragBounds;          // selection bounds when the drag began
    QTransform m_dragTransform;   // applied to the selection while dragging
    QPointF m_guideStart;        // where the line being drawn along the guide began (in the frame)
    QPointF m_guideWay;          // the way it was settled to run (null until the pen has moved)
    bool m_guideBends = false;   // ... and, when that way bends (4- and 5-point perspectives), the circle it is part of
    QPointF m_hoverFrame;        // where the pointer is (in the frame)
    QVector<Scene::Guide> m_guideEdit;   // the guides while a point of one is dragged
    int m_guideLine = -1;        // the plain guide being dragged with the Select tool
    int m_cpKey = -1, m_cpIndex = -1;       // the control point picked
    TransformOptions m_tx;
    QVector<int> m_transformLayers;         // picked along with the current layer
    QPolygonF m_layerPick;                  // the lasso (or the box's two corners) round the layers being picked
    bool m_pivotAllDrawings = false;
    bool m_repositionCurrent = false;
    QPointF m_cpDragPos;                    // where the pointer has it while it is dragged (view)
    bool m_cpDepth = false;                 // taken with Alt: dragged up or down, it goes away from the camera or towards it
    QPointF m_cpPressPos;                   // where it was taken (view)
    QVector<QLineF> m_alignLines;        // what the selection being dragged is in line with (on the layer)
    bool m_maintaining = false;         // the move being dragged is Maintain Size
    bool m_scaleFromPress = false;       // the scale being dragged is counted from where the pen went down
    bool m_dragLasso = true;      // for the drag going on: Alt gives the other of the two
    QPolygonF m_lassoPath;        // the shape being drawn round things, in layer coordinates
    QVector<int> m_ceExtra;   // other points of that curve picked with it (Shift, or a box dragged round them)
    Drawing m_stamp;             // artwork of the user's own to stamp, about the origin
    QVector<Drawing> m_stampSet;  // all the drawings of the stamp in hand (m_stamp is the one whose turn it is)
    QPointF m_hoverWidget;       // where the pointer is (in the view)
    int m_thickStroke = -1;      // the Pencil Editor: the line and the thickness point of it in hand (in m_eraseArt)
    QPointF m_thickPos0;         // where on the line that is, and which way its left is
    QPointF m_thickPress;        // where the pen went down
    bool m_thickOneSide = false; // Alt: only the side pulled on
    bool m_thickExisting = false;   // a thickness point that was there (not one put down by this press)
    bool m_inked = false;        // the Ink tool: whether the drag has coloured a line yet
    QPointF m_turnFrom;          // Turn in Space: where the pen went down (in the view)
    int m_envPicked = -1;       // the point pressed last (its place among the points, times three): its handles show
    int m_hintMorph = -1;       // the morphing whose hint is being dragged, and which of its hints
    bool m_hintFar = false;     // on the second drawing: where it goes to
    QVector<float> m_rawPressure;   // the pen's pressure along the line being drawn (for smoothing with pressure)
    bool m_singleNow = false;       // for the rub going on (Shift turns it the other way)
    int m_onlyLine = -1;            // ... the line it keeps to, or the brush shape (-2 - index)
    QPainterPath m_paintClip;       // while a stroke is painted into pixels: only within this (Overlay Brush Mode)
    int m_segIndex = -1;            // the Contour Editor: the piece of the line being bent, and where on it it was taken
    int m_ceBoxCorner = -1;         // the control of that box being dragged (0 to 3 a corner, 4 the knob)
    QRectF m_ceBoxStart;            // the box when it was taken
    int m_cutterTip = 1;            // Stroke::FlatCap
    QVector<float> m_copiedProfile; // Copy Thickness: the profile (shares of the thickest) and how thick the thickest is
    Drawing m_eraseArt;             // the art being erased, as it is so far in the drag
    int m_gradStroke = -1;      // a line instead of an area
    int m_gradHandle = 0;           // 0: drawn out afresh, 1: its start, 2: its end
    bool m_eraseAny = false;        // whether the drag has taken anything yet
    int m_ceHandle = -1;      // the handle of that point being dragged: 0 the one before it, 1 the one after, 2 both pulled out of it (Alt)
    bool m_ceBreak = false;   // Alt: the handle moves alone, and the point becomes a cusp
    QVector<QPair<int, double>> m_ikHeld;   // IK: the bones holding their orientation, and the angle on screen each keeps
    QVector<SkinDab> m_weightDabs;      // painted in this stroke, not yet in the scene
    // transform tool
    Pose m_poseStart;             // the pose of the layer when the drag began
    Pose m_dragPose;              // the pose while dragging
    QPointF m_dragPivot;          // the pivot while dragging it, in layer coordinates
    QTransform m_world0;          // layer to scene when the drag began
};
