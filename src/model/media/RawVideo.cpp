#include "model/media/RawVideo.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <cmath>

QVector<RawVideo::Format> const& RawVideo::formats()
{
    static QVector<Format> const list={
        {QStringLiteral("rgb24"),QStringLiteral("RGB, 8-bit"),3},{QStringLiteral("bgr24"),QStringLiteral("BGR, 8-bit"),3},
        {QStringLiteral("rgba"),QStringLiteral("RGBA, 8-bit"),4},{QStringLiteral("bgra"),QStringLiteral("BGRA, 8-bit"),4},
        {QStringLiteral("argb"),QStringLiteral("ARGB, 8-bit"),4},{QStringLiteral("abgr"),QStringLiteral("ABGR, 8-bit"),4},
        {QStringLiteral("gray"),QStringLiteral("Greyscale, 8-bit"),1},
        {QStringLiteral("gray16le"),QStringLiteral("Greyscale, 16-bit little endian"),1,2},{QStringLiteral("gray16be"),QStringLiteral("Greyscale, 16-bit big endian"),1,2},
        {QStringLiteral("rgb48le"),QStringLiteral("RGB, 16-bit little endian"),3,2},{QStringLiteral("rgb48be"),QStringLiteral("RGB, 16-bit big endian"),3,2},
        {QStringLiteral("rgba64le"),QStringLiteral("RGBA, 16-bit little endian"),4,2},{QStringLiteral("rgba64be"),QStringLiteral("RGBA, 16-bit big endian"),4,2},
        {QStringLiteral("yuv420p"),QStringLiteral("Planar YUV 4:2:0, 8-bit"),0,1,2,2},
        {QStringLiteral("yuv422p"),QStringLiteral("Planar YUV 4:2:2, 8-bit"),0,1,2,1},
        {QStringLiteral("yuv444p"),QStringLiteral("Planar YUV 4:4:4, 8-bit"),0,1,1,1},
        {QStringLiteral("nv12"),QStringLiteral("NV12, 8-bit"),0,1,2,2},{QStringLiteral("nv21"),QStringLiteral("NV21, 8-bit"),0,1,2,2},
        {QStringLiteral("yuyv422"),QStringLiteral("Packed YUYV 4:2:2"),2},{QStringLiteral("uyvy422"),QStringLiteral("Packed UYVY 4:2:2"),2},
        {QStringLiteral("yuv420p10le"),QStringLiteral("Planar YUV 4:2:0, 10-bit little endian"),0,2,2,2},
        {QStringLiteral("yuv420p10be"),QStringLiteral("Planar YUV 4:2:0, 10-bit big endian"),0,2,2,2},
        {QStringLiteral("yuv422p10le"),QStringLiteral("Planar YUV 4:2:2, 10-bit little endian"),0,2,2,1},
        {QStringLiteral("yuv444p10le"),QStringLiteral("Planar YUV 4:4:4, 10-bit little endian"),0,2,1,1},
        {QStringLiteral("gbrpf32le"),QStringLiteral("Planar RGB, float32 little endian"),3,4,1,1,true},
        {QStringLiteral("gbrapf32le"),QStringLiteral("Planar RGBA, float32 little endian"),4,4,1,1,true},
        {QStringLiteral("grayf32le"),QStringLiteral("Greyscale, float32 little endian"),1,4,1,1,true}
    };
    return list;
}
RawVideo::Format const* RawVideo::format(QString const& id)
{
    for(auto const& item : formats()) if(item.id==id.toLower()) return &item;
    return nullptr;
}
QStringList RawVideo::suffixes()
{
    return {QStringLiteral("yuv"),QStringLiteral("rgb"),QStringLiteral("rgba"),QStringLiteral("bgr"),QStringLiteral("bgra"),QStringLiteral("gray"),QStringLiteral("uyvy"),QStringLiteral("yuyv"),QStringLiteral("nv12"),QStringLiteral("nv21"),QStringLiteral("raw"),QStringLiteral("rawvideo")};
}
bool RawVideo::needsSettings(QString const& path)
{
    if(!suffixes().contains(QFileInfo(path).suffix().toLower())) return false;
    QFile file(path); if(file.open(QIODevice::ReadOnly) && file.read(2)==QByteArray("\x01\xda",2)) return false;
    return true;
}
double RawVideo::frameRate(QString const& text)
{
    static QRegularExpression const valid(QStringLiteral("^[0-9]+(?:\\.[0-9]+|/[1-9][0-9]*)?$"));
    if(!valid.match(text).hasMatch()) return 0;
    QStringList const values=text.split(QLatin1Char('/'));
    double const rate=values.first().toDouble()/(values.size()==2?values.last().toDouble():1.0);
    return std::isfinite(rate) && rate>=.001 && rate<=1000?rate:0;
}
qint64 RawVideo::frameBytes(Options const& options)
{
    auto const* item=format(options.format);
    if(!item || options.width<1 || options.height<1 || options.width>32768 || options.height>32768) return 0;
    if((item->id==QLatin1String("uyvy422") || item->id==QLatin1String("yuyv422")) && options.width%2) return 0;
    qint64 const pixels=qint64(options.width)*options.height;
    if(item->components) return pixels*item->components*item->bytes;
    return (pixels+2*qint64((options.width+item->subX-1)/item->subX)*((options.height+item->subY-1)/item->subY))*item->bytes;
}
QStringList RawVideo::inputArguments(QString const& path,Options const& options,QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return QStringList(); };
    if(options.skipBytes<0) return fail(QStringLiteral("The raw video byte offset cannot be negative."));
    qint64 const bytes=frameBytes(options),payload=QFileInfo(path).size()-options.skipBytes;
    auto const* item=format(options.format);
    if(!item || !bytes || !frameRate(options.frameRate) || options.skipBytes<0 || payload<=0 || payload%bytes)
        return fail(QStringLiteral("Raw video settings must describe complete frames, a valid byte offset and a frame rate from 0.001 to 1000."));
    qint64 const limit=qint64(QImageReader::allocationLimit())*1024*1024;
    if(limit>0 && qint64(options.width)*options.height>limit/(item->floating?16:item->bytes>1?8:4)) return fail(QStringLiteral("Raw video dimensions exceed the image allocation limit."));
    QStringList args{QStringLiteral("-f"),QStringLiteral("rawvideo"),QStringLiteral("-pixel_format"),item->id,QStringLiteral("-video_size"),QStringLiteral("%1x%2").arg(options.width).arg(options.height),
        QStringLiteral("-framerate"),options.frameRate,QStringLiteral("-skip_initial_bytes"),QString::number(options.skipBytes)};
    if(!item->components || item->id==QLatin1String("uyvy422") || item->id==QLatin1String("yuyv422")) {
        if(options.matrix!=QLatin1String("smpte170m") && options.matrix!=QLatin1String("bt709") && options.matrix!=QLatin1String("bt2020nc")) return fail(QStringLiteral("Unsupported raw YUV colour matrix."));
        args << QStringLiteral("-colorspace") << options.matrix << QStringLiteral("-color_range") << (options.fullRange?QStringLiteral("pc"):QStringLiteral("tv"));
    }
    args << QStringLiteral("-i") << QFileInfo(path).absoluteFilePath();
    return args;
}
