// Eclipse - plays a block of sound through the default output of the computer.
#pragma once

#include <QVector>

class SoundPlayer {
public:
    SoundPlayer();
    ~SoundPlayer();
    SoundPlayer(SoundPlayer const&) = delete;
    SoundPlayer& operator=(SoundPlayer const&) = delete;

    bool play(QVector<qint16> const& stereo, qint64 first, qint64 count = -1);
    void stop();

    // Silent: nothing is sent to the sound output by any player (the tests of the program run so).
    static void setSilent(bool silent);
    static bool silent();

private:
    struct Impl;
    Impl* m;
};
