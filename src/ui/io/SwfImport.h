// Eclipse - reads a Flash movie (.swf): the shapes on its main timeline, frame by frame, as drawings.
//
// The fills of a shape (solid, or the first colour of a gradient) become painted areas, its line styles lines; where a
// shape is placed (moved, scaled, turned) on each frame is kept.  Movie clips inside the movie, bitmaps, text and
// ActionScript are not read.  Compressed movies (CWS) are read; LZMA ones (ZWS) are not.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVector>

struct Drawing;

namespace SwfImport {

struct Movie {
    QSize size;                 // the frame, in pixels
    double fps = 24.0;
    QVector<Drawing> frames;    // one drawing for each frame of the main timeline
};

// False (and why) when the bytes are not a movie that can be read.
ECLIPSE_UI_API bool read(QByteArray const& bytes, Movie& out, QString* error = nullptr);

}
