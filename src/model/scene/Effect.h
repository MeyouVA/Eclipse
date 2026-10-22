// Eclipse - an effect on a layer's picture.
#pragma once

#include "model/scene/Swatch.h"

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
