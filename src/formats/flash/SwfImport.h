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
#include <QColor>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVector>

#include <functional>

#include "formats/flash/FlashFilter.h"

struct Drawing;
struct Swatch;
class Scene;

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

// What one depth of the main timeline shows (read as Tracks): each thing put there drawn once, and where it is on each frame.
struct Track {
    QString name;               // the instance name, or the name ActionScript knows what is put there by, or the depth
    int depth = 0;
    QVector<Drawing> drawings;  // about the registration point of what is drawn, as big as it first shows
    QVector<int> cells;         // for each frame: which of them, or -1
    QVector<QTransform> places; // for each frame: where it is put (in pixels, from the top left corner of the movie)
    QVector<double> opacity;    // for each frame, 0..1 (a colour effect that only fades: the rest are drawn in)
    bool mask = false;          // a mask: not seen itself, it cuts the tracks of `maskedBy`
    QVector<int> maskedBy;      // the tracks (places in Movie::tracks) of the masks over this one
    QString blend;              // as Flash names it ("multiply", "screen", ...), or empty
    QVector<Flash::Filter> filters;
};

enum class Layout {
    Frames,     // Movie::frames: the whole picture, a drawing a frame
    Tracks,     // Movie::tracks: a track a depth, to be animated on pegs
};

struct Movie {
    QSize size;                 // the frame, in pixels
    double fps = 24.0;
    QColor background;          // (invalid: the movie gives none)
    int frameCount = 0;
    QVector<Drawing> frames;    // Layout::Frames: one drawing for each frame of the main timeline
    QVector<Track> tracks;      // Layout::Tracks: from the back to the front
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

using Progress = std::function<bool(qint64 done, qint64 all)>;
ECLIPSE_FORMATS_API bool read(QByteArray const& bytes, Movie& out, QString* error = nullptr, QString const& mediaFolder = QString(),
                         Progress const& progress = {}, Layout layout = Layout::Frames);

// A movie read in tracks as a template to put in a scene of that size: a peg fitting it into the frame, a peg a track
// (with `groups`, each track and its peg in a group of their own).
ECLIPSE_FORMATS_API Scene rig(Movie const& movie, QString const& name, QSize frame, bool groups = false);

}
