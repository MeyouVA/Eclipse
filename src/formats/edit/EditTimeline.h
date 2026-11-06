// Eclipse - an editing program's timeline, read and written: Final Cut Pro XML (xmeml), CMX 3600 edit decision lists
// (EDL) and OpenTimelineIO (.otio).
//
// A sequence is clips on video and audio tracks, placed in its frames (start .. end, the frame after the last), each
// playing a file from its `in` frame; transitions between clips on a track; markers on the sequence and on clips.
// Clips nested in another sequence are kept as clips that name the sequence they play.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace EditTimeline {

struct Marker {
    int frame = 0;          // in the sequence (or, on a clip, in the clip's file)
    int length = 0;         // 0: a point
    QString name, comment;
    bool operator==(Marker const&) const = default;
};

struct Clip {
    QString id;             // the file's id in Final Cut XML (two clips of one file share it)
    QString name;
    QString file;           // its media, a local path (empty: none, or a nested sequence)
    int track = 0;          // the track, from 0 (video and audio counted apart)
    bool audio = false;
    int start = 0, end = 0; // where it lies in the sequence
    int in = 0;             // the file's frame it starts from
    bool enabled = true;
    QString nested;         // the sequence it plays, when it is a nested one
    QVector<Marker> markers;
    int length() const { return end - start; }
    bool operator==(Clip const&) const = default;
};

struct Transition {
    int track = 0;
    bool audio = false;
    int start = 0, end = 0;
    QString name;           // "Cross Dissolve", "Wipe" ...
    bool operator==(Transition const&) const = default;
};

struct Sequence {
    QString name;
    double fps = 24.0;
    bool ntsc = false;      // its rate is timebase * 1000 / 1001 (29.97 ...)
    bool dropFrame = false; // its time codes count dropped frames
    int duration = 0;       // frames (0: as long as its clips)
    int width = 0, height = 0;
    QVector<Clip> clips;
    QVector<Transition> transitions;
    QVector<Marker> markers;

    int length() const;     // `duration`, or the end of the last clip
    int timebase() const;   // the whole-number rate its time codes count in (24, 25, 30)
    QVector<Clip> videoClips() const;   // in order of start (the lowest track first where they start together)
    QVector<Clip> audioClips() const;
    int tracks(bool audio) const;
    // The video clip named so (nullptr: none).
    Clip const* clipNamed(QString const& name) const;
    // Every media file it uses, once each.
    QStringList files() const;
    bool operator==(Sequence const&) const = default;
};

// ---- Final Cut Pro XML ----
// Every sequence in the file (nested ones too); none, with `error`, when it is not one.
QVector<Sequence> readFcpXml(QByteArray const& data, QString* error = nullptr);
QByteArray writeFcpXml(Sequence const& sequence);

// ---- CMX 3600 edit decision list ----
// `fps`: what the list's time codes count in (they do not say; 0: 24, or 29.97 when it says DROP FRAME).
Sequence readEdl(QByteArray const& data, double fps, QString* error = nullptr);
// The video events then the audio ones (A for the first audio track, A2 the second ...); record times from 01:00:00:00.
QByteArray writeEdl(Sequence const& sequence, bool audio = true);

// ---- OpenTimelineIO ----
Sequence readOtio(QByteArray const& data, QString* error = nullptr);
QByteArray writeOtio(Sequence const& sequence);

// A file of any of them, by what it holds; relative media found beside it.
QVector<Sequence> read(QString const& path, double fps, QString* error = nullptr);
QString fileFilter();

// ---- time codes ----
// hh:mm:ss:ff (with `dropFrame`, hh:mm:ss;ff counting the dropped frame numbers of 29.97 and 59.94).
QString timecode(qint64 frame, double fps, bool dropFrame = false);
// Back to frames; -1 when it is not one.
qint64 frames(QString const& timecode, double fps, bool dropFrame = false);

// file://localhost/C:/a%20b.mov -> C:/a b.mov, and back.
QString fileFromUrl(QString const& url);
QString urlFromFile(QString const& file);

}
