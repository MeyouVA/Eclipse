// Eclipse - the Flash movie (.swf) reader: what it keeps while it reads, and what it does.
#pragma once

#include <QHash>
#include <QImage>
#include <QPair>
#include <QStringList>
#include <QTemporaryDir>
#include <QTransform>
#include <QVector>

#include "formats/flash/SwfImport.h"
#include "formats/flash/SwfParts.h"
#include "model/scene/Scene.h"

namespace swf {

class Reader {
public:
    using Movie = SwfImport::Movie;
    using Sound = SwfImport::Sound;
    using Label = SwfImport::Label;

    Reader(QByteArray const& data, Movie& out, QString const& mediaFolder, SwfImport::Progress const& progress)
        : data(data), out(out), mediaFolder(mediaFolder), progress(progress)
    {
        stream.frame = -1;
    }

    // 1. What the movie defines, and each timeline as its display list stands on each of its frames (its tags from `p` on).
    bool readTags(int p);
    // 2. The video, each stream decoded (into the media folder, or in passing).
    bool decodeVideo();
    // 3. Each frame drawn, from the movie's coordinates (`toPixels`) into pixels.
    bool drawFrames(QTransform const& toPixels);
    // Why it stopped, when it did.
    QString why;

private:
    struct Stream {
        int codec = 0;
        QSize size;
        QVector<QByteArray> frames;
    };
    // A movie clip's own sounds: an event sound (by its id) or its streamed soundtrack, and the frame of the clip it starts on.
    struct ClipSound {
        int soundId = -1;
        Sound sound;
        int frame = 0;
    };

    // (Of a thousand: the first fifth reading, the next decoding video, the rest drawing the frames.)
    bool told(qint64 done) const { return !progress || progress(done, 1000); }
    void label(int frame, QString const& name);
    // A video frame (the main timeline's, or a movie clip's).
    void videoFrame(QByteArray const& body);
    // A movie clip: its own timeline, its tags inside.
    void readClip(QByteArray const& body);
    // The sounds the movie clips start, on the frames of the main timeline they start on.
    void clipSounds(DisplayList const& shown, int age, int mainFrame, int depth);

    int gradientOf(Swatch const& s);
    QImage tintedImage(QImage const& image, ColourFx const& fx);
    // A shape drawn into `d`, placed by `place`, coloured by `fx`, at `order` among what is drawn.
    void shapeInto(Shape const& s, QTransform const& place, ColourFx const& fx, Drawing& d, int order);
    // A drawing put into another: placed, coloured, over what is there.
    void placeInto(Drawing const& inner, QTransform const& place, ColourFx const& fx, Drawing& d, int& order);
    // What a character looks like where it is placed (in the coordinates of the timeline it is on), on frame `frame` of it.
    void drawPlaced(Placed const& placed, int frame, Drawing& d, int& order, int depth);
    void drawList(DisplayList const& shown, int frame, Drawing& d, int& order, int depth);

    QByteArray const& data;
    Movie& out;
    QString mediaFolder;
    SwfImport::Progress progress;

    QHash<int, Stream> streams;
    QHash<int, Shape> shapes;
    QHash<int, MorphShape> morphs;
    QHash<int, Font> fonts;
    QHash<int, QImage> bitmaps;
    QHash<int, QVector<DisplayList>> sprites;       // movie clips: their frames
    QHash<int, QVector<ClipSound>> spriteSounds;
    QByteArray jpegTables;
    QHash<int, Sound> sounds;           // event sounds, by their ids
    QHash<QString, int> classes;        // ids by the names ActionScript knows them by
    int streamFormat = -1;              // the sound streamed along with the frames: its form, and what has come of it
    Sound stream;
    QByteArray streamed;
    QVector<DisplayList> mainFrames;
    DisplayList list;

    QTemporaryDir passing;
    QHash<int, QStringList> decoded;

    QHash<QByteArray, int> gradientKeys;    // a gradient of `out.gradients`, by what it is
    QHash<QString, QImage> videoImages;
    QHash<QByteArray, QImage> tintedImages;
    QHash<QPair<int, int>, Drawing> spriteFrames;
    QHash<QPair<int, int>, Shape> morphShapes;
};

}  // namespace swf
