#pragma once
#include <QString>
#include <QVector>

namespace AudioFiles {
struct Options {
    QString format;
    int rate=44100;
    int bits=16;
    int channels=2;
    bool floating=false;
    int kbps=192;
};
bool write(QString const& path,QVector<qint16> const& stereo,Options const& options,QString* error=nullptr);
bool write(QString const& path,QVector<double> const& stereo,Options const& options,QString* error=nullptr);
}
