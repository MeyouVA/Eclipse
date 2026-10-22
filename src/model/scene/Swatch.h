// Eclipse - a colour of the palette, plain or a gradient.
#pragma once

#include "model/scene/Keyframe.h"

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
