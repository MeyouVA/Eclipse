#pragma once
#include <QImage>
#include <QStringList>
#include <QVector>

namespace RawPixels {
struct Format { QString id,name; int channels=0; QStringList aliases; };
struct Options {
    QString format=QStringLiteral("rgba"),profile;
    int width=1920,height=1080,bits=8,palette=256;
    bool floating=false,bigEndian=false,planar=false,linear=false;
    qint64 skipBytes=0;
};
QVector<Format> const& formats();
Format const* format(QString const& suffix);
QStringList suffixes();
bool needsSettings(QString const& path);
QImage read(QString const& path,Options const& options,QString* error=nullptr);
}
