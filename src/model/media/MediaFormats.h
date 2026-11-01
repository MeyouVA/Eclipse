#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace MediaFormats {
enum VideoCodec { H264=0, Hevc, ProRes422, ProRes4444, DnxHR, DnxHD, Vp8, Vp9, Av1, Mjpeg, Png, Animation, Uncompressed, Mpeg2, Wmv, Flv, Theora, Gif, Ffv1, HuffYuv };
struct Codec {
    int id;
    QString name,encoder;
    bool alpha=false;
    bool deep=false;
};
struct Video {
    QString suffix,name,muxer;
    QVector<int> codecs;
    int defaultCodec=H264;
    QString audioEncoder;
    QStringList aliases;
    QVector<int> alphaCodecs;
    bool writable=true;
};
struct Audio {
    QString id,name,suffix,encoder,muxer;
    QStringList aliases;
    bool pcm=false;
    int maxBits=16;
    bool floating=false;
    bool lossless=false;
    bool writable=true;
};
QVector<Codec> const& videoCodecs();
QVector<Video> const& videoFormats();
QVector<Audio> const& audioFormats();
Codec const* videoCodec(int id);
Video const* video(QString const& suffix);
Audio const* audio(QString const& idOrSuffix);
QString videoFilter(bool separate=false);
QString audioFilter(bool includeVideo=true);
int resolvedCodec(Video const& format,int requested,bool transparent);
}
