// Eclipse - the document model.
//
// A scene is a stack of layers over a number of frames.  Each layer owns its drawings and
// an exposure sheet: for every frame, which drawing is shown (or none).  A drawing held
// over several frames is simply the same index repeated, as on a paper exposure sheet.
#pragma once

#include <functional>

#include <QBrush>
#include <QColor>
#include <QPair>
#include <QJsonArray>
#include <QMap>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

// How far a point is from the stretch between two others.
double distanceToSegment(QPointF p, QPointF a, QPointF b);

// One sample of a stroke: where the pen was and how hard it pressed (0..1).
struct StrokePoint {
    QPointF pos;
    float pressure = 1.0f;
    // On a curve: the line turns sharply here instead of bending through the point.
    bool corner = false;
    bool handles = false;
    QPointF in;
    QPointF out;
    float lean = 0.0f;
    // How solid the line is here (0 to 1): a line can fade along its length.
    float opacity = 1.0f;
    int joint = 0;
};

struct ThicknessPoint {
    double at = 0.0;
    float left = 1.0f;
    float right = 1.0f;
    bool operator==(ThicknessPoint const& o) const { return at == o.at && left == o.left && right == o.right; }
};

// One pen stroke, in scene coordinates (pixels of the camera frame).
struct Stroke {
    QVector<StrokePoint> points;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    bool curve = false;
    // A closed line returns from its last point to its first.
    bool closed = false;
    enum Join { RoundJoin, MitreJoin, BevelJoin };
    enum Cap { RoundCap, FlatCap };
    Join join = RoundJoin;
    Cap capStart = RoundCap;  // at the first point
    Cap capEnd = RoundCap;    // at the last
    static constexpr double kMitreLimit = 50.0;
    // True when the line is as thick at every point (so that joins and caps apply).
    bool uniform() const;
    // Whether any of it leans to one side; whether any of it is less than solid.
    bool leans() const;
    bool fades() const;
    int swatch = -1;
    int group = 0;
    // A line that paint was made inside of (Auto Fill): the number that paint knows it by (0: none). See Fill::link.
    int link = 0;
    int order = 0;
    QColor color = Qt::black;
    float width = 4.0f;  // at full pressure
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;
    static constexpr int kTextureCount = 5;
    int texture = 0;
    float textureAmount = 1.0f;
    // A crisp edge: the grain frays at the edge of the line but does not fade there, and is not blurred.
    bool textureCrisp = false;
    float textureLow = 0.0f;
    float textureHigh = 1.0f;
    double textureRamp(double grain) const
    {
        if (textureLow <= 0.0f && textureHigh >= 1.0f)
            return grain;
        double const low = textureLow, high = textureHigh;
        return high - low < 1e-4 ? (grain >= low && grain > 0.0 ? 1.0 : 0.0) : (grain <= low ? 0.0 : grain >= high ? 1.0 : (grain - low) / (high - low));
    }

    void transform(QTransform const& t);

    QVector<StrokePoint> path(QVector<int>* segmentOf = nullptr) const;
    // The same, with the place on the curve of each piece's start (see ThicknessPoint::at).
    QVector<StrokePoint> pathWithPlaces(QVector<double>& places) const;
    // The shape alone (places along it, with the points' own thickness where the line has none of its own).
    QVector<StrokePoint> shapeWithPlaces(QVector<double>& places) const;
    QVector<ThicknessPoint> keysByLength(QVector<double> const& places, QVector<double> const& along) const;
    static ThicknessPoint sidesAlong(QVector<ThicknessPoint> keys, double length, bool loop, double total);

    QVector<ThicknessPoint> thickness;
    // The pieces of the curve: places run from 0 to this.
    int pieces() const;
    // How thick the line is at a place (as `pressure`), and to which side (as `lean`); its two sides there.
    void thicknessAt(double at, float& pressure, float& lean) const;
    float pressureAt(double at) const;
    ThicknessPoint sidesAt(double at) const;
    // The thickness made a profile of its own, from the points' (a place at each); one there is already is kept.
    void ownThickness();
    void takeThicknessFrom(QVector<StrokePoint> const& drawn);
    // The thickness, as places along the line by how far along it they are (0 its start, 1 its end), and set so.
    QVector<QPair<double, ThicknessPoint>> thicknessByLength() const;
    void setThicknessByLength(QVector<QPair<double, ThicknessPoint>> const& byLength);
    // Places that a smooth run through their neighbours says within `tolerance` anyway: dropped.
    void simplifyThickness(double tolerance);
    // A thickness place put at `at` (as thick as the line is there), or the one already within `near`; its index.
    int thicknessPointAt(double at, double near = 1e-6);
    // The place on the curve nearest `p`, and the point of the curve at a place.
    double placeAt(QPointF p) const;
    QPointF pointAt(double at) const;
    // Which way the line goes at a place (a unit vector; null where it has no length).
    QPointF directionAt(double at) const;
    Stroke section(int from, int to) const;
    // The line end for end: its points, its tips, its thickness (and which side is which).
    void reverse();
    void append(Stroke const& other, bool meets = true);
    // The thickness at a share of the way along the line (0 its start, 1 its end).
    ThicknessPoint sidesAtLength(double fraction) const;
    // A point taken out of the line; its thickness kept where it was along the line.
    void removePoint(int index);
    void freezeHandles();
    // (After piece `s` was cut in two at `t`: its thickness places moved to where they now are.)
    void splitThickness(int s, double t);
    // The index of the point after which a new point at `p` would go.
    int segmentAt(QPointF p) const;
    void fit(double tolerance, double cornerDegrees = 50.0);
    void setCorner(int index, bool corner);
    // Whether the line turns sharply at a point (always, on a line of straight pieces).
    bool isCorner(int index) const;
    void handlesAt(int index, QPointF& in, QPointF& out) const;
    // Gives a point its handles; the stroke becomes a curve if it was not one.
    void setHandles(int index, QPointF in, QPointF out);
    int insertPoint(QPointF p);

    QRectF bounds() const;
    // True when the stroke passes within `radius` of `p`.
    bool hit(QPointF p, double radius) const;
    QVector<QPolygonF> outlinePolygons() const;
    QVector<Stroke> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
};

struct Fill {
    QVector<QPolygonF> outlines;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    QString text;
    QString textFont;
    QPointF textAt;
    int textAlign = 0;
    double textSpacing = 1.0;
    // How far apart its letters stand, as a percentage of what the font gives.
    int textKerning = 100;
    QTransform textShape;
    int swatch = -1;
    int group = 0;
    int order = 0;
    QColor color = Qt::black;
    QVector<Stroke> contours;
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;
    bool follows = false;
    QPointF seed;
    int link = 0;

    QRectF bounds() const;
    int texture = 0;
    float textureAmount = 1.0f;
    bool textureCrisp = false;
    float textureLow = 0.0f;
    float textureHigh = 1.0f;
    double textureRamp(double grain) const
    {
        if (textureLow <= 0.0f && textureHigh >= 1.0f)
            return grain;
        double const low = textureLow, high = textureHigh;
        return high - low < 1e-4 ? (grain >= low && grain > 0.0 ? 1.0 : 0.0) : (grain <= low ? 0.0 : grain >= high ? 1.0 : (grain - low) / (high - low));
    }

    bool contains(QPointF p) const;
    // Inside it, or within `radius` of its edge.
    bool hit(QPointF p, double radius) const;
    void transform(QTransform const& t);
    // Makes `outlines` again from `contours` (after a contour was edited).
    void rebuild();
    void giveContours(double tolerance);
    QVector<Fill> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
    // The other half of the same cut: what of this area lies inside `area`, in pieces as minus() gives them (none when nothing does).
    QVector<Fill> within(QVector<QPolygonF> const& area, double tolerance) const;
    // The two areas as one, in the colour of this one; false (and nothing done) when they do not meet.
    bool mergeWith(Fill const& other, double tolerance);
    bool overlaps(Fill const& other) const;
    QVector<Fill> pieces(QVector<int> const& counts = {}) const;
    static Fill fromStroke(Stroke const& stroke, double tolerance);
    // A closed curve through the corners of a polygon, fitted with as few points as will do.
    static Stroke contourOf(QPolygonF const& polygon, double tolerance);
};

struct Picture {
    QImage image;           // null when the file of a linked picture could not be read
    QString file;
    bool linked = false;
    // The pixels a bitmap layer is painted in: made by the program, painted on by the brush.
    bool painted = false;
    int sheet = 2;
    // From the pixels of the image to the coordinates of the drawing.
    QTransform placement;
    int order = -1;
    QVector<QPolygonF> clip;
    QSize size;

    QRectF bounds() const { return placement.mapRect(QRectF(QPointF(0, 0), QSizeF(size))); }
    void transform(QTransform const& t) { placement = placement * t; }
};

struct Drawing {
    QVector<Picture> pictures;
    QVector<Fill> fills;
    QVector<Stroke> strokes;
    bool pinned = false;
    // A name of its own ("A", "blink"); without one it goes by its number in the layer.
    QString name;
    QString set;
    QString model;
    QPointF modelCentre;    // where the middle of the model is, in the drawing
    double modelScale = 1.0;    // how big, as a share of Model3D::kSize at its biggest
    enum Mark { NoMark, KeyMark, BreakdownMark, InBetweenMark };
    int mark = NoMark;
    static QColor markColour(int mark)
    {
        return mark == KeyMark ? QColor(232, 76, 61) : mark == BreakdownMark ? QColor(80, 150, 240) : mark == InBetweenMark ? QColor(90, 200, 110) : QColor(24, 24, 24);
    }
    QVector<Fill> colourFills;
    QVector<Stroke> colourStrokes;
    QVector<Fill> underlayFills;
    QVector<Stroke> underlayStrokes;
    QVector<Fill> overlayFills;
    QVector<Stroke> overlayStrokes;
    // The four sheets, from the one underneath to the one on top.
    enum Sheet { UnderlaySheet, ColourSheet, LineSheet, OverlaySheet, SheetCount };
    QVector<Fill>& fillsOf(int sheet) { return sheet == UnderlaySheet ? underlayFills : sheet == ColourSheet ? colourFills : sheet == OverlaySheet ? overlayFills : fills; }
    QVector<Fill> const& fillsOf(int sheet) const { return const_cast<Drawing*>(this)->fillsOf(sheet); }
    QVector<Stroke>& strokesOf(int sheet) { return sheet == UnderlaySheet ? underlayStrokes : sheet == ColourSheet ? colourStrokes : sheet == OverlaySheet ? overlayStrokes : strokes; }
    QVector<Stroke> const& strokesOf(int sheet) const { return const_cast<Drawing*>(this)->strokesOf(sheet); }
    bool isEmpty() const
    {
        for (int sheet = 0; sheet < SheetCount; ++sheet)
            if (!fillsOf(sheet).isEmpty() || !strokesOf(sheet).isEmpty())
                return false;
        return pictures.isEmpty() && model.isEmpty();
    }
    QRectF bounds() const;
    static int topOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    void keepJoints(Drawing const& before);
    int nextJoint() const;
    static void freshJoints(QVector<Stroke>& strokes, Drawing const& into);
    static int bottomOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    // The order a new painted area takes to lie on top of everything in the sheet.
    static int orderOnTop(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    // The colour art on its own, as a drawing.
    Drawing colourArt() const
    {
        Drawing art;
        art.fills = colourFills;
        art.strokes = colourStrokes;
        return art;
    }
};

struct Pose {
    double x = 0.0;
    double y = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    // Leaning: its uprights tipped over by so many degrees (positive: the top goes to the right).
    double skew = 0.0;
    double z = 0.0;
    double rotX = 0.0;
    double rotY = 0.0;
    double fov = 0.0;
    QVector<QPointF> quad;
    QVector<double> bend;
    QVector<QPointF> warp;
    // How opaque the layer is (0..1): keyframed like the rest, so that it can fade.
    double opacity = 1.0;
    QVector<double> fx;

    bool operator==(Pose const&) const = default;
    static Pose lerp(Pose const& a, Pose const& b, double t);
};

struct Keyframe {
    int frame = 0;
    Pose pose;
    enum Part { PartX = 1, PartY = 2, PartScaleX = 4, PartScaleY = 8, PartAngle = 16, PartOpacity = 32, PartZ = 64, PartSkew = 128,
                PartFov = 256, PartRest = 512, AllParts = 1023 };
    int parts = AllParts;
    bool sets(int part) const { return (parts & part) != 0; }
    bool tween = true;
    double easeIn = 0.0;
    double easeOut = 0.0;
    double liftIn = 0.0;
    double liftOut = 0.0;
    QVector<QPointF> controls;
    QVector<double> controlsZ;
    // The depth of the way `along` (0..1), through the control points' depths at the places `controlsAt` gives them.
    double pathDepth(double fromZ, double toZ, double along, QVector<double> const& controlsAt) const;
    double tension = 0.0;
    double continuity = 0.0;
    double bias = 0.0;
    QVector<QPointF> pathLine(QPointF from, QPointF to, QVector<double>* controlsAt = nullptr) const;
    // The place `along` (0..1) of the way.
    QPointF pathPoint(QPointF from, QPointF to, double along) const;
};

double easedProgress(double t, double easeOut, double easeIn, double liftOut = 0.0, double liftIn = 0.0);

// One colour of a gradient, and where along it that colour stands (0..1).
struct GradientStop {
    double at = 0.0;
    QColor color;
    bool operator==(GradientStop const&) const = default;
};

// A named colour of the palette.  Ids are never reused within a scene.
struct Swatch {
    int id = 0;
    QString name;
    QColor color;
    // One colour, or several running into each other: in a line, or out from a middle.
    enum Kind { Solid, Linear, Radial };
    Kind kind = Solid;
    QVector<GradientStop> stops;    // a gradient: two or more, in order
    bool isGradient() const { return kind != Solid && stops.size() >= 2; }
    QImage texture;
    bool isTexture() const { return !texture.isNull(); }
    // Whether where it lies in an area or along a line can be placed by hand (the Edit Gradient tool).
    bool isPlaced() const { return isGradient() || isTexture(); }
    // The picture as text, to keep in a file; and back.
    QString textureText() const;
    void setTextureText(QString const& text);
    // What to paint with when the gradient lies from `from` to `to`.
    QBrush brush(QPointF from, QPointF to) const;
    // The colour the gradient has at a place along it.
    static QColor colourAt(QVector<GradientStop> const& stops, double at);
    QJsonArray stopsJson() const;
    void setStopsJson(QJsonArray const& array);
    // Which of the palettes of the scene it is in (a place in Scene::paletteNames).
    int palette = 0;
    // Protected: what is drawn or painted in it is left as it is by the paint tools.
    bool isProtected = false;
};

struct Effect {
    enum Type { Blur, Tint, Cutter, Glow, Shadow, Particles, Transparency, DirectionalBlur, HueSaturation, BrightnessContrast, Grain, RadialBlur,
                ColourScale, Negate, Median, Ripple, Tone, Highlight, Bloom, Pixelate, Turbulence, MatteGenerator, BlackWhite, MatteResize, MatteBlur, Gradient,
                ChannelSelector, Crop, RemoveTransparency, MatteComposite, Blending, VariableBlur, MotionBlur, FieldChart, ColourCurves, Focus, ApplyPeg, ScaleOutput, Glue, Script, External, TypeCount };
    Type type = Blur;
    // A new effect of that kind, as it starts out.
    static Effect made(Type type);
    double hue = 0.0;
    double saturation = 0.0;
    double lightness = 0.0;
    double brightness = 0.0;
    double contrast = 0.0;
    bool enabled = true;
    // Blur, glow and shadow: the radius of the softening, in scene units.  Tint: how much, 0..1.
    double amount = 0.0;
    // The amount follows the keyframes of the layer (Pose::fx) instead of staying as set.
    bool animated = false;
    QColor color = Qt::black;   // tint: towards this colour; glow and shadow: their colour
    QPointF offset;             // shadow: where it falls, in scene units
    int matte = 0;              // cutter: the id of the layer whose drawing cuts this one; particles: the layer whose drawing
                                // they bounce off (0: none)
    bool invert = false;        // cutter: keep what lies outside the matte instead of inside
    int channel = 0;            // channel selector: 0 red, 1 green, 2 blue, 3 opacity
    QRectF area;                // crop: what is kept, in scene units
    int blendMode = 1;          // blending: a Layer::Blend (Multiply to begin with)
    QVector<QPointF> curves[4];
    QString text;               // script: the script; external: the command line
    int life = 24;
    double speed = 6.0;
    double spread = 60.0;
    double direction = 90.0;
    double gravity = 0.3;
    int seed = 1;               // another number, another scattering
    double endSize = 1.0;       // how big a particle is at the end of its life (1: as big as when it left)
    bool followLayer = false;   // the particles move with the layer; off: each stays where the layer was when it was let go
    QColor endColor;            // the colour a particle turns to by the end of its life (none: it keeps the drawing's)
    double wind = 0.0;          // pushes the particles across by so many units a frame, each frame (positive: to the right)
    double spin = 0.0;          // each turns by up to so many degrees a frame, one way or the other
    double regionWidth = 0.0;   // where they are let go from: anywhere in a box this wide and high about the drawing (0: from it)
    double regionHeight = 0.0;
    int regionShape = 0;        // the region: 0 a box, 1 a round one (an ellipse that wide and high), 2 its edge only (a ring),
                                // 3 the drawing's own shape (anywhere on its artwork; `places` gives where that is)
    double floor = 0.0;         // a floor so far below where they are let go, that they bounce off (0: none)
    double bounce = 0.5;        // how much of its speed a particle keeps when it bounces (0..1)
    double walls = 0.0;         // walls so far to the left and to the right of where they are let go, bounced off too (0: none)
    struct Particle {
        QPointF offset;         // from where the drawing itself is (was, on the frame it was let go)
        double opacity = 1.0;
        double scale = 1.0;
        double tint = 0.0;      // how far it has turned to endColor (0..1)
        double angle = 0.0;     // how far it has turned about its middle (degrees)
        int born = 0;           // the frame it was let go on
        bool operator==(Particle const&) const = default;
    };
    QVector<Particle> particles(double rate, int frame, QVector<QPointF> const& places = {},
                                std::function<QPainterPath(int born)> const& shapeFor = {}) const;

    bool operator==(Effect const&) const = default;
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

struct Layer {
    static constexpr int kNoDrawing = -1;

    enum Kind { DrawingLayer, Peg, Camera, Sound, ColourCard, Group, Bone, Quadmap };
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
    static constexpr double kDefaultFov = 41.112;
    double fov = kDefaultFov;
    // The angle on a frame: that of the keyframes when they say, else the one above.
    double fovAt(int frame) const;
    QVector<QPointF> quad;
    QSizeF quadSize;
    static QTransform quadWarp(QSizeF size, QVector<QPointF> const& quad);
    // The four corners under a pose.
    QVector<QPointF> quadAt(Pose const& pose) const;
    QString soundFile;        // a sound layer: the file it plays
    int soundStart = 0;       // and the frame it starts on
    QString lipsync;
    // The shape on a frame of the scene; null where the sound layer has none.
    QChar mouthAt(int frame) const
    {
        int const at = frame - soundStart;
        return kind == Sound && at >= 0 && at < lipsync.size() ? lipsync[at] : QChar();
    }
    QPointF pivot;            // the point scaling and rotation happen about, in layer coordinates
    QVector<Keyframe> keys;   // sorted by frame; empty means the layer never moves

    // The pose at a frame: that of the keyframes, tweened where they ask for it.
    Pose poseAt(int frame) const;
    // The index of the keyframe at exactly `frame`, or -1.
    int keyIndex(int frame) const;
    int previousKeyFrame(int frame) const;  // -1 when there is none
    int nextKeyFrame(int frame) const;
    void setKey(int frame, Pose const& pose);
    static QTransform matrix(Pose const& pose, QPointF pivot);
    // From layer coordinates to those of the parent.
    QTransform localTransform(int frame) const { return matrixFor(poseAt(frame)); }
    QTransform matrixFor(Pose const& pose) const;

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
    // Tagged: marked out, so that the Timeline can be made to show the tagged layers only.
    bool tagged = false;
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
    QVector<SkinDab> skin;
    // Values of its pose that follow other layers' (attribute controls): these take the place of its keyframes for them.
    QVector<Driver> drivers;
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
    // The colour of the layer's row in the Timeline; invalid for the usual grey.
    QColor trackColor;
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
    void substitute(int frame, int drawing);
};

struct PoseOverride {
    int layer = -1;
    int frame = -1;
    Pose pose;
    bool hasMatrix = false;
    QTransform matrix;
};

struct GraphNode {
    enum Kind { LayerNode, Composite, EffectNode, Group, GroupIn, GroupOut, Display, Note, KindCount };
    int id = 0;
    Kind kind = Composite;
    QString name;
    int layer = 0;              // a layer node: the layer's id
    int sheets = 0xf;
    Effect effect;              // an effect node: what it does (its input 0 the picture; input 1 the matte, for those that take one)
    // An effect node's amount, keyframed: so much on these frames, straight from one to the next (none: `effect.amount`).
    QMap<int, double> amountKeys;
    double amountAt(int frame) const;
    // How many inputs it has (a composite, a group, a group's output node), and outputs (a group, a group's input node).
    int ports = 1;
    int outPorts = 1;
    int group = 0;              // the group node it is in (0: none, at the top)
    QPointF at;                 // where it stands in the Node View
    // The ports it has, in and out.
    int inputs() const;
    int outputs() const;
    // An effect that takes a matte at its second input.
    static bool takesMatte(Effect::Type type);
    bool operator==(GraphNode const&) const = default;
};

struct GraphCable {
    int from = 0, fromPort = 0;     // the node (its id) and its output the cable leaves
    int to = 0, toPort = 0;         // the node and its input it goes into
    bool operator==(GraphCable const&) const = default;
};

struct NodeGraph {
    QVector<GraphNode> nodes;
    QVector<GraphCable> cables;
    bool isEmpty() const { return nodes.isEmpty(); }
    int nodeIndex(int id) const;
    GraphNode const* node(int id) const;
    // The cable into an input (an input takes one), or null.
    GraphCable const* into(int id, int port) const;
    int nextId() const;
    // The Display at the top that the frame shows (the first), its id; 0: none.
    int display() const;
    // The node of a layer (its id), 0: none.
    int layerNode(int layerId) const;
    // Whether a cable out of `from` into `to` would close a ring (`to` already feeds `from`).
    bool feeds(int to, int from) const;
    // A group's node for what comes into it, and for what goes out (ids), 0: none.
    int groupIn(int group) const;
    int groupOut(int group) const;
    bool operator==(NodeGraph const&) const = default;
};

class Scene {
public:
    Scene();

    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    // Marks on the ruler of the Timeline: a name over a frame or a run of frames.
    struct Marker {
        int frame = 0;
        int length = 1;
        QString name;
        QColor color = QColor(60, 190, 205);
        QString note;       // what there is to remember about it
        bool operator==(Marker const&) const = default;
    };
    QVector<Marker> markers;    // sorted by frame
    struct Controller {
        struct State {
            QString name;
            QVector<Pose> poses;        // one per layer of the controller
            QVector<int> drawings;      // the drawing each shows (its place in the layer), or -1: left as it is
            QPointF at;                 // a 2D point controller: where the state lies on its pad (0..1 across and down)
            bool operator==(State const&) const = default;
        };
        int id = 0;
        QString name;
        QVector<int> layers;            // the ids of the layers
        QVector<State> states;
        double value = 0.0;             // where its slider was last left (0: the first state, 1 the second ...)
        bool point = false;
        QPointF where{0.5, 0.5};
        QVector<double> weightsAt(QPointF spot) const;
        QMap<int, double> keys;
        QMap<int, QPointF> pointKeys;
        double valueAt(int frame) const;
        QPointF whereAt(int frame) const;
        bool operator==(Controller const&) const = default;
    };
    QVector<Controller> controllers;
    // The node graph, once wired by hand (empty: the layers stack as they are).
    NodeGraph graph;
    NodeGraph graphFromLayers() const;
    void graphLayerAdded(int layer);
    void graphLayerRemoved(int layerId);
    struct Write {
        QString name;
        bool movie = false;         // a movie file (its kind by its extension); otherwise images
        QString path;               // the folder of the images, or the movie file
        QString format = QStringLiteral("png");     // of the images
        int percent = 100;          // of the scene's size
        bool enabled = true;
        bool operator==(Write const&) const = default;
    };
    QVector<Write> writes;
    struct Guide {
        bool vertical = false;      // a Line: runs down the picture, at an x; else across it, at a y
        double at = 0.0;
        enum Kind { Line, Ruler, SquareGrid, Isometric, OnePoint, TwoPoint, ThreePoint, Symmetry, FourPoint, FivePoint };
        static constexpr Kind kLastKind = FivePoint;
        Kind kind = Line;
        QVector<QPointF> points;
        bool operator==(Guide const&) const = default;
    };
    QVector<Guide> guides;
    static QString guideName(Guide::Kind kind);
    // A guide of a kind, set out to suit the size of this scene.
    Guide makeGuide(Guide::Kind kind) const;
    // The ways a line may run from a place (each of length one; a line runs either way along it).
    static QVector<QPointF> guideDirections(Guide const& guide, QPointF at);
    static bool guideArc(Guide const& guide, QPointF at, int way, QPointF* centre, double* radius);
    static QVector<QPointF> guideVanishingPoints(Guide const& guide);
    // Which of those is nearest to the way the pen has gone (-1: there are none) ...
    static int nearestGuideDirection(QVector<QPointF> const& ways, QPointF drag);
    // ... and the place on the line from `from` running `way` that is nearest to `to`.
    static QPointF alongGuide(QPointF from, QPointF to, QPointF way);
    // Names and values kept with the scene.
    QVector<QPair<QString, QString>> metadata;
    // The pictures of the Reference view: the files they are read from.
    QStringList references;
    // The marker over `frame`, or -1.
    int markerAt(int frame) const;
    QColor background = Qt::white;
    QVector<Layer> layers;  // index 0 is the back
    QVector<Swatch> palette;
    bool orthographic = false;
    // The camera's angle of view is measured from side to side (horizontal fit) rather than from top to bottom.
    bool fovAcross = false;
    // Saving: scene files written packed (zlib), smaller; they are read either way.  (Set from Preferences.)
    static void setCompressFiles(bool on);
    static bool compressFiles();
    double fov = Layer::kDefaultFov;
    QStringList paletteNames = {QStringLiteral("Scene")};
    QStringList paletteLinks;
    QString paletteLink(int which) const { return paletteLinks.value(which); }
    QJsonObject paletteJson(int which) const;
    bool takePaletteJson(int which, QJsonObject const& root);
    bool writePaletteFile(int which, QString const& path) const;
    bool readPaletteFile(int which, QString const& path);

    static constexpr int kNoSwatch = -1;
    QColor resolve(int swatch, QColor const& fallback) const;
    // What an area or a line is painted with: its colour, or the gradient of its swatch.
    QBrush brushFor(Fill const& fill) const;
    QBrush brushFor(Stroke const& stroke) const;
    // Where the gradient of an area lies: where it was placed, or across the area.
    void gradientLine(Fill const& fill, QPointF& from, QPointF& to) const;
    void gradientLine(Stroke const& stroke, QPointF& from, QPointF& to) const;
    Swatch const* findSwatch(int id) const;
    // Whether the paint tools are to leave what has this colour alone.
    bool protects(int swatch) const;
    int addSwatch(QString const& name, QColor const& color, int inPalette = 0);

    int frameCount() const { return m_frameCount; }
    void setFrameCount(int count);

    int addLayer(QString const& name, int at = -1, Layer::Kind kind = Layer::DrawingLayer);
    int layerIndex(int id) const;                  // -1 when there is no such layer
    int activeCamera = 0;
    int display = 0;
    // Whether a layer is in what the display shows.
    bool inDisplay(int layer) const;
    // The camera the scene is seen through, or -1: then the scene is seen as it is.
    int cameraIndex() const;
    QVector<int> cameras() const;
    double depthOf(int layer, int frame, PoseOverride const* over = nullptr) const;
    // How far the camera stands from artwork of no depth: from the angle it sees.
    double focalDistance(int frame = 0) const;
    // Whether the layer, or something it hangs under, is turned in space on a frame.
    bool turned(int layer, int frame, PoseOverride const* over = nullptr) const;
    void toSpace(int layer, int frame, QPointF p, double& x, double& y, double& z, PoseOverride const* over = nullptr) const;
    void depthPlane(int layer, int frame, double& a, double& b, double& c) const;
    // Level with the camera or behind it: not seen at all.
    bool behindCamera(int layer, int frame) const;
    QTransform depthTransform(double z, int frame) const;
    // By how much (1: not at all).
    double depthScale(double z, int frame) const;
    QVector<int> drawOrder(int frame) const;
    // From the coordinates of the scene to those of the picture, at a frame.
    QTransform cameraTransform(int frame) const;
    int parentIndex(int layer) const;              // -1 for a layer without parent
    bool isAncestor(int ancestor, int layer) const;
    // Whether the layer is rendered: it is enabled, and so is every group it is in.
    bool enabled(int layer) const;
    // Whether the Camera view shows the layer: it is enabled, and solo if any layer is.
    bool shows(int layer) const;
    // Whether a layer it hangs under is collapsed (the Timeline then leaves it out).
    bool foldedAway(int layer) const;
    bool hasChildren(int layer) const;
    // A copy of the layer just in front of it, under the same parent.  Returns its index.
    int duplicateLayer(int layer);
    // How many layers `layer` hangs under.
    int depth(int layer) const;
    QTransform worldTransform(int layer, int frame, PoseOverride const* over = nullptr) const;
    // The pose of a layer on a frame as the scene has it: its keyframes', with the values its drivers set from other layers.
    Pose poseOf(int layer, int frame, PoseOverride const* over = nullptr) const;
    // The same without depth: where the layer lies on its own plane.
    QTransform flatTransform(int layer, int frame, PoseOverride const* over = nullptr) const;
    // Attaches `layer` to `parent` (-1 to detach).  False when that would make a loop.
    bool setParent(int layer, int parent);
    void removeLayer(int layer);
    QString uniqueLayerName(QString const& base) const;

    // ---- templates: a piece of a scene kept to be used again -----------------------------
    // These layers (and everything attached to them) over the frames `first`..`last`, as a
    // scene of its own: what a template file holds.  A layer that was moved by keyframes
    // outside those frames keeps the pose it had on `first`.
    Scene extract(QVector<int> const& layers, int first, int last) const;
    int insert(Scene const& piece, int frame);
    int pasteOnto(Scene const& piece, int frame, bool drawings = true, bool keys = true);
    static Scene pasteReady(Scene piece, bool drawings, bool keys, int room);

    QJsonObject toJson(QString const& folder = QString()) const;

    // ---- symbols ------------------------------------------------------------------------------
    // Layers kept together under a name: a character, a prop, a cycle.  A layer of the scene shows a symbol
    // (Layer::symbolId), and any number of layers may show the same one - change the symbol and they all change.
    struct Symbol {
        int id = 0;
        QString name;
        QVector<Layer> layers;      // index 0 is the back, as in a scene
        int frameCount = 1;
    };
    QVector<Symbol> symbols;
    static constexpr int kSymbolDepth = 6;      // symbols inside symbols, so deep and no deeper
    Symbol const* findSymbol(int id) const;
    // The layers of a symbol as a scene of their own: to paint, and to work on.
    Scene symbolScene(Symbol const& symbol) const;
    // All that a symbol shows on one of its frames, as one drawing.
    Drawing symbolPicture(int symbolId, int frame, int depth = 0) const;
    // Gives the layers that show a symbol (this one, or - 0 - any) their drawings anew.
    void refreshSymbolInstances(int symbolId = 0);
    void copyTiming(int from, int to);
    // After a change: the synced layers whose timing was not touched take that of the one whose timing was.
    void followSyncedTiming(Scene const& before);
    // A new drawing layer over `layer`, synced with it; and a layer synced with another (-1: with none any more).
    int addSyncedLayer(int layer);
    int insertControlPoint(int layer, int frame);
    // The frame on which the layer is nearest the control point (between the two keyframes it lies between).
    int controlPointFrame(int layer, int key, int control) const;
    // The control point becomes a keyframe on that frame (locked in time); the frame, or -1.
    int lockControlPointInTime(int layer, int key, int control);
    // After a change: the clones of a layer whose drawings or timing it touched take them over.
    void followClones(Scene const& before);
    // A clone of the layer, over it; its place, or -1.  And a clone made a layer of its own again.
    int addCloneLayer(int layer, bool timing = true);
    void makeCloneIndependent(int layer);
    bool useDrawingsOf(int layer, int source);
    // The layer whose drawings it shows (another of its drawings-only group), or -1 for its own.
    int drawingsSource(int layer) const;
    void syncLayerWith(int layer, int other);
    int makeSymbol(QVector<int> const& layers, QString const& name);
    // Another layer that shows a symbol.  Returns where it is, or -1.
    int addSymbolInstance(int symbolId, int at = -1);
    int expandSymbol(int layer);
    void takeSymbol(int symbolId, Scene const& worked);
    // A symbol with one empty drawing layer in it.  Its id.
    int addEmptySymbol(QString const& name);
    // A symbol of its own for a layer that shows one: a copy, to change without changing the other.  Its id, or 0.
    int duplicateSymbol(int layer);
    int cloneLayer(int layer);
    // The symbols these layers show - and those shown inside them, and so on.
    QVector<Symbol> symbolsUsed(QVector<Layer> const& by) const;
    static bool fromJson(QJsonObject const& json, Scene& out, QString* error, QString const& folder = QString());
    bool save(QString const& path, QString* error) const;
    static bool load(QString const& path, Scene& out, QString* error);

private:
    int m_frameCount = 24;
    int m_nextSwatchId = 1;
    int m_nextLayerId = 1;
};
