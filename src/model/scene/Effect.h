// Eclipse - an effect on a layer's picture.
#pragma once

#include "model/scene/Swatch.h"

#include <QMap>
#include <QPair>

struct Effect {
    enum Type { Blur, Tint, Cutter, Glow, Shadow, Particles, Transparency, DirectionalBlur, HueSaturation, BrightnessContrast, Grain, RadialBlur,
                ColourScale, Negate, Median, Ripple, Tone, Highlight, Bloom, Pixelate, Turbulence, MatteGenerator, BlackWhite, MatteResize, MatteBlur, Gradient,
                ChannelSelector, Crop, RemoveTransparency, MatteComposite, Blending, VariableBlur, MotionBlur, FieldChart, ColourCurves, Focus, ApplyPeg, ScaleOutput, Glue, Script, External, ApplyCamera,
                GenericComposite, LayerSelector, ChromaKey, DifferenceKeyer, LuminanceThreshold, ImageSwitch, Visibility,
                GaussianBlur, BoxBlur, ZoomBlur, BokehBlur, AntiFlicker, ColourFade, ColourOverride, ChannelSwap, ColourLevels, ColourScreen, Contrast,
                Gamma, ColourBanding, Dither, Shine, LensFlare, BurnIn, TurbulentNoise, Grid, Refract, Shake, Quake, UnsharpMask, IncreaseOpacity,
                AnimatePencilTexture, ColourSpace, LookUpTable, HoldTiming, HoldTransformationTiming, Retime, RetimeTransformation,
                Loop, LoopTransformation, ShapeCurve, TypeCount };
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
    // Blurs: the radius grows and shrinks with how big depth makes the layer look through the camera.
    bool truckFactor = false;
    static bool trucks(Type t) { return t == Blur || t == DirectionalBlur || t == MatteBlur || t == RadialBlur; }
    // Apply Camera: seen through the camera (off: moved with it as with a peg); in a graph, flattened first (off: each layer in its depth).
    bool cameraViewpoint = true;
    bool flattenDepth = false;
    bool invert = false;        // cutter: keep what lies outside the matte instead of inside
    int channel = 0;            // channel selector: 0 red, 1 green, 2 blue, 3 opacity
    QRectF area;                // crop: what is kept, in scene units
    int blendMode = 1;          // blending: a Layer::Blend (Multiply to begin with)
    QVector<QPointF> curves[4];
    // Generic composite: how the colours and the opacity of the first input go onto the second, each colour scaled first.
    int colourOperation = 0;
    int alphaOperation = 0;
    double intensity[3] = {1.0, 1.0, 1.0};
    int sheets = 0xf;           // layer selector: the art layers it lets through (1 << Drawing::Sheet each)
    double softness = 0.0;      // keyers: how gradual the edge between kept and taken away is
    double clipBlack = 0.0;     // keyers: opacity below this taken away wholly, above `clipWhite` kept wholly (0..1)
    double clipWhite = 1.0;
    double spill = 0.0;         // chroma keying: how much of the key colour is taken out of what stays (0..1)
    bool showInViews = true;    // visibility: whether what comes in shows in the Camera view, and in renders
    bool showInRender = true;
    // Works only as a node of a graph wired by hand (it takes more than one picture, or a layer's art layers).
    static bool graphOnly(Type type)
    {
        return type == GenericComposite || type == LayerSelector || type == DifferenceKeyer || type == ImageSwitch || type == Visibility || type == ColourOverride
               || type == Refract || type == AnimatePencilTexture;
    }
    // A setting of a kind of effect, by name: what it is called, its range and where it starts (a colour: its 0xAARRGGBB).
    struct Param {
        enum Kind { Number, Whole, Toggle, Choice, Colour };
        char const* key;
        char const* label;
        Kind kind;
        double low, high, start;
        char const* choices;    // a choice: the names, with "|" between
        char const* tip;
    };
    static QVector<Param> const& params(Type type);
    static Param const* param(Type type, QString const& key);
    // The settings by name; only those moved from where they start are kept.
    QMap<QString, double> values;
    double value(char const* key) const;
    bool flag(char const* key) const { return value(key) != 0.0; }
    QColor colourValue(char const* key) const;
    void setValue(QString const& key, double value);
    // The amount of the newer kinds: what it is called, how it is shown (0 not at all, 1 a percentage, 2 scene units, 3 a number), where it starts and its most.
    struct Amount {
        char const* label;
        int shown;
        double start;
        double most;
    };
    static Amount amountOf(Type type);
    // Colour-Override: the colours (swatch ids) shown in another colour instead.
    QVector<QPair<int, QColor>> overrides;
    // ... or shown as a picture sequence instead: the file of its first picture, the numbered files after it the frames after.
    QMap<int, QString> overrideImages;
    QString text;               // script: the script; external: the command line
    QString fromSpace, toSpace; // Colour Space: the colour space the picture is taken to be in, and the one it is put in
    QString file;               // Look-Up Table: the LUT file
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
