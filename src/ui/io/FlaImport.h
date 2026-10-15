// Eclipse - reads a Flash document (.fla, the XFL kind Flash CS5 and after save: a zip of XML): its layers, frame by
// frame, as drawings.
//
// Each layer of the main timeline becomes a layer, its drawing on each frame what the layer shows there: shapes (fills -
// solid colours and gradients - and lines with their caps and joins), groups, rectangle (rounded or not) and oval objects,
// and graphic symbols, movie clips and buttons drawn as they look on that frame (their own timelines followed, nested
// ones and their masks too), where they are placed, with their colour effects; classic motion tweens (with their eases,
// custom ones too, about the transformation point) and shape tweens are followed between keyframes.  Guide layers and
// folders are left out; a mask layer comes as a hidden layer the layers under it are cut by.  Bitmaps (JPEG, or Flash's
// own; bitmap fills too) come as pictures in their place among what is drawn, text as the shapes of its letters (in its
// font, bold or italic, aligned), video (decoded by ffmpeg, when it is installed) as a picture a frame.  Frame labels and
// scripts, the colour and lock of each layer, the sounds on the main timeline and the document's swatches are read too.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QColor>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

struct Drawing;
struct Swatch;

namespace FlaImport {

// A filter of Flash's on a symbol: a glow, a blur or a drop shadow (as Flash gives them: sizes in pixels).
struct Filter {
    QString kind;               // "GlowFilter", "BlurFilter", "DropShadowFilter"
    double blurX = 5.0, blurY = 5.0;
    QColor colour = Qt::black;
    double strength = 1.0;      // (1: as strong as it comes)
    double distance = 0.0;      // a shadow: how far it falls, at so many degrees
    double angle = 45.0;
    bool inner = false;
};

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
    QColor colour;              // its colour in Flash's timeline
    bool mask = false;          // a mask layer: it cuts the layers it masks, and is not seen itself
    int maskedBy = -1;          // the layer (in `layers`) that masks this one, or -1
    QVector<Drawing> drawings;  // the different drawings it shows
    QVector<int> cells;         // for each frame: which of them (or -1: none)
    // What on this Flash layer was drawn with a blend mode ("add", "multiply"...; empty: none) or filters comes on a layer
    // of its own, over the layer it was on, with them.
    QString blend;
    QVector<Filter> filters;
};

// A label of a frame of the main timeline, over so many frames.
struct Label {
    int frame = 0;
    int length = 1;
    QString name;
    bool comment = false;       // a comment rather than a name
    QString note;               // what there is to say about it (a frame's script)
};

// A sound of the main timeline, as a file of its own would hold it.
struct Sound {
    QString name;               // its name in the library
    QByteArray bytes;
    QString suffix;             // "wav" or "mp3"
    int frame = 0;              // the frame it starts on
};

struct Document {
    QSize size;
    double fps = 24.0;
    QColor background = Qt::white;
    int frames = 0;
    QVector<Layer> layers;      // from the back to the front
    // The gradients the drawings are painted with: a fill whose swatch is not -1 is painted with gradients[swatch].
    QVector<Swatch> gradients;
    QVector<Label> labels;
    QVector<Sound> sounds;
    // The colours of the document's swatches panel (its solid swatches, each once), and its gradient swatches.
    QVector<QColor> swatches;
    QVector<Swatch> swatchGradients;
    int skippedPictures = 0;    // bitmaps that could not be read
    int skippedVideo = 0;       // videos that could not be decoded
    QString videoProblem;       // and why
    int skippedSounds = 0;      // sounds in a form Eclipse does not read
};

// False (and why) when the bytes are not a Flash document that can be read.  The frames of its video are decoded into
// `mediaFolder` and shown from there (linked pictures); without one, they are decoded in passing and kept in the drawings.
// `progress`, when given, is told how far the reading has come (so much done of so much in all) now and then; false from
// it stops the reading (and it fails, as stopped).
using Progress = std::function<bool(qint64 done, qint64 all)>;
ECLIPSE_UI_API bool read(QByteArray const& bytes, Document& out, QString* error = nullptr, QString const& mediaFolder = QString(),
                         Progress const& progress = {});

}
