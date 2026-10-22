// Eclipse - the Camera view's kinds of things: its tools, art layers, stencils, drags, view modes, and the settings that
// come as a set (line options, a brush tip's shape).  Canvas has them as its own (Canvas::Tool, Canvas::Brush ...).
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include "model/scene/Scene.h"

struct CanvasTypes {
    enum Tool {
        Select, Transform, Brush, Eraser, Paint, Hand, Pencil, Line, Rectangle, Ellipse, ContourEditor, Rigging, IK,
        // Takes the colour of what is clicked on.
        Dropper,
        // Click to look closer, Alt-click to draw back.
        Zoom,
        // A line of straight pieces: a click for each point, a double click (or Enter) for the last.
        Polyline,
        PaintUnpainted, Repaint, Unpaint,
        // Click, type, Enter: the words become painted shapes of the current colour.
        Text,
        // Drag to turn the view about its middle.
        RotateView,
        Cutter,
        StrokeTool, CloseGap, EditGradient,
        Reposition,
        Stamp,
        PencilEditor,
        // What is selected is put in perspective: each corner of its box is pulled on its own.
        Perspective,
        // The lines the pen passes over take the colour in hand; paint is left alone.
        Ink,
        SmoothEditor,
        Envelope,
        Morphing,
        RepaintBrush,
        // Dragged anywhere, the current layer's whole motion path goes with the pen: every keyframe and control point.
        SplineOffset,
        Turn3D,
        // Rubbed along pencil lines, it makes them thicker there (or thinner: its mode, or Alt), more the longer it is rubbed.
        PencilRetouch,
    };
    // Which sheet of the drawing the tools work on.
    enum Art { LineArt, ColourArt, OverlayArt, UnderlayArt };
    enum Stabiliser { StabiliserOff, StabiliserCatchUp, StabiliserNoCatchUp };
    enum LineMode { LineStraight, LineCurve, LineSCurve };
    // The thickness stencils of the Pencil: how thick a new pencil line is along its length.
    enum Stencil { StencilUniform, StencilTapered, StencilTaperedStart, StencilTaperedEnd, StencilMediumTapered, StencilPointy, StencilFatEnds,
                   StencilCurvedAbove, StencilCurvedBelow, StencilCount };
    struct LineOptions {
        int startTaper = 0, endTaper = 0, speed = 0;
        // Fade Distance: the Brush's line thins away to nothing over so far from where it began (scene units; 0: no fade).
        int fade = 0;
        bool speedInvert = false, trim = false, connect = false, closeGap = false, building = false, straight = false;
    };
    struct CopyArt {
        int from = 2, to = 1;           // Drawing::Sheet: Line art into colour art
        bool clearFirst = false, allDrawings = false;
    };
    // The Brush on a bitmap layer: the shape of its tip, pressed down again and again along the stroke.
    enum BitmapTip { BitmapRound, BitmapSquare, BitmapChisel, BitmapSpray, BitmapImage };
    enum RigMode { RigBones, RigCurve, RigEnvelope, RigWeights };
    enum ViewMode { ViewNormal, ViewRender, ViewMatte, ViewDepth };
    enum TransformMode { TransformAll, TranslateOnly, RotateOnly, ScaleOnly, SkewOnly, MaintainSize };
    static constexpr int kOnionSteps = 6;
    static constexpr char16_t kLineBreak = 0x21B5;
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
    enum CanvasBasis { CanvasScene, CanvasCamera, CanvasCameraMove };
    enum CutterGesture { GestureOff, GestureSingle, GestureMultiple, GestureBreak };
    enum PencilMode { PencilDraw, PencilPush, PencilThicken };
    enum Handle { NoHandle, TopLeft, TopRight, BottomRight, BottomLeft, Rotate };
    // The dabs of a tip along a stroke (the stroke's own tip, or the dual one): `size` the width of the line, the rest as TipShape.
    struct Dabs {
        int kind = 0, roundness = 100, angle = 0, count = 1, spacing = 0;
        bool flipX = false, flipY = false, follow = false;
        int randomAngle = 0, randomSpacing = 0, randomCount = 0, randomSize = 0, randomOffset = 0, minOpacity = 100;
        double size = 1.0;
    };
    // interaction
    enum Drag {
        DragNone, DragDraw, DragErase, DragPan, DragMarquee, DragMove, DragScale, DragRotate,
        DragPoseMove, DragPoseScale, DragPoseRotate, DragPivot, DragShape, DragPoint, DragBone, DragJoint, DragIK, DragQuad, DragRotateView, DragHandle, DragPaint, DragPointMarquee, DragPolyHandle, DragWarpRest, DragWarp, DragGradient, DragGuide, DragThickness, DragInk, DragSmooth, DragPoseSkew, DragEnvelope, DragBoneIK, DragHint, DragControlPoint, DragRepaint, DragSplineOffset, DragPoseTurn, DragWeights, DragRetouch, DragCeBox, DragSegment
    };
};
