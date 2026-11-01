// Eclipse - sound: reading sound files, mixing the sound layers of a scene, writing WAV.
//
// Everything is kept as 16-bit stereo at 44100 Hz.  Sound files other than plain WAV are
// decoded by ffmpeg when it is installed; the scene itself only stores the path.
#pragma once

#include <QString>
#include <QByteArray>
#include <QPair>
#include <QVector>

#include <memory>

class Scene;
struct Layer;
struct SoundEffect;

struct SoundData {
    static constexpr int kRate = 44100;

    QVector<qint16> samples;  // left, right, left, right, ...
    QByteArray contentHash;
    QVector<QPair<qint64,qint64>> ranges;
    bool hasSamples(qint64 first,qint64 end) const;

    qint64 frames() const { return samples.size() / 2; }
    double seconds() const { return double(frames()) / kRate; }
    // The loudest sample (0..1) among `count` sample frames from `first`.
    float peak(qint64 first, qint64 count) const;
};

namespace Sound {

// Where ffmpeg is, or an empty string when it is not installed.
QString findFfmpeg();

// The decoded file; read once and remembered.  Null, with a message, when it cannot be read.
std::shared_ptr<SoundData> load(QString const& path, QString* error = nullptr);

// A sound layer's track: its clips placed on its frames (frame 0 at sample 0), looped, their gain and envelope,
// then the layer's gain and its effects in order.  Null when it has no clip that can be read (the first message in `error`).
std::shared_ptr<SoundData> track(Layer const& layer, double fps, QString* error = nullptr);
// An effect on a block of stereo floats (left, right, ...); the effects that change speed change its length.
void applyEffect(SoundEffect const& effect, QVector<float>& stereo);

// All the audible sound layers of the scene, mixed, as long as the scene.
QVector<qint16> mix(Scene const& scene,int first=0,int last=-1);
QVector<qint16> scrub(QVector<qint16> const& stereo,double first,double last,int outputFrames);

bool writeWav(QString const& path, QVector<qint16> const& stereo, QString* error);

}
