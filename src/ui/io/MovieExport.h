// Eclipse - writes the scene as a movie file (picture through the camera, and the sound
// layers mixed), by handing the frames to ffmpeg.
#pragma once

#include <QString>

#include <functional>

class Scene;

namespace MovieExport {

// What of the scene goes into the movie, and how.
struct Options {
    int first = 0;          // the first frame, counted from 0
    int last = -1;          // the last; -1: to the end of the scene
    double scale = 1.0;     // the size of the picture, as a share of the scene's
    int quality = 18;       // for the movies that have one: lower is finer (18 fine, 23 medium, 28 small)
};

// Writes `path` (the kind of movie follows its extension: .mp4, .mov, .webm, .gif).
// `progress` is called before each frame with its index and may return false to stop.
// False, with a message, on failure or when stopped.
bool write(Scene const& scene, QString const& path, std::function<bool(int frame)> const& progress, QString* error,
           Options const& options = Options());

}
