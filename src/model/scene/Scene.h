// Eclipse - the document model.
//
// A scene is a stack of layers over a number of frames.  Each layer owns its drawings and
// an exposure sheet: for every frame, which drawing is shown (or none).  A drawing held
// over several frames is simply the same index repeated, as on a paper exposure sheet.
#pragma once

#include <QBrush>
#include <QColor>
#include <QPair>
#include <QJsonArray>
#include <QMap>
#include <QImage>
#include <QJsonObject>
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
    // The two handles of the curve at this point, as on a Bezier curve: `in` reaches back
    // towards the point before, `out` on towards the point after, both measured from the
    // point.  Without them (`handles` false) the curve finds its own way through the point.
    bool handles = false;
    QPointF in;
    QPointF out;
};

// One pen stroke, in scene coordinates (pixels of the camera frame).
struct Stroke {
    QVector<StrokePoint> points;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    // A curve passes smoothly through its points instead of going straight from one to the
    // next: a few points are then enough for a flowing line, and moving one reshapes it.
    bool curve = false;
    // A closed line returns from its last point to its first.
    bool closed = false;
    // How the outline is finished where the line turns a corner: rounded off, carried on
    // to a point (mitre), or cut flat across (bevel).  And at each of its two ends: rounded,
    // or cut off square at the end point.  A line that thins to nothing at an end comes to a
    // point there whatever the tip.
    enum Join { RoundJoin, MitreJoin, BevelJoin };
    enum Cap { RoundCap, FlatCap };
    Join join = RoundJoin;
    Cap capStart = RoundCap;  // at the first point
    Cap capEnd = RoundCap;    // at the last
    // A mitre longer than this many line widths is cut off flat, so that a very tight
    // corner does not grow a spike.
    static constexpr double kMitreLimit = 8.0;
    // True when the line is as thick at every point (so that joins and caps apply).
    bool uniform() const;
    // The palette swatch the stroke is painted with, or -1 to use `color` as it is.
    // A stroke linked to a swatch changes colour when the swatch does.
    int swatch = -1;
    // Things of one drawing sheet grouped together share a number here (0: on its own):
    // picking one of them with the Select tool picks them all.
    int group = 0;
    QColor color = Qt::black;
    float width = 4.0f;  // at full pressure
    // Drawn in a gradient colour: where the gradient lies, once it has been placed by hand (the Edit Gradient
    // tool) - as for a painted area.  Not placed, it goes across what the line covers.
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;
    // A pencil texture: the grain the line is drawn with in place of solid ink (0: none; 1 to
    // kTextureCount: chalk, charcoal, crayon, dry brush, stipple), and how much of it shows -
    // 1 the grain as it is, 0 a solid line after all.
    static constexpr int kTextureCount = 5;
    int texture = 0;
    float textureAmount = 1.0f;
    // A crisp edge: the grain frays at the edge of the line but does not fade there, and is not blurred.
    bool textureCrisp = false;
    // The ramp of the texture: grain fainter than `textureLow` is no ink at all, grain denser than
    // `textureHigh` is solid ink, and what lies between runs from the one to the other (0 and 1: the
    // grain as it is; the two alike: ink or nothing).
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

    // The line as it is drawn: the points themselves, or the curve through them cut into
    // short straight pieces.  `segmentOf`, when given, receives for each piece the index
    // of the point it starts from.
    QVector<StrokePoint> path(QVector<int>* segmentOf = nullptr) const;
    // The index of the point after which a new point at `p` would go.
    int segmentAt(QPointF p) const;
    // Drops the points a curve through the others passes within `tolerance` of, and makes
    // the stroke a curve.  What turns a wobbly run of pen samples into an editable line.
    // A turn sharper than `cornerDegrees` is kept as a corner (180 keeps none).
    void fit(double tolerance, double cornerDegrees = 50.0);
    // Makes point `index` a corner, or smooth again.  A line of straight pieces becomes a
    // curve whose other points are all corners, so only that point changes.
    void setCorner(int index, bool corner);
    // Whether the line turns sharply at a point (always, on a line of straight pieces).
    bool isCorner(int index) const;
    // The handles the curve has at a point: the ones it was given, or the ones that make the
    // curve it finds by itself.  (Nothing for a line of straight pieces.)
    void handlesAt(int index, QPointF& in, QPointF& out) const;
    // Gives a point its handles; the stroke becomes a curve if it was not one.
    void setHandles(int index, QPointF in, QPointF out);
    // A new point on the line nearest to `p`, with handles such that the line keeps its
    // shape exactly.  Returns its index, or -1.
    int insertPoint(QPointF p);

    QRectF bounds() const;
    // True when the stroke passes within `radius` of `p`.
    bool hit(QPointF p, double radius) const;
    // The area the line covers, with its thickness, its tips and its joins: one outline, or
    // more where the line crosses itself and leaves a hole (odd-even, as a painted area).
    QVector<QPolygonF> outlinePolygons() const;
    // What is left of the line when the part of it inside `area` (outlines, odd-even) is
    // rubbed out: the pieces, each a line of its own; the line itself when none of it was
    // inside.  `changed` is told whether anything was taken.
    QVector<Stroke> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
};

// A painted area: one or more closed outlines filled with the odd-even rule (so an outline
// inside another is a hole).  Fills are drawn under the strokes of their drawing.
struct Fill {
    QVector<QPolygonF> outlines;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    // Words written with the Text tool are a painted area like any other, which remembers
    // what it says, in what font (QFont::toString) and where its first line stands - so
    // that the Text tool can change the words or the font later.  Empty for other areas;
    // an area that is cut or joined is words no longer.
    QString text;
    QString textFont;
    QPointF textAt;
    // How its lines stand to that place - 0: starting there, 1: about it, 2: ending there -
    // and how far apart they are (1: as the font has it).
    int textAlign = 0;
    double textSpacing = 1.0;
    // How far apart its letters stand, as a percentage of what the font gives.
    int textKerning = 100;
    // How the words were scaled, turned or leaned after they were written, about that place:
    // typed again, they are set the same way.
    QTransform textShape;
    int swatch = -1;
    // Things of one drawing sheet grouped together share a number here (0: on its own):
    // picking one of them with the Select tool picks them all.
    int group = 0;
    QColor color = Qt::black;
    // A contour shape - what the brush makes: each outline is a closed curve with points
    // and handles (the geometry of a Stroke; its thickness and colour are not used), and
    // `outlines` is those curves drawn out.  Empty for an area that is only outlines (paint
    // from the bucket, traced pictures, text).
    QVector<Stroke> contours;
    // Painted with a gradient colour: where the gradient lies in the area, once it has been
    // placed by hand (the Edit Gradient tool) - from its first colour to its last; for a
    // round gradient, from its middle to its rim.  Not placed, it goes across the area.
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;

    QRectF bounds() const;
    // A paper texture: the grain the area shows in place of solid paint (as a line's pencil
    // texture, but lying flat), and how much of it shows.  What a textured brush leaves.
    int texture = 0;
    float textureAmount = 1.0f;
    bool textureCrisp = false;
    // The ramp of the texture: grain fainter than `textureLow` is no ink at all, grain denser than
    // `textureHigh` is solid ink, and what lies between runs from the one to the other (0 and 1: the
    // grain as it is; the two alike: ink or nothing).
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
    // What is left of the area when `area` (outlines, odd-even) is cut out of it: nothing,
    // the area itself (when they do not meet), or the pieces - each piece that stands apart
    // an area of its own.  A piece whose edge was cut gets a contour with points and
    // handles, with new points where the cut meets the old edge; outlines the cut did not
    // reach keep the points they had.
    QVector<Fill> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
    // The two areas as one, in the colour of this one; false (and nothing done) when they do not meet.
    bool mergeWith(Fill const& other, double tolerance);
    bool overlaps(Fill const& other) const;
    // The area as the separate things it is made of, each an area of its own in the same
    // colour and words no longer.  `counts` says how many outlines, in order, each one has
    // (the letters of words); when they do not add up to the outlines there are, an outline
    // goes with the outline it lies inside (a hole with its shape).
    QVector<Fill> pieces(QVector<int> const& counts = {}) const;
    // The shape a line covers, as a contour shape of the line's colour: what a brush stroke
    // becomes.  `tolerance` is how far its contour may stray from the true edge of the line.
    static Fill fromStroke(Stroke const& stroke, double tolerance);
    // A closed curve through the corners of a polygon, fitted with as few points as will do.
    static Stroke contourOf(QPolygonF const& polygon, double tolerance);
};

// A bitmap picture in a drawing (an imported image, a frame of an imported movie).  It lies
// under the paint and the lines of its drawing.
struct Picture {
    QImage image;           // null when the file of a linked picture could not be read
    // The file it came from.  A linked picture is not kept in the scene file: it is read
    // from here each time the scene is opened.
    QString file;
    bool linked = false;
    // The pixels a bitmap layer is painted in: made by the program, painted on by the brush.
    bool painted = false;
    // From the pixels of the image to the coordinates of the drawing.
    QTransform placement;
    // The size the picture had when it came in (kept so that a linked picture whose file
    // has gone still has its place).
    QSize size;

    QRectF bounds() const { return placement.mapRect(QRectF(QPointF(0, 0), QSizeF(size))); }
    void transform(QTransform const& t) { placement = placement * t; }
};

struct Drawing {
    QVector<Picture> pictures;
    QVector<Fill> fills;
    QVector<Stroke> strokes;
    // Kept in the layer even when no frame shows it: a drawing that has been substituted
    // in or out belongs to the bank of drawings a cut-out layer chooses from.
    bool pinned = false;
    // A name of its own ("A", "blink"); without one it goes by its number in the layer.
    QString name;
    // What the drawing is in the animation, when the animator has said: a key drawing, a
    // breakdown between keys, or an in-between.  Shown in the Timeline and the Xsheet.
    enum Mark { NoMark, KeyMark, BreakdownMark, InBetweenMark };
    int mark = NoMark;
    static QColor markColour(int mark)
    {
        return mark == KeyMark ? QColor(232, 76, 61) : mark == BreakdownMark ? QColor(80, 150, 240) : mark == InBetweenMark ? QColor(90, 200, 110) : QColor(24, 24, 24);
    }
    // A drawing has two sheets.  The lines above are its line art; under them is its
    // colour art, where areas are painted without touching the lines over them.
    QVector<Fill> colourFills;
    QVector<Stroke> colourStrokes;
    // And two more: an underlay beneath the colour art and an overlay over
    // the lines - for a rough, for notes, for highlights and shadows kept apart.
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
        return pictures.isEmpty();
    }
    QRectF bounds() const;
    // The colour art on its own, as a drawing.
    Drawing colourArt() const
    {
        Drawing art;
        art.fills = colourFills;
        art.strokes = colourStrokes;
        return art;
    }
};

// Where a layer sits: an offset, a scale and a rotation (degrees, clockwise on screen)
// about the pivot of the layer.
struct Pose {
    double x = 0.0;
    double y = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    // Leaning: its uprights tipped over by so many degrees (positive: the top goes to the right).
    double skew = 0.0;
    // How far the layer stands towards the camera (forward, positive) or away from it
    // (back, negative), in scene units.  What is nearer looks bigger and passes by faster
    // when the camera moves, and is drawn in front of what is further off.  For a camera:
    // where it stands on the same line - moved back it sees more, moved forward less.
    double z = 0.0;
    // Turned in space about its pivot (degrees): about the line across it - positive, its
    // top leans away from the camera - and about the line up it - positive, its right side
    // goes away.  Seen through the camera, what leans away gets smaller.
    double rotX = 0.0;
    double rotY = 0.0;
    // A camera: the angle it sees on this keyframe (degrees), or 0 for the angle the
    // camera was given (Layer::fov).
    double fov = 0.0;
    // A quadmap: how far each of its four corners is pulled from where the quadmap has
    // them (Layer::quad).  Ones not listed are not moved.
    QVector<QPointF> quad;
    // For a layer with bones: how far each bone is turned from its rest direction
    // (degrees).  Bones not listed are not turned.
    QVector<double> bend;
    // For a layer with a curve or envelope deformer: how far each of its points and handles
    // is moved from where it lies at rest (laid out as Layer::warp).  Ones not listed are not moved.
    QVector<QPointF> warp;
    // How opaque the layer is (0..1): keyframed like the rest, so that it can fade.
    double opacity = 1.0;
    // The amounts of the animated effects of the layer on this keyframe, by position in
    // the list of effects.  An effect not listed keeps the amount it was given.
    QVector<double> fx;

    bool operator==(Pose const&) const = default;
    static Pose lerp(Pose const& a, Pose const& b, double t);
};

// A pose fixed at a frame.  A motion keyframe (`tween`) glides to the next keyframe; a
// stop-motion keyframe holds its pose until the next one.
struct Keyframe {
    int frame = 0;
    Pose pose;
    bool tween = true;
    // How gently the motion arrives at this keyframe and leaves it: 0 is constant speed,
    // 1 the slowest start or stop.
    double easeIn = 0.0;
    double easeOut = 0.0;
    // The ease handles pulled up or down (free handles): how far above or below the level of this keyframe the
    // handle before it (in) and the one after it (out) stand, as a share of the way to the keyframe on that side.
    // With either, the move may run past its end and come back, or start the wrong way.
    double liftIn = 0.0;
    double liftOut = 0.0;
    // Control points: places the way from this keyframe to the next passes through, in order.  They shape
    // the path and belong to no frame - the layer comes by each when the timing brings it there.
    QVector<QPointF> controls;
    // How the path bends through its points: tension (1: straight from point to point), continuity
    // (away from 0: a corner at each point), bias (which side of a point the bend leans to); each -1..1.
    double tension = 0.0;
    double continuity = 0.0;
    double bias = 0.0;
    // The way from `from` (where this keyframe is) to `to` (where the next is) through the control points, as
    // a line of many short pieces; and how far along it (0..1) each control point lies.
    QVector<QPointF> pathLine(QPointF from, QPointF to, QVector<double>* controlsAt = nullptr) const;
    // The place `along` (0..1) of the way.
    QPointF pathPoint(QPointF from, QPointF to, double along) const;
};

// How far along a tween is (0..1, or beyond with lifted handles) when `t` of its time (0..1) has passed,
// leaving with `easeOut` and arriving with `easeIn`; `liftOut` raises the handle it leaves by, `liftIn` the
// handle it arrives by.
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
    // The colour; for a gradient, the first of its colours (what a place that cannot show a
    // gradient shows instead).
    QColor color;
    // One colour, or several running into each other: in a line, or out from a middle.
    enum Kind { Solid, Linear, Radial };
    Kind kind = Solid;
    QVector<GradientStop> stops;    // a gradient: two or more, in order
    bool isGradient() const { return kind != Solid && stops.size() >= 2; }
    // A texture colour: what is drawn or painted with it shows this picture, laid side by side, instead of
    // one colour (`color` is then the colour of the picture taken all together).
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

// Something done to the picture of a layer before it goes into the frame.  The effects of
// a layer are applied in order.
struct Effect {
    enum Type { Blur, Tint, Cutter, Glow, Shadow, Particles };
    Type type = Blur;
    bool enabled = true;
    // Blur, glow and shadow: the radius of the softening, in scene units.  Tint: how much, 0..1.
    double amount = 0.0;
    // The amount follows the keyframes of the layer (Pose::fx) instead of staying as set.
    bool animated = false;
    QColor color = Qt::black;   // tint: towards this colour; glow and shadow: their colour
    QPointF offset;             // shadow: where it falls, in scene units
    int matte = 0;              // cutter: the id of the layer whose drawing cuts this one
    bool invert = false;        // cutter: keep what lies outside the matte instead of inside
    // Particles: the layer's drawing is thrown out again and again.  `amount` is how many are let go each
    // frame; each lives `life` frames, fading; it leaves at about `speed` units a frame, within `spread`
    // degrees of `direction` (0: to the right, 90: straight up), and falls by `gravity` units a frame, each frame.
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
    double floor = 0.0;         // a floor so far below where they are let go, that they bounce off (0: none)
    double bounce = 0.5;        // how much of its speed a particle keeps when it bounces (0..1)
    struct Particle {
        QPointF offset;         // from where the drawing itself is (was, on the frame it was let go)
        double opacity = 1.0;
        double scale = 1.0;
        double tint = 0.0;      // how far it has turned to endColor (0..1)
        double angle = 0.0;     // how far it has turned about its middle (degrees)
        int born = 0;           // the frame it was let go on
        bool operator==(Particle const&) const = default;
    };
    // The particles there are on `frame` (from 0) with `rate` let go each frame: the same every time it is asked.
    QVector<Particle> particles(double rate, int frame) const;

    bool operator==(Effect const&) const = default;
};

// A morphing: the frames between the key drawing that starts on `from` and the next key
// drawing of the layer show drawings made by the program, on the way from the one to the
// other (see model/Morph.h).  It lasts as long as that exposure does.
struct Morph {
    int from = 0;
    // How gently the change leaves the first drawing and arrives at the second (0..1 each).
    double easeIn = 0.0;
    double easeOut = 0.0;
    // Hints: a place on the first drawing (p1) and the place on the second it is to go to (p2).  The shape
    // nearest the one goes to the shape nearest the other, that point of it to that point.
    QVector<QLineF> hints;
};

struct Layer {
    static constexpr int kNoDrawing = -1;

    // A peg holds no drawings and moves what is attached to it.  A camera holds none
    // either: where it is, is what the scene is seen from.
    // A sound layer plays a sound file from a frame on.
    // A colour-card fills the frame with one colour (a background).  A group holds
    // other layers and does nothing else: it has no position, and disabling it disables
    // what is in it.  A bone is a peg with a length, drawn as a bone, and bones attached
    // to it start at its tip: what hangs on it turns with it, unbent.  A quadmap pulls
    // what is attached to it by four corners, as if seen in perspective.
    enum Kind { DrawingLayer, Peg, Camera, Sound, ColourCard, Group, Bone, Quadmap };
    bool holdsDrawings() const { return kind == DrawingLayer; }

    int id = 0;  // unique in the scene and never reused: what `parent` refers to
    Kind kind = DrawingLayer;
    // The layer this one follows (its id), or 0.  A peg holds no drawings: it only moves
    // the layers attached to it.
    int parent = 0;
    // When the parent has bones: the bone of its chain this layer rides on (a hand on a
    // forearm), or -1 to follow the parent layer only.
    int parentBone = -1;
    // The joints of the bone chain the drawings bend along (at rest, in layer
    // coordinates); a chain needs two.  See model/Deform.h.
    QVector<QPointF> bones;
    // A deformer the drawings are reshaped by: a curve they bend along, or - closed - an
    // envelope round them that pulls them about.  Three entries for each of its points, at
    // rest and in layer coordinates: the point, then its two handles (measured from the
    // point) towards the point before and the point after, as on a Bezier curve.  A curve
    // needs two points, an envelope three.  See model/Deform.h.
    QVector<QPointF> warp;
    bool warpClosed = false;
    int warpPoints() const { return int(warp.size()) / 3; }
    bool hasWarp() const { return warpPoints() >= (warpClosed ? 3 : 2); }
    // Where the node of the layer stands in the Node View, once the user has put it
    // somewhere; until then the view finds it a place.
    QPointF nodePos;
    bool nodePlaced = false;
    QVector<Effect> effects;
    // The amount of effect `index` under a pose: that of the pose when the effect is
    // animated and the pose has one, else the one the effect was given.
    double effectAmount(int index, Pose const& pose) const;
    QColor cardColor = Qt::white;   // a colour-card: its colour
    double boneLength = 200.0;      // a bone: from its pivot, along its x axis
    // A bone and the IK tool: left out of it (it is not turned when a bone hanging on it is pulled), or turned
    // only between two angles (degrees, as its rotation is told).
    bool ikExcluded = false;
    bool ikLimited = false;
    double ikMin = -90.0;
    double ikMax = 90.0;
    // A camera: the angle it sees from the top of the frame to the bottom (degrees).  It
    // sets how far the camera stands from the artwork that has no depth, and so how
    // strongly depth shows: a wide angle stands close, and near things loom.
    static constexpr double kDefaultFov = 41.112;
    double fov = kDefaultFov;
    // The angle on a frame: that of the keyframes when they say, else the one above.
    double fovAt(int frame) const;
    // A quadmap: where the four corners of the rectangle `quadSize` (top left, top right,
    // bottom right, bottom left) are pulled to.
    QVector<QPointF> quad;
    QSizeF quadSize;
    static QTransform quadWarp(QSizeF size, QVector<QPointF> const& quad);
    // The four corners under a pose.
    QVector<QPointF> quadAt(Pose const& pose) const;
    QString soundFile;        // a sound layer: the file it plays
    int soundStart = 0;       // and the frame it starts on
    // Lip-sync: the shape of the mouth on each frame of the sound, a letter a frame (A to G,
    // and X for a mouth at rest; see audio/LipSync.h), counted from where the sound starts -
    // so that it goes with the sound when the sound is moved.  Empty: none.
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
    // The same under a given pose, for the kind of layer this is: nothing for a group or
    // a colour-card, the pull of the corners as well for a quadmap.
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
    // A layer that shows a symbol (Scene::symbols, by its id; 0: none): its cells say which frame of the symbol
    // each frame shows.  Its drawings are made for it - one for each frame of the symbol, all that the symbol
    // shows there in one - and are not to be drawn on: the symbol itself is what is painted.
    int symbolId = 0;
    // Synced layers: drawing layers of the same number here (0: none) keep the same timing - where one starts a
    // drawing, holds it or shows nothing, so do the others, each with drawings of its own.
    int syncGroup = 0;
    // Art layers of the drawings that are left out of the picture: bit `Drawing::Sheet` set = not shown.
    // (The one being worked on is still seen in the Camera view while only it is shown.)
    // Clones: drawing layers of the same number here (0: none) are one set of drawings and one timing seen
    // twice - what is drawn or timed on one is so on the others; each has its own place, pegs and effects.
    int cloneGroup = 0;
    // A clone that shares the drawings only: each layer of the group has a timing of its own.
    bool cloneTiming = true;
    int hiddenSheets = 0;
    // A bitmap layer: what is drawn on its line art is painted in pixels, not kept as lines.
    bool bitmap = false;
    bool sheetShown(int sheet) const { return (hiddenSheets & (1 << sheet)) == 0; }
    bool showsSymbol() const { return symbolId != 0; }
    // Frames of the layer marked with a colour in the Timeline (a note to oneself: "redo", "hold" ...).
    QMap<int, QColor> frameMarks;
    // Key exposures: frames where an exposure of its own begins although the drawing is the one the frame before
    // shows (sorted).  Where the drawing changes an exposure begins anyway, and nothing is kept here.
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
    // What a drawing is called: its name, or its number (from one).
    QString drawingLabel(int index) const
    {
        return index < 0 || index >= drawings.size() ? QString() : drawings[index].name.isEmpty() ? QString::number(index + 1) : drawings[index].name;
    }
    QVector<Morph> morphs;
    // The morphing `frame` lies in (from its first key drawing up to, not counting, its second), or -1.
    int morphIndexAt(int frame) const;
    // The frame of the second key drawing of a morphing; -1 when it has none any more.
    int morphEnd(int index) const;
    // True on the frames in between the two key drawings of a morphing.
    bool morphing(int frame) const;
    // The drawing a frame shows: that of its cell, or - in a morphing - the one in between.
    // Empty for an empty cell.
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
    // Shows drawing `drawing` from `frame` to the end of the exposure `frame` is in.  Both
    // the drawing shown before and the new one are pinned.
    void substitute(int frame, int drawing);
};

// What a view puts in place of what is stored while something is being dragged: the pose
// of one layer on one frame, or the whole matrix of that layer.
struct PoseOverride {
    int layer = -1;
    int frame = -1;
    Pose pose;
    bool hasMatrix = false;
    QTransform matrix;
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
    // Pose controllers: a set of layers and poses of them kept under names (states).  A value between two
    // states blends the poses of the two; the drawing each layer shows is the nearer state's.
    struct Controller {
        struct State {
            QString name;
            QVector<Pose> poses;        // one per layer of the controller
            QVector<int> drawings;      // the drawing each shows (its place in the layer), or -1: left as it is
            bool operator==(State const&) const = default;
        };
        int id = 0;
        QString name;
        QVector<int> layers;            // the ids of the layers
        QVector<State> states;
        double value = 0.0;             // where its slider was last left (0: the first state, 1 the second ...)
        bool operator==(Controller const&) const = default;
    };
    QVector<Controller> controllers;
    // Guides (in the coordinates of the frame), seen in the Camera view and never rendered.
    // A plain line across or down the picture is snapped to.  The other kinds are drawn
    // along: with one of them in use, a line drawn with the brush or pencil is kept straight
    // and running one of the ways the guide has at the place where it starts.
    struct Guide {
        bool vertical = false;      // a Line: runs down the picture, at an x; else across it, at a y
        double at = 0.0;
        enum Kind { Line, Ruler, SquareGrid, Isometric, OnePoint, TwoPoint, ThreePoint, Symmetry, FourPoint, FivePoint };
        static constexpr Kind kLastKind = FivePoint;
        Kind kind = Line;
        // Ruler: two points on it.  The perspectives: their vanishing points (the first two
        // of a two- or three-point one lie on the horizon).
        QVector<QPointF> points;
        bool operator==(Guide const&) const = default;
    };
    QVector<Guide> guides;
    static QString guideName(Guide::Kind kind);
    // A guide of a kind, set out to suit the size of this scene.
    Guide makeGuide(Guide::Kind kind) const;
    // The ways a line may run from a place (each of length one; a line runs either way along it).
    static QVector<QPointF> guideDirections(Guide const& guide, QPointF at);
    // The 4- and 5-point perspectives have ways that bend: way number `way` of those above is then part of a
    // circle, told here (false: that way runs straight).
    static bool guideArc(Guide const& guide, QPointF at, int way, QPointF* centre, double* radius);
    // The four vanishing points of a 4-point perspective (evenly along its horizon), or of a 5-point one
    // (left, right, above and below its middle, on its circle).
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
    // The palettes of the scene, by name; there is always one.  `palette` above holds the
    // colours of all of them, each knowing which it is in.
    // Orthographic: depth puts layers in front of and behind one another, but nothing looks bigger
    // or smaller for being nearer or further (off: in perspective, as a camera sees).
    bool orthographic = false;
    QStringList paletteNames = {QStringLiteral("Scene")};
    // A palette can be linked to a file that other scenes are linked to as well: its colours are
    // taken from the file when the scene is opened and written to it when the scene is saved.
    // One entry per palette (it may be shorter than paletteNames); empty: not linked.
    QStringList paletteLinks;
    QString paletteLink(int which) const { return paletteLinks.value(which); }
    // A palette as it is written to a palette file; and its colours brought up to those of such
    // a file: a colour of the same name takes what the file has, one the palette has not is added,
    // and those the file has not are kept.  False when it is no palette file.
    QJsonObject paletteJson(int which) const;
    bool takePaletteJson(int which, QJsonObject const& root);
    bool writePaletteFile(int which, QString const& path) const;
    bool readPaletteFile(int which, QString const& path);

    static constexpr int kNoSwatch = -1;
    // The colour to paint with: that of the swatch when `swatch` names one that exists,
    // else `fallback`.
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
    // A scene may have several cameras; it is seen through one of them.  The id of that
    // layer, or 0 for the first camera there is.
    int activeCamera = 0;
    // The Camera view's display: the id of the layer (a group, a peg ...) whose part of the scene it shows - that layer
    // and what hangs under it; 0: the whole scene.
    int display = 0;
    // Whether a layer is in what the display shows.
    bool inDisplay(int layer) const;
    // The camera the scene is seen through, or -1: then the scene is seen as it is.
    int cameraIndex() const;
    QVector<int> cameras() const;
    // How far towards the camera a layer stands on a frame: its own depth and that of
    // everything it hangs under.
    // (For a layer turned in space, or hanging under one: the depth of its pivot.)
    double depthOf(int layer, int frame, PoseOverride const* over = nullptr) const;
    // How far the camera stands from artwork of no depth: from the angle it sees.
    double focalDistance(int frame = 0) const;
    // Whether the layer, or something it hangs under, is turned in space on a frame.
    bool turned(int layer, int frame, PoseOverride const* over = nullptr) const;
    // Where a point of a layer is in space: across and down as in the scene, and how far
    // towards the camera.  Before the camera has anything to do with it.
    void toSpace(int layer, int frame, QPointF p, double& x, double& y, double& z, PoseOverride const* over = nullptr) const;
    // Level with the camera or behind it: not seen at all.
    bool behindCamera(int layer, int frame) const;
    // What depth does to something standing at `z` on a frame, in the coordinates of the
    // scene: it grows or shrinks about the middle of what the camera sees.
    QTransform depthTransform(double z, int frame) const;
    // By how much (1: not at all).
    double depthScale(double z, int frame) const;
    // The layers from the furthest to the nearest; those at one depth in their own order.
    // Those behind the camera are left out.
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
    // From the coordinates of a layer to those of the scene, through all its parents, as
    // the camera sees it: with what its depth does to it.
    // (A layer turned in space comes out in perspective.)
    QTransform worldTransform(int layer, int frame, PoseOverride const* over = nullptr) const;
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
    // Brings the layers of `piece` in, in front of everything, their frames starting at
    // `frame`; the scene grows when they run past its end.  Colours of the piece that the
    // scene has already (the same name and colour) are used; the others join the palette.
    // Returns the index of the first layer brought in, or -1 when there was none.
    int insert(Scene const& piece, int frame);
    // The same piece put onto layers the scene has already: each layer of the piece goes to the layer of
    // the same name and kind, its drawings and keyframes taking the place of what that layer had over
    // those frames.  -1, and nothing done, unless every layer of the piece has such a layer; else the
    // index of the first of them.  Without `drawings` the layers keep their exposure; without `keys`, their keyframes.
    int pasteOnto(Scene const& piece, int frame, bool drawings = true, bool keys = true);
    // A piece made ready to be pasted the way Paste Special asks: without `drawings` its layers show nothing,
    // without `keys` each keeps the pose of its first frame, held; with `room` > 0 what runs past that many
    // frames is left out.
    static Scene pasteReady(Scene piece, bool drawings, bool keys, int room);

    // `folder` is where the scene file is: the files the scene uses (linked pictures,
    // sounds) are then also written by their path from there, and looked for there when
    // they are no longer where they were - so a scene moved with its files still finds them.
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
    // Synced layers.  Gives layer `to` the timing of layer `from` (its drawings stay its own; where the timing
    // asks for one it has not got, it gets an empty one).
    void copyTiming(int from, int to);
    // After a change: the synced layers whose timing was not touched take that of the one whose timing was.
    void followSyncedTiming(Scene const& before);
    // A new drawing layer over `layer`, synced with it; and a layer synced with another (-1: with none any more).
    int addSyncedLayer(int layer);
    // Control points of the motion path.  `frame` lies between two keyframes of the layer that glide: a
    // point is put on the path where the layer is on that frame; its place among the points of that stretch, or -1.
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
    void syncLayerWith(int layer, int other);
    // Makes a symbol of these layers and everything attached to them; one layer shows it in their place.
    // Returns where that layer is, or -1.
    int makeSymbol(QVector<int> const& layers, QString const& name);
    // Another layer that shows a symbol.  Returns where it is, or -1.
    int addSymbolInstance(int symbolId, int at = -1);
    // The layer that shows a symbol becomes a peg, with copies of the symbol's layers attached to it.  Returns
    // where the peg is, or -1.
    int expandSymbol(int layer);
    // What was worked on as a scene of its own (symbolScene) becomes the symbol again: its layers and length;
    // and the colours and the other symbols as they are there now.
    void takeSymbol(int symbolId, Scene const& worked);
    // A symbol with one empty drawing layer in it.  Its id.
    int addEmptySymbol(QString const& name);
    // A symbol of its own for a layer that shows one: a copy, to change without changing the other.  Its id, or 0.
    int duplicateSymbol(int layer);
    // A second layer showing the same drawings as this one, above it: the layer is made a symbol first if it is
    // not one, and the new layer shows that symbol too - with the same timing and movement to begin with.
    // Returns where the new layer is, or -1.
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
