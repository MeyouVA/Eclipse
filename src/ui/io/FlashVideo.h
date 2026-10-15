// Eclipse - the video inside Flash files (embedded Sorenson H.263 or On2 VP6, as a .swf or a .fla keeps it), decoded
// into pictures by ffmpeg.
//
// The frames are put in an FLV stream (the form ffmpeg reads them in) and decoded to numbered image files in a folder:
// JPEGs, or PNGs for video with an alpha channel.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace FlashVideo {

// The kinds of video Flash embeds (its codec numbers).
enum Codec { SorensonH263 = 2, ScreenVideo = 3, VP6 = 4, VP6Alpha = 5, ScreenVideo2 = 6 };

// The frames as an FLV stream of so many frames a second.  `keyframes` (as long as `frames`, or empty: only the first)
// says which frames stand on their own.
ECLIPSE_UI_API QByteArray flv(int codec, QVector<QByteArray> const& frames, double fps, QVector<bool> const& keyframes = {});

// The frames decoded into `folder`, named `<name> 00001.jpg` and on (a frame that could not be decoded has no file); in
// order of the frames.  Empty, with why, when ffmpeg is not installed or could not decode them.
ECLIPSE_UI_API QStringList decode(int codec, QVector<QByteArray> const& frames, double fps, QString const& folder, QString const& name,
                                  QVector<bool> const& keyframes = {}, QString* error = nullptr);

// The frames of a video item of a .fla (its file in bin/: Flash's own form - a table of where each frame lies, then the
// frames one after another): their data, which stand on their own, and the codec.  False when it is not that form.
ECLIPSE_UI_API bool flaFrames(QByteArray const& data, int& codec, QVector<QByteArray>& frames, QVector<bool>& keyframes);

}
