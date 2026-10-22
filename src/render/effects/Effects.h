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

}
