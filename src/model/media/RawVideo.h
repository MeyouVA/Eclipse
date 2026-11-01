#pragma once
#include <QStringList>
#include <QVector>

namespace RawVideo {
struct Format { QString id,name; int components=0,bytes=1,subX=1,subY=1; bool floating=false; };
struct Options { QString format=QStringLiteral("yuv420p"),frameRate=QStringLiteral("24"),matrix=QStringLiteral("smpte170m"); int width=1920,height=1080; qint64 skipBytes=0; bool fullRange=false; };
QVector<Format> const& formats();
Format const* format(QString const& id);
QStringList suffixes();
bool needsSettings(QString const& path);
double frameRate(QString const& text);
qint64 frameBytes(Options const& options);
QStringList inputArguments(QString const& path,Options const& options,QString* error=nullptr);
}
