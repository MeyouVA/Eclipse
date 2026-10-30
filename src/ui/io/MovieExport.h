// Eclipse - writes the scene as a movie file (picture through the camera, and the sound
// layers mixed), by handing the frames to ffmpeg.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class Scene;

namespace MovieExport {

// What of the scene goes into the movie, and how.
struct Options {
    int first = 0;          // the first frame, counted from 0
    int last = -1;          // the last; -1: to the end of the scene
    double scale = 1.0;     // the size of the picture, as a share of the scene's
    int quality = 18;       // for the movies that have one: lower is finer (18 fine, 23 medium, 28 small)
    // Automatic: ProRes 4444 for MOV, H.264 elsewhere; explicit codecs are 0 H.264, 1 HEVC, 2 ProRes 422 HQ, 3 ProRes 4444.
    int codec = -1;
    // How long the encoder takes over it: 0 fast, 1 medium, 2 slow (a smaller file for the same quality).
    int speed = 1;
    int audioKbps = 192;    // the sound's bit rate
    bool transparentBackground = false;
    QString colourSpace;    // the colour space the frames are put in from the working space (empty: as they are)
};

// What ffmpeg is told about the encoding of a movie of that kind (by its extension), with sound or without.
QStringList encoderArguments(QString const& suffix, Options const& options, bool sound);

bool write(Scene const& scene, QString const& path, std::function<bool(int frame)> const& progress, QString* error,
           Options const& options = Options());

}
