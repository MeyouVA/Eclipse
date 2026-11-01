#include "model/media/RawPixels.h"
#include "render/images/ImageFiles.h"
#include "render/images/ImageCodecProcess.h"
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <cmath>
#include <algorithm>

QVector<RawPixels::Format> const& RawPixels::formats()
{
    static QVector<Format> const list={
        {QStringLiteral("rgb"),QStringLiteral("RGB"),3,{}},{QStringLiteral("rgba"),QStringLiteral("RGBA"),4,{}},
        {QStringLiteral("rgbo"),QStringLiteral("RGB with opacity"),4,{}},{QStringLiteral("bgr"),QStringLiteral("BGR"),3,{}},
        {QStringLiteral("bgra"),QStringLiteral("BGRA"),4,{}},{QStringLiteral("bgro"),QStringLiteral("BGR, legacy BGRO"),3,{}},
        {QStringLiteral("gray"),QStringLiteral("Greyscale / single channel"),1,{QStringLiteral("r"),QStringLiteral("g"),QStringLiteral("b"),QStringLiteral("a"),QStringLiteral("c"),QStringLiteral("m"),QStringLiteral("y"),QStringLiteral("k"),QStringLiteral("o")}},
        {QStringLiteral("graya"),QStringLiteral("Greyscale with alpha"),2,{}},
        {QStringLiteral("cmyk"),QStringLiteral("CMYK"),4,{}},{QStringLiteral("cmyka"),QStringLiteral("CMYK with alpha"),5,{}},
        {QStringLiteral("ycbcr"),QStringLiteral("YCbCr"),3,{}},{QStringLiteral("ycbcra"),QStringLiteral("YCbCr with alpha"),4,{}},
        {QStringLiteral("uyvy"),QStringLiteral("UYVY 4:2:2"),2,{QStringLiteral("pal")}},
        {QStringLiteral("yuv"),QStringLiteral("Planar YUV 4:2:0"),0,{}},
        {QStringLiteral("bayer"),QStringLiteral("Bayer samples"),1,{QStringLiteral("bayera")}},
        {QStringLiteral("mono"),QStringLiteral("Packed monochrome"),0,{}},
        {QStringLiteral("group4"),QStringLiteral("CCITT Group 4"),0,{}},
        {QStringLiteral("map"),QStringLiteral("Indexed MAP, 8-bit RGB palette"),0,{}}
    };
    return list;
}
RawPixels::Format const* RawPixels::format(QString const& suffix)
{
    QString const key=suffix.toLower();
    for(auto const& item : formats()) if(item.id==key || item.aliases.contains(key)) return &item;
    return nullptr;
}
QStringList RawPixels::suffixes()
{
    QStringList list;
    for(auto const& item : formats()) { list << item.id; list+=item.aliases; }
    return list;
}
bool RawPixels::needsSettings(QString const& path)
{
    QString const suffix=QFileInfo(path).suffix().toLower();
    if(!format(suffix)) return false;
    if(suffix==QLatin1String("rgb") || suffix==QLatin1String("rgba")) {
        QFile file(path); if(file.open(QIODevice::ReadOnly) && file.read(2)==QByteArray("\x01\xda",2)) return false;
    }
    return true;
}
QImage RawPixels::read(QString const& path,Options const& options,QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return QImage(); };
    auto const* layout=format(options.format);
    if(!layout || options.width<=0 || options.height<=0 || options.width>32768 || options.height>32768 || options.skipBytes<0)
        return fail(QStringLiteral("Invalid raw image layout, dimensions or byte offset."));
    bool const mono=layout->id==QLatin1String("mono") || layout->id==QLatin1String("group4");
    bool const indexed=layout->id==QLatin1String("map"),yuv=layout->id==QLatin1String("yuv"),packed=layout->id==QLatin1String("uyvy");
    if((options.bits!=8 && options.bits!=16 && !(options.bits==32 && options.floating) && !(options.bits==1 && mono)) || (options.floating && options.bits!=32))
        return fail(QStringLiteral("Choose 8/16-bit unsigned samples, 32-bit float samples, or 1-bit monochrome."));
    if((indexed || yuv || packed) && (options.bits!=8 || options.floating)) return fail(QStringLiteral("This raw layout requires 8-bit samples."));
    if((yuv && (options.width%2 || options.height%2)) || (packed && options.width%2)) return fail(QStringLiteral("Subsampled YUV requires even image dimensions."));
    if(indexed && (options.palette<1 || options.palette>256)) return fail(QStringLiteral("MAP requires 1 to 256 palette entries."));
    qint64 const count=qint64(options.width)*options.height,limit=qint64(QImageReader::allocationLimit())*1024*1024;
    if(limit>0 && count>limit/(options.floating?16:options.bits>8?8:4)) return fail(QStringLiteral("Raw image dimensions exceed the image allocation limit."));
    qint64 required=layout->channels?count*layout->channels*(options.bits/8):0;
    if(layout->id==QLatin1String("mono")) required=qint64((options.width+7)/8)*options.height;
    if(yuv) required=count*3/2;
    if(indexed) required=count+options.palette*3;
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    if(options.skipBytes>=input.size() || !input.seek(options.skipBytes) || input.size()-options.skipBytes<required) return fail(QStringLiteral("Not enough raw data for the selected dimensions, layout and byte offset."));
    QTemporaryDir folder;
    if(!folder.isValid()) return fail(QStringLiteral("Cannot create a raw image conversion folder."));
    if(indexed) {
        QByteArray const palette=input.read(options.palette*3);
        if(palette.size()!=options.palette*3) return fail(QStringLiteral("Incomplete MAP palette."));
        QImage image(options.width,options.height,QImage::Format_RGBA8888);
        if(image.isNull()) return fail(QStringLiteral("Cannot allocate the raw image."));
        for(int y=0;y<options.height;++y) {
            QByteArray const row=input.read(options.width);
            if(row.size()!=options.width) return fail(QStringLiteral("Incomplete MAP pixel row."));
            for(int x=0;x<options.width;++x) {
                int const index=quint8(row[x]); if(index>=options.palette) return fail(QStringLiteral("MAP pixel refers to a missing palette entry."));
                image.setPixelColor(x,y,QColor(quint8(palette[index*3]),quint8(palette[index*3+1]),quint8(palette[index*3+2])));
            }
        }
        QColorSpace space(options.linear?QColorSpace::SRgbLinear:QColorSpace::SRgb);
        if(!options.profile.isEmpty()) {
            QFile profile(options.profile); if(!profile.open(QIODevice::ReadOnly) || profile.size()>16*1024*1024) return fail(QStringLiteral("Cannot read the source ICC profile."));
            space=QColorSpace::fromIccProfile(profile.readAll()); if(!space.isValid()) return fail(QStringLiteral("MAP requires a valid RGB ICC profile."));
        }
        image.setColorSpace(space); return image;
    }
    QString const raw=folder.filePath(QStringLiteral("pixels.raw")),output=folder.filePath(QStringLiteral("decoded.miff"));
    QFile bytes(raw); if(!bytes.open(QIODevice::WriteOnly)) return fail(bytes.errorString());
    qint64 remaining=required?required:input.size()-options.skipBytes;
    while(remaining>0) {
        QByteArray const chunk=input.read(std::min<qint64>(remaining,1024*1024));
        if(chunk.isEmpty() || bytes.write(chunk)!=chunk.size()) return fail(QStringLiteral("Cannot read the raw image samples."));
        remaining-=chunk.size();
    }
    bytes.close();
    QStringList args{QStringLiteral("-size"),QStringLiteral("%1x%2").arg(options.width).arg(options.height),QStringLiteral("-depth"),QString::number(options.bits),QStringLiteral("-endian"),options.bigEndian?QStringLiteral("MSB"):QStringLiteral("LSB"),
        QStringLiteral("-define"),options.floating?QStringLiteral("quantum:format=floating-point"):QStringLiteral("quantum:format=unsigned"),QStringLiteral("-interlace"),options.planar?QStringLiteral("plane"):QStringLiteral("none"),layout->id.toUpper()+QLatin1Char(':')+raw+QStringLiteral("[0]")};
    if(!options.profile.isEmpty()) args << QStringLiteral("-profile") << QFileInfo(options.profile).absoluteFilePath();
    if(layout->id.startsWith(QLatin1String("cmyk"))) {
        if(!options.profile.isEmpty()) {
            QString const target=folder.filePath(QStringLiteral("srgb.icc")); QFile profile(target);
            QByteArray const data=QColorSpace(QColorSpace::SRgb).iccProfile();
            if(!profile.open(QIODevice::WriteOnly) || profile.write(data)!=data.size()) return fail(QStringLiteral("Cannot prepare the colour profile."));
            profile.close(); args << QStringLiteral("-profile") << target;
        } else args << QStringLiteral("-colorspace") << QStringLiteral("sRGB");
    }
    args << QStringLiteral("-depth") << QString::number(options.floating?32:options.bits>8?16:8) << QStringLiteral("MIFF:")+output;
    if(!ImageCodec::run(args,error)) return {};
    QImage image=ImageFiles::load(output,error);
    if(image.size()!=QSize(options.width,options.height)) return fail(QStringLiteral("The raw decoder returned incorrect dimensions."));
    if(options.profile.isEmpty()) image.setColorSpace(QColorSpace(options.linear && !layout->id.startsWith(QLatin1String("cmyk"))?QColorSpace::SRgbLinear:QColorSpace::SRgb));
    if(options.floating) {
        image=image.convertToFormat(QImage::Format_RGBA32FPx4);
        for(int y=0;y<image.height();++y) {
            auto const* row=reinterpret_cast<float const*>(image.constScanLine(y));
            for(int x=0;x<image.width()*4;++x) if(!std::isfinite(row[x])) return fail(QStringLiteral("Raw image contains non-finite samples; check its encoding settings."));
        }
    }
    return image;
}
