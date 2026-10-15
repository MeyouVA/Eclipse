// Eclipse - reads a Flash document (.fla, the XFL kind Flash CS5 and after save: a zip of XML): its layers, frame by
// frame, as drawings.
//
// Each layer of the main timeline becomes a layer, its drawing on each frame what the layer shows there: shapes (fills -
// a solid colour, a gradient's first - and lines), groups, rectangle and oval objects, and graphic symbols, movie clips
// and buttons drawn as they look on that frame (their own timelines followed, nested ones too), where they are placed,
// with their colour effects; classic motion tweens are followed between keyframes.  Guide layers and folders are left
// out; a mask layer comes as a hidden layer the layers under it are cut by.  Text, bitmaps, video and sound are not read.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QColor>
#include <QSize>
#include <QString>
#include <QVector>

struct Drawing;

namespace FlaImport {

struct Layer {
    QString name;
    bool visible = true;
    bool mask = false;          // a mask layer: it cuts the layers it masks, and is not seen itself
    int maskedBy = -1;          // the layer (in `layers`) that masks this one, or -1
    QVector<Drawing> drawings;  // the different drawings it shows
    QVector<int> cells;         // for each frame: which of them (or -1: none)
};

struct Document {
    QSize size;
    double fps = 24.0;
    QColor background = Qt::white;
    int frames = 0;
    QVector<Layer> layers;      // from the back to the front
    int skippedText = 0;        // text, bitmaps and video left out (how many times one was met)
    int skippedPictures = 0;
};

// False (and why) when the bytes are not a Flash document that can be read.
ECLIPSE_UI_API bool read(QByteArray const& bytes, Document& out, QString* error = nullptr);

}
