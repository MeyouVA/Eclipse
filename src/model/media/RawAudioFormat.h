#pragma once
#include <QStringList>
#include <QVector>

namespace RawAudio {
struct Format {
    QString id,name,demuxer,decoder;
    int sampleBytes=0;
    QStringList aliases;
    bool codes=false;
};
QVector<Format> const& formats();
Format const* format(QString const& idOrSuffix);
QStringList suffixes();
bool isRaw(QString const& path);
}
