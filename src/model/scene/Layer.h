// Eclipse - a layer: its drawings, exposure, keys, bones, effects, morphs, drivers.
#pragma once

#include "model/scene/Effect.h"

#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <algorithm>

// A sound clip on a sound layer: part of a file, placed on the layer's frames, looped, its loudness shaped.
struct SoundClip {
    QString file;
    int start = 0;              // the layer's frame it starts on
    int stop = -1;              // the frame after its last (-1: as long as the part of the file played, times the loops)
    double offset = 0.0;        // seconds into the file it starts from (Start Time)
    double length = -1.0;       // seconds of the file played (-1: to its end)
    int loops = 1;              // times the part is played, one after another
    double volume = 1.0;        // its gain (1: as recorded)
    QVector<QPointF> envelope;  // (frame from the clip's start, gain) points; between them the gain goes straight; none: flat
    double envelopeAt(double frame) const;
    bool operator==(SoundClip const&) const = default;
};

// A sound effect on a sound layer's track, in order (Volume, Balance, Echo, Reverb, Low Pass, High Pass, Pitch Shift, Resample, Tempo).
struct SoundEffect {
    enum Type { Volume, Balance, Echo, Reverb, LowPass, HighPass, PitchShift, Resample, Tempo, TypeCount };
    int type = Volume;
    bool enabled = true;
    double a = 0.0, b = 0.0, c = 0.0;   // its settings (SoundEffect::settings says what each is)
    static QString name(int type);
    // The settings of a kind: their names, ranges and defaults.
    struct Setting { QString name; double low, high, initial; };
    static QVector<Setting> settings(int type);
    static SoundEffect make(int type);
    bool operator==(SoundEffect const&) const = default;
};

struct Morph {
    int from = 0;
    // How gently the change leaves the first drawing and arrives at the second (0..1 each).
    double easeIn = 0.0;
    double easeOut = 0.0;
    QVector<QPointF> velocity;
    // How far along (0 the first drawing, 1 the second) the morphing is when `t` (0..1) of its time has passed.
    double progress(double t) const;
    QVector<QLineF> hints;
    struct Group {
        QString name;
        QVector<int> first, second;
        bool operator==(Group const&) const = default;
    };
    QVector<Group> groups;
};

struct SkinDab {
    int bone = 0;
    QPointF at;
    double radius = 20.0;
    double strength = 0.5;
    bool operator==(SkinDab const&) const = default;
};

// Where a bone of a layer's chain bends the drawing (its zone of influence): everywhere, as near as it is (Infinite); inside
// an ellipse round it - so far to its left and right, so far beyond its start and end - fading out over `fade` (Elliptic);
// inside a shape drawn round it (Shaped); or nowhere (Zero).  Distances in the layer's units.
struct BoneInfluence {
    enum Type { Infinite, Elliptic, Zero, Shaped };
    int type = Infinite;
    double left = 60.0, right = 60.0;      // transversal radii
    double begin = 40.0, end = 40.0;       // longitudinal radii, past the bone's start and end
    bool symmetric = true;                 // the right radius is the left one
    double fade = 20.0;                    // the fade radius past the zone's edge
    double exponent = 1.0;                 // how the fade falls off (1: straight)
    bool fadeInside = false;               // Fade Inside Zones: the pull fading from the bone out to the zone's edge too
    QPolygonF shape;                       // a Shaped zone, in the layer's coordinates at rest
    bool operator==(BoneInfluence const&) const = default;
    double rightRadius() const { return symmetric ? left : right; }
};

// How a chain's drawing is scaled across the skeleton, from the chain's start to its end, on each side (Deformation Scale);
// both sides alike (Uniform); and a wave of scaling along it (Deformation Wave).  1: as drawn.
struct ChainScale {
    double leftStart = 1.0, leftEnd = 1.0, rightStart = 1.0, rightEnd = 1.0;
    bool uniform = false;                  // the right side as the left
    double waveLeft = 0.0, waveRight = 0.0;          // amplitude (a share of the width)
    double periodLeft = 100.0, periodRight = 100.0;  // length of one wave along the chain
    double offsetLeft = 0.0, offsetRight = 0.0;      // where the wave starts (along the chain)
    bool waveSameAsLeft = true;
    // The wave over part of the chain only (span 0: to its end), growing in and dying out over so much of it.
    double spanLeft = 0.0, spanRight = 0.0;
    double fadeInLeft = 0.0, fadeOutLeft = 0.0, fadeInRight = 0.0, fadeOutRight = 0.0;
    // The scale from start to end along a curve: its handles' places (0..1 along) and scales.  Off: straight.
    bool curved = false;
    double leftStartHandlePos = 1.0 / 3.0, leftStartHandleScale = 1.0, leftEndHandlePos = 2.0 / 3.0, leftEndHandleScale = 1.0;
    double rightStartHandlePos = 1.0 / 3.0, rightStartHandleScale = 1.0, rightEndHandlePos = 2.0 / 3.0, rightEndHandleScale = 1.0;
    // Offset Deformer: the line the side is scaled from, so far out from the skeleton; Offset Scaling: so much added out there.
    double offsetDeformerLeft = 0.0, offsetDeformerRight = 0.0, offsetScalingLeft = 0.0, offsetScalingRight = 0.0;
    bool operator==(ChainScale const&) const = default;
    bool plain() const
    {
        return leftStart == 1.0 && leftEnd == 1.0 && (uniform || (rightStart == 1.0 && rightEnd == 1.0)) && waveLeft == 0.0
               && (waveSameAsLeft || waveRight == 0.0) && !curved && offsetScalingLeft == 0.0 && (uniform || offsetScalingRight == 0.0);
    }
};

// A value of a layer's pose (Driver::Value) that takes a function column's value instead of its keyframes'.
struct ValueLink {
    int value = 0;
    int function = 0;       // the column's id
    bool operator==(ValueLink const&) const = default;
};

// An attribute of the user's own on a layer: a number, set by hand or taken from a function column.
struct CustomAttribute {
    QString name;
    double value = 0.0;
    int function = 0;       // a function column's id it follows (0: its own value)
    bool operator==(CustomAttribute const&) const = default;
};

// A value of a layer inside a group shown (and edited) on the group itself: the layer's id and its Driver::Value.
struct PublishedAttribute {
    int layer = 0;
    int value = 0;
    bool operator==(PublishedAttribute const&) const = default;
};

struct Driver {
    enum Value { X, Y, ScaleX, ScaleY, Angle, Opacity, Z, Skew, Fov, ValueCount };
    int value = Angle;          // what of this layer's pose follows
    int source = 0;             // the id of the layer it follows
    int sourceValue = Angle;    // what of that layer's pose
    double factor = 1.0;
    double offset = 0.0;
    bool operator==(Driver const&) const = default;
    static double read(Pose const& pose, int value);
    static void write(Pose& pose, int value, double v);
};

// A mark drawn with the pen in an annotation column: x across the column (0..1), y down it in frames.
struct AnnotationInk {
    QVector<QPointF> points;
    QColor colour = Qt::black;
    double width = 2.0;
    bool operator==(AnnotationInk const&) const = default;
};

// A picture put into an annotation column: from a frame, so many frames tall.
struct AnnotationPicture {
    int frame = 0;
    int frames = 4;
    QByteArray png;
    bool operator==(AnnotationPicture const&) const = default;
};

struct Layer {
    static constexpr int kNoDrawing = -1;

    enum Kind { DrawingLayer, Peg, Camera, Sound, ColourCard, Group, Bone, Quadmap, Annotation };
    bool holdsDrawings() const { return kind == DrawingLayer; }

    int id = 0;  // unique in the scene and never reused: what `parent` refers to
    Kind kind = DrawingLayer;
    int parent = 0;
    int parentBone = -1;
    QVector<QPointF> bones;
    QVector<QPointF> warp;
    bool warpClosed = false;
    int warpPoints() const { return int(warp.size()) / 3; }
    bool hasWarp() const { return warpPoints() >= (warpClosed ? 3 : 2); }
    QPointF nodePos;
    bool nodePlaced = false;
    int nodeThumb = 0;              // how tall its node's thumbnail is in the Node View (0: none shown)
    QVector<CustomAttribute> customAttributes;
    QVector<PublishedAttribute> published;     // a group: values of the layers in it shown on it
    bool enable3d = true;           // off: it does not turn in space (Turn in Space is left out)
    // How its pictures were last fitted to the frame (Layer Properties > Alignment Rule): 0 as placed, 1 fit, 2 pan (fill), 3 actual size.
    int alignment = 0;
    // What it shows, turned a quarter (before it is fitted) and flipped, about the middle of the frame.
    bool turnBeforeAlignment = false;
    bool flipHorizontal = false;
    bool flipVertical = false;
    // Clipped to the frame made larger or smaller about its middle by these factors (off: nothing is clipped).
    bool clipping = false;
    double clipX = 1.0;
    double clipY = 1.0;
    // Vector render options: edge smoothness (Low hard, Very High at twice the size), edge hardness (exponent), picture scaling.
    enum Antialias { AntialiasLow, AntialiasMedium, AntialiasHigh, AntialiasVeryHigh };
    int antialiasing = AntialiasHigh;
    double antialiasExponent = 1.0;
    enum TextureFilter { FilterSmooth, FilterNearest };
    int textureFilter = FilterSmooth;
    // Line Thickness: its vector lines' thickness multiplied, added to and held between a least and a most (0: none); Zoom Independent keeps it as the layer is scaled.
    double lineMultiply = 1.0, lineAdd = 0.0, lineMin = 0.0, lineMax = 0.0;
    bool lineZoomIndependent = false;
    bool linesReshaped() const { return lineMultiply != 1.0 || lineAdd != 0.0 || lineMin > 0.0 || lineMax > 0.0 || lineZoomIndependent; }
    // Bitmap File Options: its pictures' colours read (off: only their shape, in black), and the pictures repeated across and down.
    bool readColour = true;
    bool tileHorizontal = false, tileVertical = false;
    double bitmapMultiplier = 1.0;  // Bitmap Options: how solid its pictures are, multiplied
    // The colour space its pictures are read in (empty: the scene's working space: not converted).
    QString colourSpace;
    QVector<Effect> effects;
    double effectAmount(int index, Pose const& pose) const;
    QColor cardColor = Qt::white;   // a colour-card: its colour
    double boneLength = 200.0;      // a bone: from its pivot, along its x axis
    bool ikExcluded = false;
    bool ikLimited = false;
    bool ikNail = false;
    bool ikHoldAngle = false;
    double ikMin = -90.0;
    double ikMax = 90.0;
    static constexpr double kMinFov = 1.0, kMaxFov = 179.0;
    static constexpr double kDefaultFov = 41.112;
    double fov = kDefaultFov;
    double nearPlane = 0.0;
    double farPlane = 0.0;
    // A camera that is not available is never the one the scene is seen through (Scene > Camera passes it by).
    bool cameraAvailable = true;
    // The angle on a frame: that of the keyframes when they say, else the one above.
    double fovAt(int frame) const;
    QVector<QPointF> quad;
    QSizeF quadSize;
    static QTransform quadWarp(QSizeF size, QVector<QPointF> const& quad);
    // The four corners under a pose.
    QVector<QPointF> quadAt(Pose const& pose) const;
    QString soundFile;        // a sound layer: the file it plays
    int soundStart = 0;       // and the frame it starts on
    QVector<int> soundFrames; // source frames after retiming; -1 is silent, empty plays continuously
    int soundFrameAt(int frame) const { int const at = frame - soundStart; return soundFrames.isEmpty() ? at : soundFrames.value(at, -1); }
    // The clips of the track (none: the whole of soundFile from the layer's first frame), the track's own gain and effects.
    QVector<SoundClip> soundClips;
    double soundVolume = 1.0;
    QVector<SoundEffect> soundEffects;
    QVector<SoundClip> clipsOfSound() const
    {
        if (!soundClips.isEmpty())
            return soundClips;
        SoundClip whole;
        whole.file = soundFile;
        return soundFile.isEmpty() ? QVector<SoundClip>() : QVector<SoundClip>{whole};
    }
    QString lipsync;
    // The shape on a frame of the scene; null where the sound layer has none.
    QChar mouthAt(int frame) const
    {
        int const at = soundFrameAt(frame);
        return kind == Sound && at >= 0 && at < lipsync.size() ? lipsync[at] : QChar();
    }
    QPointF pivot;            // the point scaling and rotation happen about, in layer coordinates
    // A drawing's own pivot: not used, used by this layer, or used by the peg it hangs on.
    enum EmbeddedPivot { PivotNotUsed, PivotOnLayer, PivotOnParentPeg };
    int embeddedPivot = PivotNotUsed;
    // Separate: x, y and z go each their own way between keyframes, with no path of control points.
    bool separatePosition = false;
    // Separate: width and height are scaled each on its own; locked, they keep in proportion (and the layer cannot be skewed).
    bool separateScale = true;
    bool ignoreParentScale = false;
    // How the camera sees it (and what hangs under it): as placed, turned to face it, carried along with it, or flat on its plane.
    enum CameraAlignment { AlignNormal, FaceCamera, PinToCamera, ProjectOnCameraPlane, AlignmentCount };
    int cameraAlignment = AlignNormal;
    bool angleLimited = false;
    double minAngle = -180.0;
    double maxAngle = 180.0;
    // Off: the animation tools pass it by and move the peg above it.
    bool animatable = true;
    // On: the angle limits hold for every frame, not only while the tools turn the layer.
    ECLIPSE_MODEL_API static bool enforceAngleLimits;
    // What new layers start with (Preferences).
    struct NewLayerDefaults {
        bool pegSeparatePosition = false, elementSeparatePosition = false;
        bool pegSeparateScale = true, elementSeparateScale = true;
        bool animatable = true;
        int embeddedPivot = PivotNotUsed;
        // Of drawing layers: how smooth their edges are, and how hard their soft edge (Antialiasing Quality and Exponent).
        int antialiasing = 2;
        double antialiasExponent = 1.0;
        // Preferences > Colour Management: New Layer Colour Space (empty: the working space).
        QString colourSpace;
    };
    ECLIPSE_MODEL_API static NewLayerDefaults newLayers;
    double limitedAngle(double angle) const { return angleLimited ? std::clamp(angle, std::min(minAngle, maxAngle), std::max(minAngle, maxAngle)) : angle; }
    QVector<Keyframe> keys;   // sorted by frame; empty means the layer never moves

    // The pose at a frame: that of the keyframes, tweened where they ask for it.
    Pose poseAt(int frame) const;
    Pose keyedPoseAt(int frame) const;  // (without the angle limits)
    Pose tweenedPoseAt(int frame) const;  // (... and without the values' own curves)
    // The keyframes (their places in `keys`) a value's curve goes through (Driver::Value).
    QVector<int> keysOf(int value) const;
    // The handles of a value's curve from keyframe `from` to the next that sets it, `to`: their own, or as the ease makes them.
    void segmentHandles(int value, int from, int to, QPointF& out, QPointF& in) const;
    // The value on `frame` through the keyframes `through` (keysOf), shaped by their curves.
    double curveValue(int value, QVector<int> const& through, double frame) const;
    // A value's curve through keyframe `key`: its own, or as the ease makes it (no handle on a side with no next point).
    ValueCurve curveAt(int value, int key) const;
    // The keyframe's ease given to the values with curves of their own too (their handles made as the ease has them).
    void easeCurves(int key);
    // A value's curve between keyframes `from` and `to` shaped as an ease: handles as shares (0..1) of the way in time and value.
    void shapeSegment(int value, int from, int to, QPointF leave, QPointF arrive);
    // Some values' points (frame, Keyframe::Part bits) moved by `by` frames, the rest of their keyframes staying (see devnotes).
    bool moveValues(QVector<QPair<int, int>> const& points, int by, int last);
    // Values taken out of keyframes (frame, bits): a keyframe left setting nothing goes.
    void removeValues(QVector<QPair<int, int>> const& points);
    // The keyframes a value goes through all say so (before one stops setting it, which would leave it to the rest).
    void spellOut(int bits);
    // The index of the keyframe at exactly `frame`, or -1.
    int keyIndex(int frame) const;
    int previousKeyFrame(int frame) const;  // -1 when there is none
    int nextKeyFrame(int frame) const;
    void setKey(int frame, Pose const& pose);
    static QTransform matrix(Pose const& pose, QPointF pivot);
    // The move, turn, scale and lean `matrix` makes `m` of about the pivot (0, 0); false when `m` flattens all onto a line.
    static bool poseOf(QTransform const& m, Pose& pose);
    // From layer coordinates to those of the parent.
    QTransform localTransform(int frame) const { return matrixFor(poseAt(frame)); }
    QTransform matrixFor(Pose const& pose) const;
    QTransform matrixFor(Pose const& pose, QPointF about) const;

    QString name;
    // Names and values kept with the layer, for whoever wants to note something about it.
    QVector<QPair<QString, QString>> metadata;
    bool visible = true;
    // A locked layer is seen but cannot be drawn on or picked in the Camera view.
    bool locked = false;
    // While any layer is solo, only the solo ones are seen in the Camera view.
    bool solo = false;
    // Shows its neighbouring drawings as onion skin even while another layer is worked on.
    bool onion = false;
    // Palettes of clones (places in Scene::paletteNames) whose colours this layer is drawn with instead of the colours they clone.
    QVector<int> paletteOverrides;
    // Tagged: marked out, so that the Timeline can be made to show the tagged layers only.
    bool tagged = false;
    // Its track in the Timeline shows a picture of each drawing it exposes (a taller row).
    bool thumbnails = false;
    int symbolId = 0;
    int syncGroup = 0;
    int cloneGroup = 0;
    // A clone that shares the drawings only: each layer of the group has a timing of its own.
    bool cloneTiming = true;
    int hiddenSheets = 0;
    // A bitmap layer: what is drawn on its line art is painted in pixels, not kept as lines.
    bool bitmap = false;
    // The other art layers painted in pixels too (one bit for each Drawing::Sheet; the line art's is `bitmap`).
    int bitmapArt = 0;
    bool bendOnBones = false;
    double jointSmoothness = 0.0;
    // The bones' zones of influence (one for each bone; those past the end are Infinite), the chain's scaling and wave.
    QVector<BoneInfluence> influences;
    BoneInfluence influenceOf(int bone) const { return bone >= 0 && bone < influences.size() ? influences[bone] : BoneInfluence(); }
    ChainScale chainScale;
    // Pencil lines deformed: Quality (0 Low .. 3 Very High: how finely they are followed), their thickness kept as drawn
    // or stretched with the drawing, smoothed (passes), and how far a point may stray when the line is simplified again.
    // Point Kinematic Output: a peg that follows points of its parent's deformed drawing (trackers, in the parent's coordinates
    // at rest) - by the first alone (moved), the first two (moved and turned; scaled too), or three (any skew).
    enum Sampling { OnePoint, TwoPoint, TwoPointScale, ThreePoint };
    QVector<QPointF> trackers;
    int trackerSampling = OnePoint;
    int lineDeformQuality = 1;
    bool linePreserveThickness = true;
    int lineDeformSmooth = 0;
    double lineDeformFitError = 0.0;
    QVector<SkinDab> skin;
    // Values of its pose that follow other layers' (attribute controls): these take the place of its keyframes for them.
    QVector<Driver> drivers;
    // Values linked to function columns (one link a value at most).
    QVector<ValueLink> links;
    // Its function columns hidden in the Xsheet (a bit for each Driver::Value).
    int hiddenFunctions = 0;
    int linkOf(int value) const;        // the column's id, or 0
    enum Blend { BlendNormal, BlendMultiply, BlendScreen, BlendOverlay, BlendDarken, BlendLighten, BlendColourDodge, BlendColourBurn,
                 BlendHardLight, BlendSoftLight, BlendDifference, BlendExclusion, BlendAdd, BlendCount };
    int blend = BlendNormal;
    bool bitmapSheet(int sheet) const { return sheet == 2 ? bitmap : (bitmapArt & (1 << sheet)) != 0; }
    int bitmapResolution = 100;
    bool sheetShown(int sheet) const { return (hiddenSheets & (1 << sheet)) == 0; }
    bool showsSymbol() const { return symbolId != 0; }
    // Frames of the layer marked with a colour in the Timeline (a note to oneself: "redo", "hold" ...).
    QMap<int, QColor> frameMarks;
    QVector<int> keyExposures;
    // Drops those that mark nothing any more (an empty cell, or a frame where the drawing changes anyway).
    void tidyKeyExposures();
    // The cells from `at` on have moved by `delta` (cells put in, or - negative - taken out there): the marks follow.
    void shiftKeyExposures(int at, int delta);
    // The layers attached to this one are folded away in the Timeline.
    bool collapsed = false;
    // The colour of the layer's row in the Timeline and of its column in the Xsheet; invalid for the usual grey.
    QColor trackColor;
    // Its column in the Xsheet: hidden, as wide as so many pixels (0: the usual width), folded to a narrow strip.
    bool columnHidden = false;
    int columnWidth = 0;
    bool columnCollapsed = false;
    // An annotation column: notes typed in from a frame on, marks drawn with the pen and pictures put in.
    QMap<int, QString> notes;
    QVector<AnnotationInk> ink;
    QVector<AnnotationPicture> pictures;
    double opacity = 1.0;
    QVector<Drawing> drawings;
    QVector<int> cells;  // one per frame: index into drawings, or kNoDrawing

    int drawingAt(int frame) const { return frame >= 0 && frame < cells.size() ? cells[frame] : kNoDrawing; }
    // Whether what the layer shows on a frame is a 3D model (which turns in space itself, rather than as a flat sheet).
    bool modelAt(int frame) const
    {
        int const index = drawingAt(frame);
        return index >= 0 && index < drawings.size() && !drawings[index].model.isEmpty();
    }
    // What a drawing is called: its name, or its number (from one).
    QString drawingLabel(int index) const
    {
        return index < 0 || index >= drawings.size() ? QString() : drawings[index].name.isEmpty() ? QString::number(index + 1) : drawings[index].name;
    }
    // The drawing layers of the layer, in the order their first drawings come (the drawings of none: "").
    QStringList drawingSets() const;
    // The drawing of that drawing layer that goes by that name, or kNoDrawing.
    int drawingInSet(QString const& set, QString const& label) const;
    QVector<Morph> morphs;
    // The morphing `frame` lies in (from its first key drawing up to, not counting, its second), or -1.
    int morphIndexAt(int frame) const;
    // The frame of the second key drawing of a morphing; -1 when it has none any more.
    int morphEnd(int index) const;
    // True on the frames in between the two key drawings of a morphing.
    bool morphing(int frame) const;
    Drawing shownAt(int frame) const;
    // True when `frame` starts an exposure (it shows a drawing the previous frame does not, or is a key exposure).
    bool isKey(int frame) const;
    // The first frame of the exposure `frame` belongs to, or -1 for an empty cell.
    int exposureStart(int frame) const;
    // The nearest frame before / after `frame` that shows a different drawing; -1 if none.
    int previousDrawingFrame(int frame) const;
    int nextDrawingFrame(int frame) const;
    // Drops drawings no cell refers to (unless pinned) and renumbers the cells.
    void compact();
    void keepDrawingIdentitiesUnique();
    // The keyframes' moved points follow their drawings to new places (`remap[old]`; kNoDrawing: gone).
    void renumberMoves(QVector<int> const& remap);
    void remapPointReferences(int drawing, Drawing const& before, Drawing const& after);
    void substitute(int frame, int drawing);
};

struct PoseOverride {
    int layer = -1;
    int frame = -1;
    Pose pose;
    bool hasMatrix = false;
    QTransform matrix;
    // Other layers posed along with it, on the same frame.
    QVector<QPair<int, Pose>> others;
    Pose const* otherPose(int l, int f) const
    {
        if (f != frame)
            return nullptr;
        for (auto const& o : others)
            if (o.first == l)
                return &o.second;
        return nullptr;
    }
};
