#include "model/media/MediaFormats.h"

namespace MediaFormats {
QVector<Codec> const& videoCodecs()
{
    static QVector<Codec> const list={
        {H264,QStringLiteral("H.264"),QStringLiteral("libx264")},
        {Hevc,QStringLiteral("H.265 / HEVC"),QStringLiteral("libx265"),false,true},
        {ProRes422,QStringLiteral("ProRes 422 HQ"),QStringLiteral("prores_ks"),false,true},
        {ProRes4444,QStringLiteral("ProRes 4444 (alpha)"),QStringLiteral("prores_ks"),true,true},
        {DnxHR,QStringLiteral("DNxHR HQX"),QStringLiteral("dnxhd"),false,true},
        {DnxHD,QStringLiteral("DNxHD"),QStringLiteral("dnxhd"),false,true},
        {Vp8,QStringLiteral("VP8"),QStringLiteral("libvpx"),true},
        {Vp9,QStringLiteral("VP9"),QStringLiteral("libvpx-vp9"),true},
        {Av1,QStringLiteral("AV1"),QStringLiteral("libaom-av1")},
        {Mjpeg,QStringLiteral("Motion JPEG"),QStringLiteral("mjpeg")},
        {Png,QStringLiteral("PNG (alpha)"),QStringLiteral("png"),true},
        {Animation,QStringLiteral("Animation (alpha)"),QStringLiteral("qtrle"),true},
        {Uncompressed,QStringLiteral("Uncompressed (alpha)"),QStringLiteral("rawvideo"),true},
        {Mpeg2,QStringLiteral("MPEG-2"),QStringLiteral("mpeg2video")},
        {Wmv,QStringLiteral("Windows Media Video"),QStringLiteral("wmv2")},
        {Flv,QStringLiteral("Flash Video"),QStringLiteral("flv")},
        {Theora,QStringLiteral("Theora"),QStringLiteral("libtheora")},
        {Gif,QStringLiteral("GIF (binary alpha)"),QStringLiteral("gif"),true}
    };
    return list;
}

QVector<Video> const& videoFormats()
{
    static QVector<Video> const list={
        {QStringLiteral("mp4"),QStringLiteral("MP4"),QStringLiteral("mp4"),{H264,Hevc,Vp9,Av1},H264,QStringLiteral("aac")},
        {QStringLiteral("mov"),QStringLiteral("QuickTime MOV"),QStringLiteral("mov"),{H264,Hevc,ProRes422,ProRes4444,DnxHR,DnxHD,Mjpeg,Png,Animation,Uncompressed},ProRes4444,QStringLiteral("pcm_s16le"),{},{ProRes4444,Png,Animation,Uncompressed}},
        {QStringLiteral("avi"),QStringLiteral("AVI"),QStringLiteral("avi"),{H264,Mjpeg,Png,Uncompressed},Mjpeg,QStringLiteral("pcm_s16le"),{},{Png,Uncompressed}},
        {QStringLiteral("mkv"),QStringLiteral("Matroska"),QStringLiteral("matroska"),{H264,Hevc,ProRes422,ProRes4444,DnxHR,DnxHD,Vp8,Vp9,Av1,Mjpeg,Png},H264,QStringLiteral("flac"),{},{ProRes4444,Vp8,Vp9,Png}},
        {QStringLiteral("webm"),QStringLiteral("WebM"),QStringLiteral("webm"),{Vp8,Vp9,Av1},Vp9,QStringLiteral("libopus"),{},{Vp8,Vp9}},
        {QStringLiteral("m4v"),QStringLiteral("M4V"),QStringLiteral("mp4"),{H264,Hevc,Av1},H264,QStringLiteral("aac")},
        {QStringLiteral("mpg"),QStringLiteral("MPEG"),QStringLiteral("mpeg"),{Mpeg2},Mpeg2,QStringLiteral("mp2"),{QStringLiteral("mpeg")}},
        {QStringLiteral("wmv"),QStringLiteral("Windows Media"),QStringLiteral("asf"),{Wmv},Wmv,QStringLiteral("wmav2")},
        {QStringLiteral("flv"),QStringLiteral("FLV"),QStringLiteral("flv"),{H264,Flv},H264,QStringLiteral("aac")},
        {QStringLiteral("ogv"),QStringLiteral("Ogg Video"),QStringLiteral("ogg"),{Theora},Theora,QStringLiteral("libvorbis")},
        {QStringLiteral("mxf"),QStringLiteral("MXF"),QStringLiteral("mxf"),{Mpeg2,DnxHR,DnxHD},Mpeg2,QStringLiteral("pcm_s16le")},
        {QStringLiteral("gif"),QStringLiteral("Animated GIF"),QStringLiteral("gif"),{Gif},Gif,{},{},{Gif}}
    };
    return list;
}

QVector<Audio> const& audioFormats()
{
    static QVector<Audio> const list={
        {QStringLiteral("wav"),QStringLiteral("WAV"),QStringLiteral("wav"),QStringLiteral("pcm_s16le"),QStringLiteral("wav"),{QStringLiteral("wave")},true,32,true,true},
        {QStringLiteral("aiff"),QStringLiteral("AIFF"),QStringLiteral("aiff"),QStringLiteral("pcm_s16be"),QStringLiteral("aiff"),{QStringLiteral("aif"),QStringLiteral("aifc")},true,32,true,true},
        {QStringLiteral("mp3"),QStringLiteral("MP3"),QStringLiteral("mp3"),QStringLiteral("libmp3lame"),QStringLiteral("mp3")},
        {QStringLiteral("aac"),QStringLiteral("AAC"),QStringLiteral("aac"),QStringLiteral("aac"),QStringLiteral("adts")},
        {QStringLiteral("m4a"),QStringLiteral("AAC in M4A"),QStringLiteral("m4a"),QStringLiteral("aac"),QStringLiteral("ipod")},
        {QStringLiteral("ogg"),QStringLiteral("Ogg Vorbis"),QStringLiteral("ogg"),QStringLiteral("libvorbis"),QStringLiteral("ogg"),{QStringLiteral("oga")}},
        {QStringLiteral("opus"),QStringLiteral("Opus"),QStringLiteral("opus"),QStringLiteral("libopus"),QStringLiteral("opus")},
        {QStringLiteral("flac"),QStringLiteral("FLAC"),QStringLiteral("flac"),QStringLiteral("flac"),QStringLiteral("flac"),{},false,24,false,true},
        {QStringLiteral("alac"),QStringLiteral("Apple Lossless (M4A)"),QStringLiteral("m4a"),QStringLiteral("alac"),QStringLiteral("ipod"),{QStringLiteral("alac")},false,24,false,true},
        {QStringLiteral("wma"),QStringLiteral("Windows Media Audio"),QStringLiteral("wma"),QStringLiteral("wmav2"),QStringLiteral("asf")},
        {QStringLiteral("ac3"),QStringLiteral("AC-3"),QStringLiteral("ac3"),QStringLiteral("ac3"),QStringLiteral("ac3")}
    };
    return list;
}

Codec const* videoCodec(int id) { for(auto const& codec : videoCodecs()) if(codec.id==id) return &codec; return nullptr; }
Video const* video(QString const& suffix) { for(auto const& format : videoFormats()) if(format.suffix.compare(suffix,Qt::CaseInsensitive)==0 || format.aliases.contains(suffix.toLower())) return &format; return nullptr; }
Audio const* audio(QString const& idOrSuffix)
{
    QString const key=idOrSuffix.toLower();
    for(auto const& format : audioFormats()) if(format.id==key) return &format;
    for(auto const& format : audioFormats()) if(format.suffix==key || format.aliases.contains(key)) return &format;
    return nullptr;
}

QString videoFilter(bool separate)
{
    QStringList filters,all;
    for(auto const& format : videoFormats()) {
        QStringList patterns{QStringLiteral("*.")+format.suffix};
        for(QString const& alias : format.aliases) patterns << QStringLiteral("*.")+alias;
        all+=patterns;
        filters << format.name+QStringLiteral(" (%1)").arg(patterns.join(QLatin1Char(' ')));
    }
    return separate?filters.join(QStringLiteral(";;")):QStringLiteral("Movies (%1);;All Files (*)").arg(all.join(QLatin1Char(' ')));
}

QString audioFilter(bool includeVideo)
{
    QStringList patterns;
    for(auto const& format : audioFormats()) {
        patterns << QStringLiteral("*.")+format.suffix;
        for(QString const& alias : format.aliases) patterns << QStringLiteral("*.")+alias;
    }
    if(includeVideo) for(auto const& format : videoFormats()) if(!format.audioEncoder.isEmpty()) {
        patterns << QStringLiteral("*.")+format.suffix;
        for(QString const& alias : format.aliases) patterns << QStringLiteral("*.")+alias;
    }
    patterns.removeDuplicates();
    return QStringLiteral("Sound Files (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
}

int resolvedCodec(Video const& format,int requested,bool transparent)
{
    int chosen=requested<0?format.defaultCodec:requested;
    if(!format.codecs.contains(chosen)) return -1;
    if(transparent && !format.alphaCodecs.contains(chosen)) {
        for(int id : format.codecs) if(format.alphaCodecs.contains(id)) return id;
        return -1;
    }
    return chosen;
}
}
