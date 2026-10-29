// Eclipse - what the effects of a layer do to its picture.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

#include "model/scene/Scene.h"

namespace Effects {

// Softens a picture (premultiplied ARGB) by about `radius` pixels.
void blur(QImage& image, double radius);
// Moves every colour towards `color` by `amount` (0..1), leaving the shape as it is.
void tint(QImage& image, QColor const& color, double amount);

void halo(QImage& image, QColor const& color, double radius, QPointF offset);

// Makes the picture `amount` (0..1) see-through.
void transparency(QImage& image, double amount);
// Smears the picture `length` pixels along a direction (degrees, 0: across, 90: up and down).
void directionalBlur(QImage& image, double length, double degrees);
// Turns the hue by `hue` degrees, and raises the saturation and the lightness by so many percent; `amount` of it (0..1).
void hueSaturation(QImage& image, double hue, double saturation, double lightness, double amount);
// So many percent brighter and more contrast; `amount` of it (0..1).
void brightnessContrast(QImage& image, double brightness, double contrast, double amount);
// A grain over the picture (where it is painted), `amount` strong (0..1); `seed` says which grain.
void grain(QImage& image, double amount, quint32 seed);
// Each point smeared along the line from the middle of what is painted, `amount` (0..1) of how far it is from it.
void radialBlur(QImage& image, double amount);
// Each colour multiplied by `scale` (its red, green and blue as shares), `amount` of the way (0..1).
void colourScale(QImage& image, QColor const& scale, double amount);
// The colours turned to their opposites, `amount` of the way (0..1).
void negate(QImage& image, double amount);
// Each point the middle colour (and opacity) of those within `radius` pixels: specks go, edges stay.
void median(QImage& image, double radius);
// The rows of the picture rocked side to side by waves `height` pixels high and `length` pixels apart, moved on by `frame`.
void ripple(QImage& image, double height, double length, int frame);
// The bright parts (lighter than about two thirds) blurred `radius` pixels and added back over the picture: a glow of light.
void bloom(QImage& image, double radius);
// Blocks `size` pixels across, each the mean colour of what lies in it.
void pixelate(QImage& image, double size);
void turbulence(QImage& image, double strength, double size, int frame);
void matteGenerator(QImage& image, double inner, double outer, QColor const& colour);
// The colours made grey (as light as they were), `amount` of the way (0..1).
void blackWhite(QImage& image, double amount);
// What is painted grown by `radius` pixels all round (shrunk, with `shrink`): each point the strongest (weakest) of those round it.
void matteResize(QImage& image, double radius, bool shrink);
void gradient(QImage& image, QColor const& from, QColor const& to, double degrees, bool radial, double amount);

QString name(Effect const& effect, Scene const& scene);
void channelSelect(QImage& image, int channel);
// Only what lies inside a rectangle of the image (in its pixels) is kept.
void crop(QImage& image, QRectF const& keep);
// What is more opaque than `threshold` (0..1) made wholly opaque, the rest taken away.
void removeTransparency(QImage& image, double threshold);
// The picture made a white matte of itself, as opaque as it is.
void matteComposite(QImage& image);
// `top` laid over `under` as a blend mode says (a Layer::Blend): the two together, in `under`.
void blendOnto(QImage& under, QImage const& top, int mode);
// Blurred `radius` where the matte is white, less where it is darker, not at all where there is none.
void variableBlur(QImage& image, QImage const& matte, double radius);
void fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width);
QVector<int> curveLevels(QVector<QPointF> const& points);
// Each colour level changed as the curves say (the three together, then red, green and blue), `amount` of the way (0..1).
void colourCurves(QImage& image, QVector<QPointF> const (&curves)[4], double amount);
// The image moved by a transform (in its pixels).
void transformImage(QImage& image, QTransform const& transform);
// The image made at `scale` (0..1) of its resolution, then brought back to its size.
void scaleOutput(QImage& image, double scale);
void glue(QImage& under, QImage const& top, double radius);
QString runScript(QImage& image, QString const& script, int frame, double amount, double unit);
QString runExternal(QImage& image, QString const& command, int frame, int seconds = 60);
// `top` put onto `under` by one of the generic composite's colour operations (Effect::colourOperation) and alpha operations, its colours scaled first.
void genericComposite(QImage& under, QImage const& top, int colourOperation, int alphaOperation, double const (&intensity)[3], double opacity);
QStringList colourOperations();
QStringList alphaOperations();
// Takes away what is near the key colour (nearer than `threshold`, partly so for `softness` more), and as much of that colour from what stays as `spill` says.
void chromaKey(QImage& image, QColor const& key, double threshold, double softness, double clipBlack, double clipWhite, double spill, bool invert);
// Takes away what is the same as the clean picture behind it (differs by less than `threshold`).
void differenceKey(QImage& image, QImage const& clean, double threshold, double softness, double clipBlack, double clipWhite, bool invert);
// Keeps what is lighter than `threshold` (0..1), the rest taken away (inverted: what is darker).
void luminanceThreshold(QImage& image, double threshold, double softness, double clipBlack, double clipWhite, bool invert);

// What a newer kind of effect is called (Blur-Gaussian, Colour-Fade and so on).
QString kindName(Effect::Type type);
// The name of the scene, for Burn-In (set by the window as the scene is opened and saved).
void setSceneName(QString const& name);
QString sceneName();

// What an effect needs to know beside its picture, for those done the same way on a layer and as a node.
struct Context {
    double amount = 0.0;
    double pixelsPerUnit = 1.0;     // pixels of the picture to a scene unit
    QRectF frame;                   // the camera frame, in the picture's pixels
    int at = 0;                     // the frame drawn
    double fps = 24.0;
    QImage second;                  // what comes in at a node's second input (none for a layer's effect)
};
// Does `effect` to `image` when it is one of those; false when it is not (it is done where it is drawn).
bool apply(QImage& image, Effect const& effect, Context const& context);

// Blurs: the newer ones and the settings added to the older ones.
void boxBlur(QImage& image, double radiusX, double radiusY, int passes, bool repeatEdges);
// Smeared towards (kind 0), round (1) or both ways about (2) a centre, `amount` (0..1) strong.
void zoomBlur(QImage& image, QPointF centre, double amount, int kind, double quality);
// A blur through a shape (0 disc, 1 hexagon, 2 square, 3 triangle), the bright spots spreading `brightness` times more.
void bokehBlur(QImage& image, double radius, int shape, double brightness, bool repeatEdges);
// Directional Blur with its trail fading (`falloff`, 0..1) and going both ways (0), behind (1) or ahead (2).
void directionalBlur(QImage& image, double length, double degrees, double falloff, int trail);
void radialBlur(QImage& image, double amount, double quality);
// Sharper: the difference from a blur of `radius` added back, `strength` times, where it is more than `threshold`.
void unsharpMask(QImage& image, double radius, double strength, double threshold);

// Colours: the newer ones and the settings added to the older ones.
// Towards `target`, `amount` of the way, in RGB (0), HSV (1) or HSL (2), the hue going linearly (0), the shorter way (1), clockwise (2) or not (3).
void colourFade(QImage& image, QColor const& target, double amount, int space, int hueWay);
// Each channel (red, green, blue, alpha) from one of: red, green, blue, alpha, nothing, full.
void channelSwap(QImage& image, int const (&from)[4]);
void levels(QImage& image, int channel, double inBlack, double inWhite, double gamma, double outBlack, double outWhite, double amount);
// Keeps what lies within the ranges of red, green and blue (min and max of each), or with `outside` what does not.
void colourScreen(QImage& image, double const (&range)[6], bool outside);
void contrast(QImage& image, double midPoint, double dark, double bright, double amount);
void gamma(QImage& image, double rgb, double red, double green, double blue, double alpha, double amount);
// What is at least as light as threshold k takes colour k (darker than the first: as it was).
void colourBanding(QImage& image, double const (&thresholds)[4], QColor const (&colours)[4]);
void dither(QImage& image, double magnitude, quint32 seed);
void negate(QImage& image, double amount, bool colour, bool alpha, bool clamp);
// The picture as a matte of its grey: as opaque as it is light, white.
void greyMatte(QImage& image);
void colourScaleMore(QImage& image, double alpha, double hue, double saturation, double value);
// Every colour given one hue (degrees) and saturation, keeping its lightness, `amount` of the way.
void colorize(QImage& image, double hue, double saturation, double lightness, double amount);
void increaseOpacity(QImage& image, double factor);
void grain(QImage& image, double amount, quint32 seed, double smooth);

// Light, generators and moves.
// Rays from the bright parts (lighter than `threshold`) going away from `light`, `length` pixels long.
void shine(QImage& image, QPointF light, double length, double threshold, double falloff, QColor const& colour, double gain, bool composite);
void lensFlare(QImage& image, QPointF light, QRectF const& frame, int kind, QColor const& colour, double intensity, bool screen, double blur);
// The text of a Burn-In for a frame: %f the frame, %F the frame with the offset, %s the scene, %t the time code, %e and %j left as they are.
QString burnInText(QString const& text, int frame, double fps, QString const& scene);
void burnIn(QImage& image, QString const& text, QRectF const& frame, double size, QColor const& colour, bool box, int place);
void turbulentNoise(QImage& image, QRectF const& frame, double pixelsPerUnit, Effect const& e, int at, double opacity);
void grid(QImage& image, QRectF const& frame, int columns, bool fitVertical, bool text, QColor const& colour, double width);
void fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width, int fields, bool opaque);
// Each point taken from where the slope of `height` (its lightness) bends it, `magnitude` pixels at the steepest.
void refract(QImage& image, QImage const& height, double magnitude, double smoothing, double offset);
// Variable Blur with a blur where the matte is black too, and kept inside what is painted.
void variableBlur(QImage& image, QImage const& matte, double white, double black, bool keepInside);
// Matte Blur's blurred picture: blurred round or one way, and coloured, as its settings say.
void matteBlurred(QImage& image, Effect const& e, double radius);
// The lines and areas of a drawing in the colours a Colour-Override gives instead (only those, with Only the Colours Listed).
void overrideColours(Drawing& drawing, Effect const& e);
// Where Shake and Quake put the picture on a frame: a move (pixels) and a turn (degrees) about a pivot.
QTransform shaken(Effect const& e, int at, double amount, double pixelsPerUnit, QPointF pivot);

}
