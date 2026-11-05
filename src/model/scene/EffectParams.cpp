// Eclipse - the named settings of each kind of effect, and the amount of the newer kinds.
#include "model/scene/Effect.h"

#include <QtGlobal>

#include <algorithm>

namespace {

using P = Effect::Param;

constexpr double kWhite = 4294967295.0;     // 0xffffffff
constexpr double kBlack = 4278190080.0;     // 0xff000000

constexpr char kChannels[] = QT_TRANSLATE_NOOP("Effect", "Red|Green|Blue|Alpha|Full Off|Full On");

QVector<P> const& table(Effect::Type type)
{
    static QVector<P> const none;
    static QVector<P> const hold = {
        {"hold", QT_TRANSLATE_NOOP("Effect", "Hold"), P::Number, 1, 100, 2, nullptr, QT_TRANSLATE_NOOP("Effect", "Each picture held for so many frames (2: on twos).")},
        {"start", QT_TRANSLATE_NOOP("Effect", "Start Frame"), P::Number, 1, 100000, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The frame the holds are counted from.")}};
    static QVector<P> const loop = {
        {"start", QT_TRANSLATE_NOOP("Effect", "Loop Start"), P::Number, 1, 100000, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The first frame of what comes in that is looped.")},
        {"end", QT_TRANSLATE_NOOP("Effect", "Loop End"), P::Number, 1, 100000, 12, nullptr, QT_TRANSLATE_NOOP("Effect", "The last frame looped.")},
        {"useOutput", QT_TRANSLATE_NOOP("Effect", "Use Output Range"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Loop only from Output Start to Output End; off, on every frame.")},
        {"outStart", QT_TRANSLATE_NOOP("Effect", "Output Start"), P::Number, 1, 100000, 1, nullptr, nullptr},
        {"outEnd", QT_TRANSLATE_NOOP("Effect", "Output End"), P::Number, 1, 100000, 100, nullptr, nullptr},
        {"resume", QT_TRANSLATE_NOOP("Effect", "Resume Behaviour"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Continue From Looped Frame|Continue From Timeline Frame"),
         QT_TRANSLATE_NOOP("Effect", "After the output range: on from the frame the loop had got to, or the frame of the timeline.")}};
    static QVector<P> const retime = {
        {"useTarget", QT_TRANSLATE_NOOP("Effect", "Retime Mode"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Frame Offset|Target Frame"),
         QT_TRANSLATE_NOOP("Effect", "Frame Offset: each frame shows the frame so many frames on. Target Frame: the frame the (animatable) Target Frame says.")},
        {"offset", QT_TRANSLATE_NOOP("Effect", "Frame Offset"), P::Number, -100000, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Frames on (or back, below 0).")}};
    static QVector<P> const directional = {
        {"falloff", QT_TRANSLATE_NOOP("Effect", "Falloff Rate"), P::Number, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast the trail fades along its length (0: evenly).")},
        {"trail", QT_TRANSLATE_NOOP("Effect", "Direction of trail"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Both Ways|Behind|Ahead"), nullptr},
        {"extraBlur", QT_TRANSLATE_NOOP("Effect", "Extra Final Blur"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "A light blur all round afterwards, to smooth the steps of the trail.")}};
    static QVector<P> const radial = {
        {"quality", QT_TRANSLATE_NOOP("Effect", "Quality"), P::Number, 0.1, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Fewer samples (less) draw faster but show steps.")}};
    static QVector<P> const variable = {
        {"blackRadius", QT_TRANSLATE_NOOP("Effect", "Black Radius"), P::Number, 0, 500, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The blur where the matte is black (the amount is the blur where it is white).")},
        {"keepInside", QT_TRANSLATE_NOOP("Effect", "Keep Inside Source Image"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The blur does not spread past the edge of what is painted.")}};
    static QVector<P> const motion = {
        {"intensity", QT_TRANSLATE_NOOP("Effect", "Intensity"), P::Number, 0, 1, 0.6, nullptr, QT_TRANSLATE_NOOP("Effect", "How strong the nearest of the trail is.")},
        {"falloff", QT_TRANSLATE_NOOP("Effect", "Fall Off"), P::Number, 0, 4, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast the trail fades (1: evenly, more: faster).")}};
    static QVector<P> const matteBlur = {
        {"blurType", QT_TRANSLATE_NOOP("Effect", "Blur Type"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Radial|Directional"), nullptr},
        {"angle", QT_TRANSLATE_NOOP("Effect", "Directional Angle"), P::Number, -360, 360, 0, nullptr, nullptr},
        {"falloff", QT_TRANSLATE_NOOP("Effect", "Directional Falloff Rate"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"useMatteColour", QT_TRANSLATE_NOOP("Effect", "Use Matte Colour"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "What is blurred takes the colour below.")},
        {"matteColour", QT_TRANSLATE_NOOP("Effect", "Colour"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr},
        {"colourGain", QT_TRANSLATE_NOOP("Effect", "Colour Gain"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"multiplicative", QT_TRANSLATE_NOOP("Effect", "Multiplicative"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The colour multiplies what is blurred instead of taking its place.")}};
    static QVector<P> const grain = {
        {"smooth", QT_TRANSLATE_NOOP("Effect", "Smooth"), P::Number, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Softer, larger grain.")},
        {"random", QT_TRANSLATE_NOOP("Effect", "Random At Each Frame"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "A new grain on each frame; off, the same grain throughout.")}};
    static QVector<P> const hueSaturation = {
        {"colorize", QT_TRANSLATE_NOOP("Effect", "Colorize HSL"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Every colour given the hue set (0 to 360) and saturation of 50% plus the saturation set, keeping its lightness.")}};
    static QVector<P> const negate = {
        {"colour", QT_TRANSLATE_NOOP("Effect", "Negate Colour"), P::Toggle, 0, 1, 1, nullptr, nullptr},
        {"alpha", QT_TRANSLATE_NOOP("Effect", "Negate Alpha"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "What was clear made opaque, and what was opaque clear.")},
        {"clamp", QT_TRANSLATE_NOOP("Effect", "Negate Colour Clamp to Alpha"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Only what is painted changes colour; off, the clear parts turn white too.")}};
    static QVector<P> const blackWhite = {
        {"matteOutput", QT_TRANSLATE_NOOP("Effect", "Matte Output"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The grey given as a matte: as opaque as it is light.")}};
    static QVector<P> const colourScale = {
        {"alpha", QT_TRANSLATE_NOOP("Effect", "Alpha"), P::Number, 0, 10, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The opacity multiplied by this.")},
        {"hue", QT_TRANSLATE_NOOP("Effect", "Hue Offset"), P::Number, -180, 180, 0, nullptr, nullptr},
        {"saturation", QT_TRANSLATE_NOOP("Effect", "Saturation"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"value", QT_TRANSLATE_NOOP("Effect", "Value"), P::Number, 0, 10, 1, nullptr, nullptr}};
    static QVector<P> const fieldChart = {
        {"fields", QT_TRANSLATE_NOOP("Effect", "Size"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "12 Field|16 Field"), nullptr},
        {"opaque", QT_TRANSLATE_NOOP("Effect", "Opaque"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "On white paper, hiding what lies under.")}};
    static QVector<P> const luminance = {
        {"greyMatte", QT_TRANSLATE_NOOP("Effect", "Output Greyscale Matte"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "What is kept given as a white matte instead of in its colours.")},
        {"gamma", QT_TRANSLATE_NOOP("Effect", "Gamma Correction"), P::Number, 0.1, 5, 1, nullptr, nullptr}};
    static QVector<P> const edges = {
        {"horizontal", QT_TRANSLATE_NOOP("Effect", "Horizontal Blurriness"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"vertical", QT_TRANSLATE_NOOP("Effect", "Vertical Blurriness"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"repeatEdges", QT_TRANSLATE_NOOP("Effect", "Repeat Edge Pixels"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Beyond the edge of the picture the edge goes on, so the edge does not fade.")}};
    static QVector<P> const box = {
        {"iterations", QT_TRANSLATE_NOOP("Effect", "Number of Iterations"), P::Whole, 1, 8, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "More passes: smoother, nearer a soft blur.")},
        edges[0], edges[1], edges[2]};
    static QVector<P> const zoom = {
        {"kind", QT_TRANSLATE_NOOP("Effect", "Blur Type"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Zoom|Radial|Spiral"), QT_TRANSLATE_NOOP("Effect", "Zoom: towards the centre; Radial: spinning round it; Spiral: both.")},
        {"x", QT_TRANSLATE_NOOP("Effect", "Centre X"), P::Number, -10000, 10000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "From the middle of the camera frame, in scene units.")},
        {"y", QT_TRANSLATE_NOOP("Effect", "Centre Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        radial[0]};
    static QVector<P> const bokeh = {
        {"shape", QT_TRANSLATE_NOOP("Effect", "Shape Type"), P::Choice, 0, 3, 0, QT_TRANSLATE_NOOP("Effect", "Disc|Hexagon|Square|Triangle"), nullptr},
        {"brightness", QT_TRANSLATE_NOOP("Effect", "Brightness Multiplier"), P::Number, 0, 20, 2, nullptr, QT_TRANSLATE_NOOP("Effect", "How much brighter the bright spots spread than the rest.")},
        {"repeatEdges", QT_TRANSLATE_NOOP("Effect", "Repeat Edges"), P::Toggle, 0, 1, 0, nullptr, nullptr}};
    static QVector<P> const fade = {
        {"target", QT_TRANSLATE_NOOP("Effect", "Target Colour"), P::Colour, 0, kWhite, 4286611584.0, nullptr, nullptr},
        {"space", QT_TRANSLATE_NOOP("Effect", "Colour Space"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "RGB|ColorSpace-HSV|ColorSpace-HSL"), nullptr},
        {"hue", QT_TRANSLATE_NOOP("Effect", "Hue Interpolation"), P::Choice, 0, 3, 1,
         QT_TRANSLATE_NOOP("Effect", "Hue-Linear|Hue-Circular-Shortest|Hue-Circular-CW|Hue-Circular-CCW"), QT_TRANSLATE_NOOP("Effect", "Which way round the colour wheel the hue goes (HSV and HSL).")}};
    static QVector<P> const override = {
        {"onlyListed", QT_TRANSLATE_NOOP("Effect", "Render"), P::Choice, 0, 2, 0,
         QT_TRANSLATE_NOOP("Effect", "All|Selected Colours and Bitmaps|Selected Colours"),
         QT_TRANSLATE_NOOP("Effect", "What is drawn: everything (the colours listed changed), or only what is in the colours listed (a colour selector), with or without the layer's bitmaps.")}};
    static QVector<P> const swap = {
        {"red", QT_TRANSLATE_NOOP("Effect", "Red Channel From"), P::Choice, 0, 5, 0, kChannels, nullptr},
        {"green", QT_TRANSLATE_NOOP("Effect", "Green Channel From"), P::Choice, 0, 5, 1, kChannels, nullptr},
        {"blue", QT_TRANSLATE_NOOP("Effect", "Blue Channel From"), P::Choice, 0, 5, 2, kChannels, nullptr},
        {"alpha", QT_TRANSLATE_NOOP("Effect", "Alpha Channel From"), P::Choice, 0, 5, 3, kChannels, nullptr}};
    static QVector<P> const levels = {
        {"channel", QT_TRANSLATE_NOOP("Effect", "Channel"), P::Choice, 0, 4, 0, QT_TRANSLATE_NOOP("Effect", "RGB|Red|Green|Blue|Alpha"), nullptr},
        {"inBlack", QT_TRANSLATE_NOOP("Effect", "Input Black"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"inWhite", QT_TRANSLATE_NOOP("Effect", "Input White"), P::Number, 0, 1, 1, nullptr, nullptr},
        {"gamma", QT_TRANSLATE_NOOP("Effect", "Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr},
        {"outBlack", QT_TRANSLATE_NOOP("Effect", "Output Black"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"outWhite", QT_TRANSLATE_NOOP("Effect", "Output White"), P::Number, 0, 1, 1, nullptr, nullptr}};
    static QVector<P> const screen = {
        {"redMin", QT_TRANSLATE_NOOP("Effect", "Red Min"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"redMax", QT_TRANSLATE_NOOP("Effect", "Red Max"), P::Number, 0, 1, 1, nullptr, nullptr},
        {"greenMin", QT_TRANSLATE_NOOP("Effect", "Green Min"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"greenMax", QT_TRANSLATE_NOOP("Effect", "Green Max"), P::Number, 0, 1, 1, nullptr, nullptr},
        {"blueMin", QT_TRANSLATE_NOOP("Effect", "Blue Min"), P::Number, 0, 1, 0, nullptr, nullptr},
        {"blueMax", QT_TRANSLATE_NOOP("Effect", "Blue Max"), P::Number, 0, 1, 1, nullptr, nullptr},
        {"outside", QT_TRANSLATE_NOOP("Effect", "Inverted"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Keep the colours outside the ranges instead.")}};
    static QVector<P> const contrast = {
        {"midPoint", QT_TRANSLATE_NOOP("Effect", "Mid Point"), P::Number, 0, 1, 0.5, nullptr, nullptr},
        {"dark", QT_TRANSLATE_NOOP("Effect", "Dark Pixel Adjustment"), P::Number, 0, 5, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How far what is darker than the mid point is pushed from it (1: not at all).")},
        {"bright", QT_TRANSLATE_NOOP("Effect", "Bright Pixel Adjustment"), P::Number, 0, 5, 1, nullptr, nullptr}};
    static QVector<P> const gamma = {
        {"rgb", QT_TRANSLATE_NOOP("Effect", "RGB Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr},
        {"red", QT_TRANSLATE_NOOP("Effect", "Red Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr},
        {"green", QT_TRANSLATE_NOOP("Effect", "Green Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr},
        {"blue", QT_TRANSLATE_NOOP("Effect", "Blue Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr},
        {"alpha", QT_TRANSLATE_NOOP("Effect", "Alpha Gamma"), P::Number, 0.1, 10, 1, nullptr, nullptr}};
    static QVector<P> const banding = {
        {"threshold1", QT_TRANSLATE_NOOP("Effect", "Threshold 1"), P::Number, 0, 1, 0.2, nullptr, QT_TRANSLATE_NOOP("Effect", "What is at least this light takes Colour 1 (darker stays as it is).")},
        {"colour1", QT_TRANSLATE_NOOP("Effect", "Colour 1"), P::Colour, 0, kWhite, 4280295488.0, nullptr, nullptr},
        {"threshold2", QT_TRANSLATE_NOOP("Effect", "Threshold 2"), P::Number, 0, 1, 0.4, nullptr, nullptr},
        {"colour2", QT_TRANSLATE_NOOP("Effect", "Colour 2"), P::Colour, 0, kWhite, 4284506256.0, nullptr, nullptr},
        {"threshold3", QT_TRANSLATE_NOOP("Effect", "Threshold 3"), P::Number, 0, 1, 0.6, nullptr, nullptr},
        {"colour3", QT_TRANSLATE_NOOP("Effect", "Colour 3"), P::Colour, 0, kWhite, 4288717008.0, nullptr, nullptr},
        {"threshold4", QT_TRANSLATE_NOOP("Effect", "Threshold 4"), P::Number, 0, 1, 0.8, nullptr, nullptr},
        {"colour4", QT_TRANSLATE_NOOP("Effect", "Colour 4"), P::Colour, 0, kWhite, 4293980415.0, nullptr, nullptr}};
    static QVector<P> const dither = {
        {"random", QT_TRANSLATE_NOOP("Effect", "Random At Each Frame"), P::Toggle, 0, 1, 1, nullptr, nullptr},
        {"seed", QT_TRANSLATE_NOOP("Effect", "Seed Value"), P::Whole, 0, 99999, 1, nullptr, nullptr}};
    static QVector<P> const shine = {
        {"threshold", QT_TRANSLATE_NOOP("Effect", "Luminance Threshold"), P::Number, 0, 1, 0.6, nullptr, QT_TRANSLATE_NOOP("Effect", "Only what is lighter than this shines.")},
        {"falloff", QT_TRANSLATE_NOOP("Effect", "Fall Off"), P::Number, 0, 1, 0.5, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast the rays fade along their length.")},
        {"x", QT_TRANSLATE_NOOP("Effect", "Light X"), P::Number, -10000, 10000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Where the light is, from the middle of the camera frame, in scene units: the rays come away from it.")},
        {"y", QT_TRANSLATE_NOOP("Effect", "Light Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"useSource", QT_TRANSLATE_NOOP("Effect", "Use Source Colour"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The rays in the colours that shine; off, in the colour below.")},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Colour"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr},
        {"gain", QT_TRANSLATE_NOOP("Effect", "Colour Gain"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"composite", QT_TRANSLATE_NOOP("Effect", "Composite with Source Image"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The rays over the picture; off, the rays alone.")}};
    static QVector<P> const flare = {
        {"x", QT_TRANSLATE_NOOP("Effect", "Flare Position X"), P::Number, -10000, 10000, -250, nullptr, QT_TRANSLATE_NOOP("Effect", "Where the light is, from the middle of the camera frame, in scene units.")},
        {"y", QT_TRANSLATE_NOOP("Effect", "Flare Position Y"), P::Number, -10000, 10000, -150, nullptr, nullptr},
        {"kind", QT_TRANSLATE_NOOP("Effect", "Flare Type"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Rings|Hexagons|Streak"), nullptr},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Colour"), P::Colour, 0, kWhite, 4294959280.0, nullptr, nullptr},
        {"screen", QT_TRANSLATE_NOOP("Effect", "Blend Mode: Normal/Screen"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "On: screened over the picture (only lighter); off: laid over it.")},
        {"blur", QT_TRANSLATE_NOOP("Effect", "Blur Intensity"), P::Number, 0, 1, 0.3, nullptr, nullptr}};
    static QVector<P> const burnIn = {
        {"colour", QT_TRANSLATE_NOOP("Effect", "Text colour"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr},
        {"box", QT_TRANSLATE_NOOP("Effect", "Add background box"), P::Toggle, 0, 1, 1, nullptr, nullptr},
        {"offset", QT_TRANSLATE_NOOP("Effect", "Frame Offset"), P::Whole, -100000, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Added to the frame number shown.")},
        {"place", QT_TRANSLATE_NOOP("Effect", "Position"), P::Choice, 0, 4, 0, QT_TRANSLATE_NOOP("Effect", "Bottom Left|Bottom Right|Top Left|Top Right|Middle"), nullptr}};
    static QVector<P> const noise = {
        {"fractal", QT_TRANSLATE_NOOP("Effect", "Fractal Type"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Fractional Brownian|Turbulent"), nullptr},
        {"frequency", QT_TRANSLATE_NOOP("Effect", "Frequency"), P::Number, 0.01, 50, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How many swirls across a hundred scene units.")},
        {"octaves", QT_TRANSLATE_NOOP("Effect", "Octaves"), P::Whole, 1, 8, 4, nullptr, QT_TRANSLATE_NOOP("Effect", "How many finer layers of noise are added.")},
        {"gain", QT_TRANSLATE_NOOP("Effect", "Gain"), P::Number, 0, 1, 0.5, nullptr, nullptr},
        {"lacunarity", QT_TRANSLATE_NOOP("Effect", "Lacunarity"), P::Number, 1, 4, 2, nullptr, nullptr},
        {"evolution", QT_TRANSLATE_NOOP("Effect", "Evolution Frequency"), P::Number, 0, 2, 0.05, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast it changes from frame to frame.")},
        {"seed", QT_TRANSLATE_NOOP("Effect", "Seed"), P::Whole, 0, 99999, 1, nullptr, nullptr},
        {"colour1", QT_TRANSLATE_NOOP("Effect", "Colour 1"), P::Colour, 0, kWhite, kBlack, nullptr, nullptr},
        {"colour2", QT_TRANSLATE_NOOP("Effect", "Colour 2"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr}};
    static QVector<P> const grid = {
        {"columns", QT_TRANSLATE_NOOP("Effect", "Columns"), P::Whole, 1, 200, 12, nullptr, nullptr},
        {"fitVertical", QT_TRANSLATE_NOOP("Effect", "Fit Vertical"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The cells counted from top to bottom instead of across.")},
        {"text", QT_TRANSLATE_NOOP("Effect", "Display Text"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Numbers along the edges.")},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Colour"), P::Colour, 0, kWhite, 4281364672.0, nullptr, nullptr}};
    static QVector<P> const refract = {
        {"smoothing", QT_TRANSLATE_NOOP("Effect", "Smoothing"), P::Number, 0, 100, 2, nullptr, QT_TRANSLATE_NOOP("Effect", "How much the height picture is softened first, in scene units.")},
        {"offset", QT_TRANSLATE_NOOP("Effect", "Height Offset"), P::Number, -1, 1, 0, nullptr, nullptr}};
    static QVector<P> const shake = {
        {"x", QT_TRANSLATE_NOOP("Effect", "Position X"), P::Number, 0, 1000, 10, nullptr, QT_TRANSLATE_NOOP("Effect", "As far as it moves either way, in scene units.")},
        {"y", QT_TRANSLATE_NOOP("Effect", "Position Y"), P::Number, 0, 1000, 10, nullptr, nullptr},
        {"rotation", QT_TRANSLATE_NOOP("Effect", "Rotation Z"), P::Number, 0, 180, 2, nullptr, nullptr},
        {"pivotX", QT_TRANSLATE_NOOP("Effect", "Pivot X"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"pivotY", QT_TRANSLATE_NOOP("Effect", "Pivot Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"frequency", QT_TRANSLATE_NOOP("Effect", "Frequency"), P::Number, 0.01, 1, 0.5, nullptr, QT_TRANSLATE_NOOP("Effect", "Shakes a frame: less, slower and smoother.")},
        {"seed", QT_TRANSLATE_NOOP("Effect", "Seed"), P::Whole, 0, 99999, 1, nullptr, nullptr}};
    static QVector<P> const quake = {
        {"move", QT_TRANSLATE_NOOP("Effect", "Move Amplitude"), P::Number, 0, 1000, 10, nullptr, nullptr},
        {"rotation", QT_TRANSLATE_NOOP("Effect", "Rotation Amplitude"), P::Number, 0, 180, 0, nullptr, nullptr},
        {"hold", QT_TRANSLATE_NOOP("Effect", "Hold Time"), P::Whole, 1, 100, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How many frames each jolt is held.")},
        {"applyX", QT_TRANSLATE_NOOP("Effect", "Apply on X"), P::Toggle, 0, 1, 1, nullptr, nullptr},
        {"applyY", QT_TRANSLATE_NOOP("Effect", "Apply on Y"), P::Toggle, 0, 1, 1, nullptr, nullptr},
        {"seed", QT_TRANSLATE_NOOP("Effect", "Seed"), P::Whole, 0, 99999, 1, nullptr, nullptr}};
    static QVector<P> const particles = {
        {"#ParticleSystem", QT_TRANSLATE_NOOP("Effect", "Particle System"), P::Heading, 0, 0, 0, nullptr, nullptr},
        {"simulate", QT_TRANSLATE_NOOP("Effect", "Simulate (Particle System)"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Simulated frame by frame, with the forces, life and looks below; off, the simple emitter above.")},
        {"maxParticles", QT_TRANSLATE_NOOP("Effect", "Maximum Number of Particles"), P::Whole, 1, 20000, 600, nullptr, nullptr},
        {"preroll", QT_TRANSLATE_NOOP("Effect", "Number of Pre-roll Frames"), P::Whole, 0, 2000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The system run so many frames before the first, so it is already going when the scene starts.")},
        {"samples", QT_TRANSLATE_NOOP("Effect", "Number of Samples per Frame"), P::Whole, 1, 16, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Steps of the simulation each frame: more follow fast moves and forces more closely.")},
        {"probability", QT_TRANSLATE_NOOP("Effect", "Probability of Generating Any Particles"), P::Number, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "The chance, frame by frame, that any are let go at all.")},
        {"sourceOffset", QT_TRANSLATE_NOOP("Effect", "Source Sampling Time Offset"), P::Whole, 0, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Particles let go from this frame on.")},
        {"sourceDuration", QT_TRANSLATE_NOOP("Effect", "Source Sampling Duration"), P::Whole, 0, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "For so many frames (0: to the end).")},
        {"globalScale", QT_TRANSLATE_NOOP("Effect", "Global Scaling Factor"), P::Number, 0.01, 100, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Every particle drawn so many times bigger.")},
        {"#Velocity", QT_TRANSLATE_NOOP("Effect", "Velocity"), P::Heading, 0, 0, 0, nullptr, nullptr},
        {"velocityType", QT_TRANSLATE_NOOP("Effect", "Velocity Type"), P::Choice, 0, 3, 0, QT_TRANSLATE_NOOP("Effect", "Random in 2D-Pie|Constant Speed|Random in all Directions 2d|Random in all Directions 3d"), QT_TRANSLATE_NOOP("Effect", "Which way they go when let go.")},
        {"minSpeed", QT_TRANSLATE_NOOP("Effect", "Min Speed"), P::Number, 0, 10, 0.6, nullptr, QT_TRANSLATE_NOOP("Effect", "The slowest, as a share of Speed.")},
        {"maxSpeed", QT_TRANSLATE_NOOP("Effect", "Max Speed"), P::Number, 0, 10, 1.4, nullptr, QT_TRANSLATE_NOOP("Effect", "The fastest, as a share of Speed.")},
        {"alignVelocity", QT_TRANSLATE_NOOP("Effect", "Align Initial Velocity"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The way they go turned with the layer.")},
        {"#Forces", QT_TRANSLATE_NOOP("Effect", "Forces"), P::Heading, 0, 0, 0, nullptr, nullptr},
        {"windY", QT_TRANSLATE_NOOP("Effect", "Friction/Wind Y"), P::Number, -50, 50, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Pushes them up or down so many units a frame, each frame (Wind pushes them across).")},
        {"friction", QT_TRANSLATE_NOOP("Effect", "Friction"), P::Number, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Slows them down: so much of their speed lost each frame.")},
        {"vortex", QT_TRANSLATE_NOOP("Effect", "Vortex"), P::Number, -50, 50, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Turns them round the Vortex Center, so many degrees a frame (less than 0: the other way).")},
        {"vortexX", QT_TRANSLATE_NOOP("Effect", "Vortex Center X"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"vortexY", QT_TRANSLATE_NOOP("Effect", "Vortex Center Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"orbitType", QT_TRANSLATE_NOOP("Effect", "Orbit Type"), P::Choice, 0, 3, 0, QT_TRANSLATE_NOOP("Effect", "None|Around Point|Around X Axis|Around Y Axis"), nullptr},
        {"orbitX", QT_TRANSLATE_NOOP("Effect", "Point X"), P::Number, -10000, 10000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The point (or the axis through it) they orbit.")},
        {"orbitY", QT_TRANSLATE_NOOP("Effect", "Point Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"aroundSpeed", QT_TRANSLATE_NOOP("Effect", "Around Speed"), P::Number, -100, 100, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast they go round it, units a frame.")},
        {"inSpeed", QT_TRANSLATE_NOOP("Effect", "In Speed"), P::Number, -100, 100, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast they are drawn in to it (less than 0: pushed out).")},
        {"explosionFrame", QT_TRANSLATE_NOOP("Effect", "Explosion Frame"), P::Whole, -1, 100000, -1, nullptr, QT_TRANSLATE_NOOP("Effect", "The frame an explosion throws them out from the Explosion point (-1: none).")},
        {"explosionX", QT_TRANSLATE_NOOP("Effect", "Explosion X"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"explosionY", QT_TRANSLATE_NOOP("Effect", "Explosion Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"explosionStrength", QT_TRANSLATE_NOOP("Effect", "Explosion Strength"), P::Number, 0, 1000, 20, nullptr, QT_TRANSLATE_NOOP("Effect", "How fast it throws them, at its middle.")},
        {"explosionRadius", QT_TRANSLATE_NOOP("Effect", "Explosion Radius"), P::Number, 1, 10000, 200, nullptr, QT_TRANSLATE_NOOP("Effect", "How far it reaches.")},
        {"repulse", QT_TRANSLATE_NOOP("Effect", "Repulse"), P::Number, -100, 100, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Pushes them away from the Repulse point (less than 0: draws them to it), units a frame at the point.")},
        {"repulseX", QT_TRANSLATE_NOOP("Effect", "Repulse X"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"repulseY", QT_TRANSLATE_NOOP("Effect", "Repulse Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"repulseRadius", QT_TRANSLATE_NOOP("Effect", "Repulse Radius"), P::Number, 1, 10000, 100, nullptr, nullptr},
        {"relativeGravity", QT_TRANSLATE_NOOP("Effect", "Relative Gravity Magnitude"), P::Number, -100, 100, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Apply Gravity between Particles: each drawn to the others (less than 0: pushed away).")},
        {"relativeDistance", QT_TRANSLATE_NOOP("Effect", "Relative Gravity Maximum Distance"), P::Number, 1, 10000, 200, nullptr, nullptr},
        {"relativeEpsilon", QT_TRANSLATE_NOOP("Effect", "Relative Gravity Epsilon"), P::Number, 0.01, 1000, 10, nullptr, QT_TRANSLATE_NOOP("Effect", "Keeps the pull from growing without end as two come close.")},
        {"follow", QT_TRANSLATE_NOOP("Effect", "Follow Intensity"), P::Number, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Make Particles Follow each Other: each steers towards the one let go before it.")},
        {"random", QT_TRANSLATE_NOOP("Effect", "Random"), P::Number, 0, 50, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "A random push each frame, up to so many units.")},
        {"#Life", QT_TRANSLATE_NOOP("Effect", "Life"), P::Heading, 0, 0, 0, nullptr, nullptr},
        {"lifeSigma", QT_TRANSLATE_NOOP("Effect", "Maximum Lifespan Sigma"), P::Number, 0, 1000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How much their lives differ, in frames.")},
        {"ageAtBirth", QT_TRANSLATE_NOOP("Effect", "Age at Birth"), P::Number, 0, 1000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How old they are already when let go, in frames.")},
        {"ageAtBirthSigma", QT_TRANSLATE_NOOP("Effect", "Age at Birth Standard Deviation"), P::Number, 0, 1000, 0, nullptr, nullptr},
        {"killOlder", QT_TRANSLATE_NOOP("Effect", "Kill Older than"), P::Whole, 0, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Those older than so many frames taken away (0: none).")},
        {"killYounger", QT_TRANSLATE_NOOP("Effect", "Kill Younger than"), P::Whole, 0, 100000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Those younger than so many frames not shown (0: none).")},
        {"sink", QT_TRANSLATE_NOOP("Effect", "Sink"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "None|Kill Inside|Kill Outside"), QT_TRANSLATE_NOOP("Effect", "Those that go into (or out of) the sink region die.")},
        {"sinkX", QT_TRANSLATE_NOOP("Effect", "Sink X"), P::Number, -10000, 10000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The sink region: a rectangle, from where they are let go.")},
        {"sinkY", QT_TRANSLATE_NOOP("Effect", "Sink Y"), P::Number, -10000, 10000, 200, nullptr, nullptr},
        {"sinkW", QT_TRANSLATE_NOOP("Effect", "Sink Width"), P::Number, 0, 100000, 2000, nullptr, nullptr},
        {"sinkH", QT_TRANSLATE_NOOP("Effect", "Sink Height"), P::Number, 0, 100000, 400, nullptr, nullptr},
        {"#Look", QT_TRANSLATE_NOOP("Effect", "Look"), P::Heading, 0, 0, 0, nullptr, nullptr},
        {"startSize", QT_TRANSLATE_NOOP("Effect", "Size of Particles"), P::Number, 0.01, 100, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How big each is when let go, as a share of the drawing.")},
        {"sizeSigma", QT_TRANSLATE_NOOP("Effect", "Size Variation"), P::Number, 0, 10, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How much their sizes differ.")},
        {"opacityByAge", QT_TRANSLATE_NOOP("Effect", "Apply Opacity Based on Age"), P::Toggle, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Fades as it grows old.")},
        {"colouring", QT_TRANSLATE_NOOP("Effect", "Colouring Strategy"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Use Drawing Colour|Use Particle Colour|Map RGB Based on Age"), QT_TRANSLATE_NOOP("Effect", "Use Drawing Colour: as drawn (to End colour, if set); Use Particle Colour: in the Particle Colour; Map RGB Based on Age: towards it as it ages.")},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Particle Colour"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr},
        {"blend", QT_TRANSLATE_NOOP("Effect", "Blend Intensity"), P::Number, 0, 1, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How far they take the Particle Colour.")},
        {"minRotation", QT_TRANSLATE_NOOP("Effect", "Minimum Initial Rotation"), P::Number, -360, 360, 0, nullptr, nullptr},
        {"maxRotation", QT_TRANSLATE_NOOP("Effect", "Maximum Initial Rotation"), P::Number, -360, 360, 0, nullptr, nullptr},
        {"alignDirection", QT_TRANSLATE_NOOP("Effect", "Align with Direction"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Each turned to the way it goes.")},
        {"directionalScale", QT_TRANSLATE_NOOP("Effect", "Directional Scale"), P::Number, 0, 10, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Each drawn longer along the way it goes, the faster the longer (a streak).")},
        {"dots", QT_TRANSLATE_NOOP("Effect", "Render as Dot"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Each a round dot rather than the drawing.")},
        {"dotSize", QT_TRANSLATE_NOOP("Effect", "Dot Size"), P::Number, 0.1, 1000, 6, nullptr, nullptr},
        {"order", QT_TRANSLATE_NOOP("Effect", "Rendering Order"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Oldest to Youngest|Youngest to Oldest"), nullptr},
        {"cycle", QT_TRANSLATE_NOOP("Effect", "Number of Drawings in Cycle"), P::Whole, 1, 1000, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "Each shows one of so many drawings of the layer, from the one shown on.")},
        {"cycleBy", QT_TRANSLATE_NOOP("Effect", "Drawing Chosen by"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Use Frame Number|Use Age|Use Particle Type"), nullptr},
        {"cycleMode", QT_TRANSLATE_NOOP("Effect", "Cycle"), P::Choice, 0, 2, 0, QT_TRANSLATE_NOOP("Effect", "Normal Cycle|Cycle Back and Forth|No Cycle"), nullptr},
        {"typeStrategy", QT_TRANSLATE_NOOP("Effect", "Type Generation Strategy"), P::Choice, 0, 1, 0, QT_TRANSLATE_NOOP("Effect", "Randomly Assign Type Number|Sequentially Assign Type Number"), nullptr},
        {"fracture", QT_TRANSLATE_NOOP("Effect", "Image Fracture"), P::Whole, 0, 64, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "The drawing broken into so many pieces across, each a particle let go once (0: off).")}};
    static QVector<P> const normalMap = {
        {"depth", QT_TRANSLATE_NOOP("Effect", "Depth"), P::Number, 0, 20, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How steeply the edges rise (more: steeper).")},
        {"flipY", QT_TRANSLATE_NOOP("Effect", "Invert Y (Normal Map Converter)"), P::Toggle, 0, 1, 0, nullptr,
         QT_TRANSLATE_NOOP("Effect", "The green channel turned over, as other programs read normal maps (Y down).")}};
    static QVector<P> const lightShader = {
        {"angle", QT_TRANSLATE_NOOP("Effect", "Light Source Position"), P::Number, -360, 360, 135, nullptr,
         QT_TRANSLATE_NOOP("Effect", "Where the light comes from, round from the right (degrees, anticlockwise: 90 from above).")},
        {"elevation", QT_TRANSLATE_NOOP("Effect", "Light Source Elevation"), P::Number, 1, 90, 45, nullptr, QT_TRANSLATE_NOOP("Effect", "How high the light is off the page (90: straight above).")},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Light Colour"), P::Colour, 0, kWhite, kWhite, nullptr, nullptr},
        {"intensity", QT_TRANSLATE_NOOP("Effect", "Adjust Light Intensity"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"depth", QT_TRANSLATE_NOOP("Effect", "Depth"), P::Number, 0, 20, 1, nullptr, QT_TRANSLATE_NOOP("Effect", "How steeply the edges rise (more: steeper).")},
        {"ambient", QT_TRANSLATE_NOOP("Effect", "Shadow Darkness"), P::Number, 0, 1, 0.25, nullptr, QT_TRANSLATE_NOOP("Effect", "How dark the side turned away from the light gets, at the darkest (0: black).")},
        {"specular", QT_TRANSLATE_NOOP("Effect", "Specular Strength"), P::Number, 0, 1, 0.3, nullptr, nullptr},
        {"shininess", QT_TRANSLATE_NOOP("Effect", "Specular Transparency"), P::Number, 1, 256, 24, nullptr, QT_TRANSLATE_NOOP("Effect", "How small and sharp the shine is.")}};
    static QVector<P> const unsharp = {
        {"strength", QT_TRANSLATE_NOOP("Effect", "Amount"), P::Number, 0, 10, 1, nullptr, nullptr},
        {"threshold", QT_TRANSLATE_NOOP("Effect", "Threshold"), P::Number, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "Differences smaller than this are left as they are.")}};
    static QVector<P> const pencil = {
        {"increment", QT_TRANSLATE_NOOP("Effect", "Incremental Texture Offset"), P::Number, 0, 1000, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "How far the grain moves along the lines each frame (0: a new random place each frame).")},
        {"most", QT_TRANSLATE_NOOP("Effect", "Max Texture Offset"), P::Number, 0, 10000, 100, nullptr, nullptr},
        {"kind", QT_TRANSLATE_NOOP("Effect", "Offset Type"), P::Choice, 0, 2, 0,
         QT_TRANSLATE_NOOP("Effect", "Offset Everything Consistently|Offset Each Drawing Uniquely|Offset Each Pencil Texture Uniquely"), nullptr},
        {"seed", QT_TRANSLATE_NOOP("Effect", "Random Seed"), P::Whole, 0, 99999, 1, nullptr, nullptr}};
    static QVector<P> const shapeCurve = {
        {"x1", QT_TRANSLATE_NOOP("Effect", "Position 1 X"), P::Number, -10000, 10000, -200, nullptr, QT_TRANSLATE_NOOP("Effect", "Where the curve starts, from the middle of the camera frame, in scene units.")},
        {"y1", QT_TRANSLATE_NOOP("Effect", "Position 1 Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"lx", QT_TRANSLATE_NOOP("Effect", "Left Handle Offset X"), P::Number, -10000, 10000, 80, nullptr, QT_TRANSLATE_NOOP("Effect", "The handle that leads the curve out of its start, from the start.")},
        {"ly", QT_TRANSLATE_NOOP("Effect", "Left Handle Offset Y"), P::Number, -10000, 10000, -120, nullptr, nullptr},
        {"x2", QT_TRANSLATE_NOOP("Effect", "Position 2 X"), P::Number, -10000, 10000, 200, nullptr, QT_TRANSLATE_NOOP("Effect", "Where the curve ends.")},
        {"y2", QT_TRANSLATE_NOOP("Effect", "Position 2 Y"), P::Number, -10000, 10000, 0, nullptr, nullptr},
        {"rx", QT_TRANSLATE_NOOP("Effect", "Right Handle Offset X"), P::Number, -10000, 10000, -80, nullptr, QT_TRANSLATE_NOOP("Effect", "The handle that leads the curve into its end, from the end.")},
        {"ry", QT_TRANSLATE_NOOP("Effect", "Right Handle Offset Y"), P::Number, -10000, 10000, -120, nullptr, nullptr},
        {"colour", QT_TRANSLATE_NOOP("Effect", "Colour"), P::Colour, 0, kWhite, 4278190080.0, nullptr, nullptr},
        {"close", QT_TRANSLATE_NOOP("Effect", "Close Shape Contour"), P::Toggle, 0, 1, 0, nullptr, QT_TRANSLATE_NOOP("Effect", "On: the curve comes back to its start and the shape it makes is filled.")}};
    switch (type) {
    case Effect::ShapeCurve: return shapeCurve;
    case Effect::Particles: return particles;
    case Effect::NormalMap: return normalMap;
    case Effect::LightShader: return lightShader;
    case Effect::HoldTiming:
    case Effect::HoldTransformationTiming: return hold;
    case Effect::Retime:
    case Effect::RetimeTransformation: return retime;
    case Effect::Loop:
    case Effect::LoopTransformation: return loop;
    case Effect::DirectionalBlur: return directional;
    case Effect::RadialBlur: return radial;
    case Effect::VariableBlur: return variable;
    case Effect::MotionBlur: return motion;
    case Effect::MatteBlur: return matteBlur;
    case Effect::Grain: return grain;
    case Effect::HueSaturation: return hueSaturation;
    case Effect::Negate: return negate;
    case Effect::BlackWhite: return blackWhite;
    case Effect::ColourScale: return colourScale;
    case Effect::FieldChart: return fieldChart;
    case Effect::LuminanceThreshold: return luminance;
    case Effect::GaussianBlur: return edges;
    case Effect::BoxBlur: return box;
    case Effect::ZoomBlur: return zoom;
    case Effect::BokehBlur: return bokeh;
    case Effect::ColourFade: return fade;
    case Effect::ColourOverride: return override;
    case Effect::ChannelSwap: return swap;
    case Effect::ColourLevels: return levels;
    case Effect::ColourScreen: return screen;
    case Effect::Contrast: return contrast;
    case Effect::Gamma: return gamma;
    case Effect::ColourBanding: return banding;
    case Effect::Dither: return dither;
    case Effect::Shine: return shine;
    case Effect::LensFlare: return flare;
    case Effect::BurnIn: return burnIn;
    case Effect::TurbulentNoise: return noise;
    case Effect::Grid: return grid;
    case Effect::Refract: return refract;
    case Effect::Shake: return shake;
    case Effect::Quake: return quake;
    case Effect::UnsharpMask: return unsharp;
    case Effect::AnimatePencilTexture: return pencil;
    default: return none;
    }
}

}  // namespace

QVector<Effect::Param> const& Effect::params(Type type)
{
    return table(type);
}

Effect::Param const* Effect::param(Type type, QString const& key)
{
    for (Param const& p : table(type))
        if (key == QLatin1String(p.key))
            return &p;
    return nullptr;
}

double Effect::value(char const* key) const
{
    QString const name = QLatin1String(key);
    if (auto const found = values.constFind(name); found != values.cend())
        return *found;
    Param const* const p = param(type, name);
    return p ? p->start : 0.0;
}

QColor Effect::colourValue(char const* key) const
{
    return QColor::fromRgba(QRgb(quint32(std::clamp(value(key), 0.0, kWhite))));
}

void Effect::setValue(QString const& key, double v)
{
    Param const* const p = param(type, key);
    if (!p)
        return;
    v = std::clamp(v, p->low, p->high);
    if (p->kind != Param::Number)
        v = std::round(v);
    if (v == p->start)
        values.remove(key);
    else
        values.insert(key, v);
}

Effect::Amount Effect::amountOf(Type type)
{
    switch (type) {
    case GaussianBlur: return {QT_TRANSLATE_NOOP("Effect", "Blurriness"), 2, 6.0, 500.0};
    case BoxBlur: return {QT_TRANSLATE_NOOP("Effect", "Radius"), 2, 6.0, 500.0};
    case ZoomBlur: return {QT_TRANSLATE_NOOP("Effect", "Blur"), 1, 0.2, 1.0};
    case BokehBlur: return {QT_TRANSLATE_NOOP("Effect", "Radius"), 2, 8.0, 300.0};
    case AntiFlicker: return {QT_TRANSLATE_NOOP("Effect", "Strength"), 2, 1.0, 50.0};
    case ColourFade: return {QT_TRANSLATE_NOOP("Effect", "Fade"), 1, 0.5, 1.0};
    case Dither: return {QT_TRANSLATE_NOOP("Effect", "Magnitude"), 3, 1.0, 16.0};
    case Shine: return {QT_TRANSLATE_NOOP("Effect", "Ray Length"), 2, 60.0, 2000.0};
    case LensFlare: return {QT_TRANSLATE_NOOP("Effect", "Intensity"), 3, 1.0, 4.0};
    case BurnIn: return {QT_TRANSLATE_NOOP("Effect", "Text Size"), 2, 24.0, 500.0};
    case TurbulentNoise: return {QT_TRANSLATE_NOOP("Effect", "Opacity"), 1, 1.0, 1.0};
    case Grid: return {QT_TRANSLATE_NOOP("Effect", "Line Width"), 3, 1.0, 20.0};
    case Refract: return {QT_TRANSLATE_NOOP("Effect", "Magnitude"), 2, 10.0, 500.0};
    case Shake:
    case Quake: return {QT_TRANSLATE_NOOP("Effect", "Amount"), 1, 1.0, 1.0};
    case UnsharpMask: return {QT_TRANSLATE_NOOP("Effect", "Radius"), 2, 3.0, 200.0};
    case IncreaseOpacity: return {QT_TRANSLATE_NOOP("Effect", "Factor"), 3, 2.0, 100.0};
    case ColourLevels:
    case Contrast:
    case Gamma:
    case ChannelSwap:
    case ColourScreen:
    case ColourBanding: return {QT_TRANSLATE_NOOP("Effect", "Amount"), 1, 1.0, 1.0};
    case LookUpTable: return {QT_TRANSLATE_NOOP("Effect", "Mix"), 1, 1.0, 1.0};
    case Retime:
    case RetimeTransformation: return {QT_TRANSLATE_NOOP("Effect", "Target Frame"), 0, 1.0, 100000.0};
    case HoldTiming:
    case HoldTransformationTiming:
    case Loop:
    case LoopTransformation: return {nullptr, 0, 1.0, 1.0};
    case ShapeCurve: return {QT_TRANSLATE_NOOP("Effect", "Line Width"), 2, 6.0, 500.0};
    case NormalMap:
    case LightShader: return {QT_TRANSLATE_NOOP("Effect", "Bevel"), 2, 8.0, 500.0};
    case ColourOverride:
    case ColourSpace:
    case AnimatePencilTexture: return {nullptr, 0, 1.0, 1.0};
    default: return {nullptr, -1, 0.0, 0.0};
    }
}
