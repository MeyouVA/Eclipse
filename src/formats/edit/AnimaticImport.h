// Eclipse - an animatic from an editing program (or a storyboard's own export) brought into a scene.
//
// A span of the editing program's sequence becomes the scene: the clip that is this scene (by name), another clip, a
// span of frames given, or the whole sequence.  In it:
//   - the reference images: a Ref_Images layer of pictures from the exported image sequence (the sequence frame by
//     frame, a picture a frame) - those on the first frame of each clip, at the markers, or every one - or, without
//     one, the first picture of each clip whose media is a picture;
//   - the sound: a sound layer for each audio track, its clips where they were;
//   - the storyboard: a shot for each clip, a panel from each marker on it;
//   - the scene's markers from the sequence's.
// Media that cannot be found are looked for, by name, in the folders given (the Locate Media window asks for them).
#pragma once

#include "formats/edit/EditTimeline.h"

#include <QStringList>

class Scene;

namespace AnimaticImport {

struct Options {
    enum Timespan { SceneClip, OtherClip, Custom, Whole };
    int timespan = SceneClip;
    QString sceneName;          // SceneClip: the clip named as the scene
    QString clip;               // OtherClip: the one chosen
    int in = 0, duration = 0;   // Custom: from frame `in` of the sequence, so many frames
    enum Images { NoImages, FirstFrameOfClips, Markers, AllImages, ClipPictures };
    int images = FirstFrameOfClips;
    QString imageFolder;        // the exported image sequence (a picture a frame of the sequence)
    bool replaceRefImages = true;   // the Ref_Images layer there is replaced; else a new one beside it
    bool sound = true;
    bool shots = true;          // the storyboard made from the clips
    bool markers = true;        // the sequence's markers as the scene's
    bool rate = true;           // the scene takes the sequence's frame rate (else frames are converted to its own)
    bool length = true;         // the scene made as long as the span
    QStringList searchFolders;  // where missing media are looked for
};

struct Result {
    int in = 0, duration = 0;   // the span of the sequence taken
    int imageLayer = -1;        // the Ref_Images layer (-1: no pictures)
    int images = 0;
    QVector<int> soundLayers;
    int shots = 0;
    QStringList missing;        // media that could not be found
};

// The clip names a span can be chosen by (video clips, in order, each once).
QStringList clipNames(EditTimeline::Sequence const& sequence);
// The span the options take (false, with `error`: no clip of that name, or nothing there).
bool span(EditTimeline::Sequence const& sequence, Options const& options, int* in, int* duration, QString* error = nullptr);
// The pictures of an exported image sequence, in order (a picture a frame).
QStringList imageSequence(QString const& folder);
bool isPicture(QString const& file);
// A media file, or the one of its name in one of the folders (empty: none).
QString locate(QString const& file, QStringList const& folders);
// The media of the sequence that cannot be found (after looking in the folders).
QStringList missingMedia(EditTimeline::Sequence const& sequence, QStringList const& folders);

// Into the scene (one step for its undo: the caller wraps it).  false, with `error`, when nothing was taken.
bool apply(Scene& scene, EditTimeline::Sequence const& sequence, Options const& options, Result* result = nullptr, QString* error = nullptr);

// The other way: the scene's storyboard as a sequence for an editing program - a clip a shot, playing `movie` (the scene
// exported as a movie) from the shot's first frame, the shots' transitions, the sound layers' clips on audio tracks, and
// the scene's markers.  Without a storyboard, one clip for the whole scene.
EditTimeline::Sequence toSequence(Scene const& scene, QString const& movie, QString const& name);

// Import as Scenes: each video clip its own scene file in `folder` (named after the clip), made from `base` as `apply`
// makes one; the files written.
QStringList splitIntoScenes(Scene const& base, EditTimeline::Sequence const& sequence, Options const& options, QString const& folder, QString* error = nullptr);

}
