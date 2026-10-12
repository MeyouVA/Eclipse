// Eclipse - sound: reading sound files, mixing the sound layers of a scene, writing WAV.
//
// Everything is kept as 16-bit stereo at 44100 Hz.  Sound files other than plain WAV are
// decoded by ffmpeg when it is installed; the scene itself only stores the path.
#pragma once

#include <QString>
#include <QVector>

#include <memory>

class Scene;

struct SoundData {
    static constexpr int kRate = 44100;

    QVector<qint16> samples;  // left, right, left, right, ...

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

// All the audible sound layers of the scene, mixed, as long as the scene.
QVector<qint16> mix(Scene const& scene);

bool writeWav(QString const& path, QVector<qint16> const& stereo, QString* error);

}
