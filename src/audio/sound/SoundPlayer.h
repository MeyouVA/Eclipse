// Eclipse - plays a block of sound through the default output of the computer.
#pragma once

#include <QVector>

class SoundPlayer {
public:
    SoundPlayer();
    ~SoundPlayer();
    SoundPlayer(SoundPlayer const&) = delete;
    SoundPlayer& operator=(SoundPlayer const&) = delete;

    // Plays 16-bit stereo 44100 Hz sound from sample frame `first`, `count` frames of it
    // (all the rest when negative), stopping whatever was playing.  False when there is
    // no sound output.
    bool play(QVector<qint16> const& stereo, qint64 first, qint64 count = -1);
    void stop();

private:
    struct Impl;
    Impl* m;
};
