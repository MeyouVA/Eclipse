// Eclipse - reading Photoshop files (PSD or PSB): the flattened picture, or the layers one by one.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>

namespace Psd {

// The picture the file shows (its layers as merged when it was saved); null, with `error` set, when it cannot be read.
QImage read(QString const& file, QString* error = nullptr);
// Whether a file is one, by its first bytes.
bool isPsd(QString const& file);

// One record of the layers, from the bottom up: a group is a GroupEnd record under its layers and a GroupStart one (named) above them.
struct Layer {
    enum Kind { Pixels, GroupStart, GroupEnd, Adjustment };
    Kind kind = Pixels;
    QString name;
    QImage image;               // its pixels, the size of its box, masks and clipping already applied (null: none)
    QPoint at;                  // where its box's top left is in the picture
    double opacity = 1.0;       // its opacity times its fill opacity
    bool visible = true;
    bool closed = false;        // a group folded shut
    QByteArray blend = "norm";  // the four letters of its blend mode
    bool clipped = false;       // clipped to the layer under it (already cut to it)
    bool styled = false;        // it has layer styles (shadows, strokes ...), which are not in its pixels
};
struct Layered {
    QSize size;
    QVector<Layer> layers;
};
// The layers of the file; false, with `error` set, when they cannot be read. A file without layers gives its flat picture as one.
bool readLayers(QString const& file, Layered& out, QString* error = nullptr);

}
