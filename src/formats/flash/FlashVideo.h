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

ECLIPSE_FORMATS_API QByteArray flv(int codec, QVector<QByteArray> const& frames, double fps, QVector<bool> const& keyframes = {});

ECLIPSE_FORMATS_API QStringList decode(int codec, QVector<QByteArray> const& frames, double fps, QString const& folder, QString const& name,
                                  QVector<bool> const& keyframes = {}, QString* error = nullptr);

// A video file (any ffmpeg reads) decoded into `folder` in the same way, frame by frame.
ECLIPSE_FORMATS_API QStringList decodeFile(QString const& input, QString const& folder, QString const& name, bool alpha = false, QString* error = nullptr);

ECLIPSE_FORMATS_API QByteArray flaMovie(QByteArray const& data);

ECLIPSE_FORMATS_API bool flaFrames(QByteArray const& data, int& codec, QVector<QByteArray>& frames, QVector<bool>& keyframes);

}
