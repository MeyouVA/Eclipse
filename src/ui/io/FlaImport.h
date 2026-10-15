// Eclipse - reads a Flash document (.fla, the XFL kind Flash CS5 and after save: a zip of XML): its layers, frame by
// frame, as drawings.
//
// Each layer of the main timeline becomes a layer, its drawing on each frame what the layer shows there: shapes (fills -
// solid colours and gradients - and lines with their caps and joins), groups, rectangle (rounded or not) and oval objects,
// and graphic symbols, movie clips and buttons drawn as they look on that frame (their own timelines followed, nested
// ones and their masks too), where they are placed, with their colour effects; classic motion tweens (with their eases,
// custom ones too, about the transformation point) and shape tweens are followed between keyframes.  Guide layers and
// folders are left out; a mask layer comes as a hidden layer the layers under it are cut by.  Bitmaps (JPEG, or Flash's
// own) come as pictures, text as the shapes of its letters (in its font, bold or italic, aligned).  Frame labels, the
// colour and lock of each layer and the sounds on the main timeline are read too.  Video is not.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QColor>
#include <QSize>
#include <QString>
#include <QVector>

struct Drawing;
struct Swatch;

namespace FlaImport {

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
    QColor colour;              // its colour in Flash's timeline
    bool mask = false;          // a mask layer: it cuts the layers it masks, and is not seen itself
    int maskedBy = -1;          // the layer (in `layers`) that masks this one, or -1
    QVector<Drawing> drawings;  // the different drawings it shows
    QVector<int> cells;         // for each frame: which of them (or -1: none)
};

// A label of a frame of the main timeline, over so many frames.
struct Label {
    int frame = 0;
    int length = 1;
    QString name;
    bool comment = false;       // a comment rather than a name
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
    int skippedPictures = 0;    // bitmaps that could not be read
    int skippedVideo = 0;       // video, left out (how many times one was met)
    int skippedSounds = 0;      // sounds in a form Eclipse does not read
};

// False (and why) when the bytes are not a Flash document that can be read.
ECLIPSE_UI_API bool read(QByteArray const& bytes, Document& out, QString* error = nullptr);

}
