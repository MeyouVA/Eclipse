#pragma once
#include "model/media/RawAudioFormat.h"
#include <functional>

namespace RawAudio {
struct Options {
    QString format=QStringLiteral("s16le");
    int rate=44100,channels=2,codeBits=4;
    qint64 skipBytes=0;
};
bool convert(QString const& source,QString const& destination,Options const& options,QString* error=nullptr,std::function<bool()> const& progress={});
}
