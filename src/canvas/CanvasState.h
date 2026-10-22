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
    int m_onionMark = 0;
    QColor m_onionBefore = QColor(220, 60, 60);
    QColor m_onionAfter = QColor(50, 160, 80);
    QColor m_backdrop = QColor(127, 127, 127);
    int m_lightWash = 30;
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
};
