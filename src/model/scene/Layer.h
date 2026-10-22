// Eclipse - a layer: its drawings, exposure, keys, bones, effects, morphs, drivers.
#pragma once

#include "model/scene/Effect.h"

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
    // The move, turn, scale and lean `matrix` makes `m` of about the pivot (0, 0); false when `m` flattens all onto a line.
    static bool poseOf(QTransform const& m, Pose& pose);
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
    // Palettes of clones (places in Scene::paletteNames) whose colours this layer is drawn with instead of the colours they clone.
    QVector<int> paletteOverrides;
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
