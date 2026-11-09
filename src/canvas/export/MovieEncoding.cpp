#include "canvas/export/MovieExport.h"
#include "model/media/MediaFormats.h"
#include <algorithm>

QStringList MovieExport::encoderArguments(QString const& suffix,Options const& options,bool sound)
{
    using namespace MediaFormats;
    Video const* const format=video(suffix);
    if(!format) return {};
    int const id=resolvedCodec(*format,options.codec,options.transparentBackground);
    Codec const* const codec=videoCodec(id);
    if(!codec) return {};
    int const quality=std::clamp(options.quality,0,51);
    static char const* const speeds[]={"veryfast","medium","slow"};
    QString const speed=QLatin1String(speeds[std::clamp(options.speed,0,2)]);
    QStringList args;
    if(id==Gif) return {QStringLiteral("-vf"),QStringLiteral("split[a][b];[a]palettegen=reserve_transparent=1[p];[b][p]paletteuse=alpha_threshold=128"),QStringLiteral("-loop"),QStringLiteral("0"),QStringLiteral("-f"),format->muxer};
    args << QStringLiteral("-c:v") << codec->encoder;
    QString pixels=QStringLiteral("yuv420p");
    QString filter=QStringLiteral("pad=ceil(iw/2)*2:ceil(ih/2)*2");
    switch(id) {
    case H264: args << QStringLiteral("-crf") << QString::number(quality) << QStringLiteral("-preset") << speed; break;
    case Hevc:
        pixels=QStringLiteral("yuv420p10le");
        args << QStringLiteral("-crf") << QString::number(std::clamp(quality+4,0,51)) << QStringLiteral("-preset") << speed;
        if(format->muxer==QLatin1String("mp4") || format->suffix==QLatin1String("mov")) args << QStringLiteral("-tag:v") << QStringLiteral("hvc1");
        break;
    case ProRes422: pixels=QStringLiteral("yuv422p10le"); args << QStringLiteral("-profile:v") << QStringLiteral("3"); break;
    case ProRes4444: pixels=QStringLiteral("yuva444p10le"); filter.clear(); args << QStringLiteral("-profile:v") << QStringLiteral("4") << QStringLiteral("-alpha_bits") << QStringLiteral("16"); break;
    case DnxHR:
        pixels=QStringLiteral("yuv422p10le"); filter=QStringLiteral("pad=max(256\\,ceil(iw/2)*2):max(120\\,ceil(ih/2)*2)");
        args << QStringLiteral("-profile:v") << QStringLiteral("dnxhr_hqx"); break;
    case DnxHD: {
        pixels=QStringLiteral("yuv422p");
        double const fps=options.frameRate>0?options.frameRate:24;
        args << QStringLiteral("-b:v") << (fps<24.5?QStringLiteral("115M"):fps<25.5?QStringLiteral("120M"):fps<30.5?QStringLiteral("145M"):fps<50.5?QStringLiteral("240M"):QStringLiteral("290M"));
        break;
    }
    case Vp8:
    case Vp9:
        pixels=format->alphaCodecs.contains(id)?QStringLiteral("yuva420p"):QStringLiteral("yuv420p");
        args << QStringLiteral("-crf") << QString::number(std::clamp(quality+12,0,63)) << QStringLiteral("-b:v") << QStringLiteral("0")
             << QStringLiteral("-deadline") << (options.speed==0?QStringLiteral("realtime"):QStringLiteral("good")) << QStringLiteral("-cpu-used") << QString::number(options.speed==2?1:options.speed==0?5:2)
             << QStringLiteral("-auto-alt-ref") << QStringLiteral("0");
        break;
    case Av1: args << QStringLiteral("-crf") << QString::number(std::clamp(quality+12,0,63)) << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-cpu-used") << QString::number(options.speed==2?3:options.speed==0?8:6); break;
    case Mjpeg: pixels=QStringLiteral("yuvj444p"); args << QStringLiteral("-q:v") << QString::number(std::clamp(quality/5+1,1,31)); break;
    case Png: pixels=QStringLiteral("rgba"); filter.clear(); break;
    case Animation: pixels=QStringLiteral("argb"); filter.clear(); break;
    case Uncompressed:
        pixels=format->suffix==QLatin1String("y4m")?QStringLiteral("yuv420p"):format->suffix==QLatin1String("mov")?QStringLiteral("argb"):QStringLiteral("bgra");
        if(format->suffix==QLatin1String("y4m")) args[1]=QStringLiteral("wrapped_avframe"); else filter.clear();
        break;
    case Ffv1: pixels=QStringLiteral("gbrap16le"); filter.clear(); args << QStringLiteral("-level") << QStringLiteral("3"); break;
    case HuffYuv: pixels=QStringLiteral("bgra"); filter.clear(); break;
    case Mpeg2: pixels=QStringLiteral("yuv422p"); args << QStringLiteral("-q:v") << QString::number(std::clamp(quality/5-1,1,31)); break;
    case Wmv:
    case Flv: args << QStringLiteral("-q:v") << QString::number(std::clamp(quality/5,1,31)); break;
    case Theora: args << QStringLiteral("-q:v") << QString::number(std::clamp(11-quality/4,0,10)); break;
    default: break;
    }
    if(!filter.isEmpty()) args << QStringLiteral("-vf") << filter;
    args << QStringLiteral("-pix_fmt") << pixels;
    if(sound && !format->audioEncoder.isEmpty()) {
        args << QStringLiteral("-c:a") << format->audioEncoder;
        if(!format->audioEncoder.startsWith(QLatin1String("pcm_")) && format->audioEncoder!=QLatin1String("flac")) args << QStringLiteral("-b:a") << QString::number(std::clamp(options.audioKbps,32,320))+QLatin1Char('k');
        if(format->suffix==QLatin1String("mxf") || format->audioEncoder==QLatin1String("libopus") || format->audioEncoder==QLatin1String("mp2")) args << QStringLiteral("-ar") << QStringLiteral("48000");
    }
    args << QStringLiteral("-f") << format->muxer;
    return args;
}
