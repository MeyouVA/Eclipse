// Eclipse - writes the scene as a Flash movie (.swf): each drawing a shape written once, moved about frame by frame.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

class Scene;

namespace SwfExport {

struct Options {
    int first = 0;
    int last = -1;                  // (-1: to the end)
    double fps = 0.0;               // (0: the scene's)
    bool compress = true;           // the movie packed (zlib)
    bool protect = false;           // marked as not to be imported by Flash
    int jpegQuality = 80;           // pictures: as JPEG of this quality, or (100) as they are, without loss
    bool linesAsAreas = true;       // every line as the area it covers (as it looks), not as a line of Flash's
    bool fixedLineWidth = false;    // lines of Flash's as thick however the movie is scaled
    bool sound = true;
    // What is turned into Flash's own colour effects, filters, blend modes and masks (false: left out).
    bool transparency = true;
    bool colourEffects = true;      // tints and colour scales
    bool blurs = true;
    bool glows = true;
    bool shadows = true;
    bool blendModes = true;
    bool masks = true;              // cutters
};

// What was written, and what was left out (for the animator to be told).
struct Report {
    int frames = 0;
    int shapes = 0;                 // shapes written (each once)
    int bitmaps = 0;
    bool sound = false;
    QStringList leftOut;            // what Flash has no like of, or the options left out, each once
    bool skippedEffects = false;
    bool skippedPictures = false;
};

// The movie as the bytes of a .swf file.
ECLIPSE_FORMATS_API QByteArray encode(Scene const& scene, Options const& options, Report* report = nullptr);
// Frames `first` to `last`, with the other options as they come.
ECLIPSE_FORMATS_API QByteArray encode(Scene const& scene, int first = 0, int last = -1, Report* report = nullptr);
// Written to `path`; false (and why) when it cannot be.
ECLIPSE_FORMATS_API bool write(Scene const& scene, QString const& path, Options const& options, QString* error = nullptr, Report* report = nullptr);
ECLIPSE_FORMATS_API bool write(Scene const& scene, QString const& path, QString* error = nullptr, Report* report = nullptr);

}
