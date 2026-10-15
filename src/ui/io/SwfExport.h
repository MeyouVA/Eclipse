// Eclipse - writes the scene as a Flash movie (.swf): its drawings as vector shapes, frame by frame, through the camera.
//
// Each painted area and each line of a frame is a shape of its own, put in the movie in the order the frame paints them
// (so what lies on top stays on top); a shape the same from one frame to the next is written once and used again.
// Effects and bitmap pictures are left out (they are not vector shapes), and a layer turned in space is laid flat.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QString>

class Scene;

namespace SwfExport {

// What was left out of the movie, for the animator to be told.
struct Report {
    int frames = 0;
    int shapes = 0;             // shapes written (each once)
    bool skippedEffects = false;
    bool skippedPictures = false;
};

// The movie of frames `first` to `last` (-1: to the end) as the bytes of a .swf file.
ECLIPSE_UI_API QByteArray encode(Scene const& scene, int first = 0, int last = -1, Report* report = nullptr);
// Written to `path`; false (and why) when it cannot be.
ECLIPSE_UI_API bool write(Scene const& scene, QString const& path, QString* error = nullptr, Report* report = nullptr);

}
