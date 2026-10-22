// Eclipse - reads a Flash movie (.swf): what its main timeline shows, frame by frame, as drawings.
//
// The fills of a shape (solid colours and gradients) become painted areas, its line styles lines (with
// their caps and joins); bitmaps (JPEG, with or without an alpha channel, and lossless ones) come as pictures where a
// shape is filled with them; static text as the shapes of its letters, from the movie's own fonts.  Where each thing is
// placed (moved, scaled, turned) on each frame is kept, as are masks and colour effects.  Frame labels and sounds (MP3 or
// uncompressed, streamed or started on a frame) are read too, and video (decoded by ffmpeg, when it is installed).
// Movie clips inside the movie and ActionScript are not run.  Compressed movies (CWS) are read; LZMA ones (ZWS) are not.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

struct Drawing;
struct Swatch;

namespace SwfImport {

struct Label {
    int frame = 0;
    QString name;
};

// A sound of the movie, as a file of its own would hold it: an MP3, or a WAV made of the movie's samples.
struct Sound {
    QByteArray bytes;
    QString suffix;             // "mp3" or "wav"
    int frame = 0;              // the frame it starts on
};

struct Movie {
    QSize size;                 // the frame, in pixels
    double fps = 24.0;
    QVector<Drawing> frames;    // one drawing for each frame of the main timeline
    // The gradients the drawings are painted with: a fill whose swatch is not -1 is painted with gradients[swatch].
    QVector<Swatch> gradients;
    QVector<Label> labels;
    QVector<Sound> sounds;
    int skippedSounds = 0;      // sounds in a form Eclipse does not read (ADPCM, Nellymoser, Speex)
    int skippedVideo = 0;       // video streams that could not be decoded
    QString videoProblem;       // and why
    int scripts = 0;            // pieces of ActionScript (not run)
};

// A WAV file of so many channels of samples (8-bit unsigned or 16-bit little-endian, as `bits` says).
ECLIPSE_FORMATS_API QByteArray wav(QByteArray const& samples, int rate, int bits, int channels);

// False (and why) when the bytes are not a movie that can be read.  The frames of its video are decoded into
// `mediaFolder` and shown from there (linked pictures); without one, they are decoded in passing and kept in the drawings.
// `progress`, when given, is told how far the reading has come (so much done of so much in all) now and then; false from
// it stops the reading (and it fails, as stopped).
using Progress = std::function<bool(qint64 done, qint64 all)>;
ECLIPSE_FORMATS_API bool read(QByteArray const& bytes, Movie& out, QString* error = nullptr, QString const& mediaFolder = QString(),
                         Progress const& progress = {});

}
