#include "animation/ui/views/PlaybackMovie.h"
#include "platform/ThirdParty.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QObject>
#include <QProcess>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
bool run(QString const& program,QStringList const& args,std::function<bool()> const& keepGoing,QString* error,QByteArray* output=nullptr,QString const& scratch={})
{
    QProcess process; process.start(program,args); QElapsedTimer elapsed; elapsed.start();
    qint64 const freeAtStart=scratch.isEmpty()?-1:QStorageInfo(scratch).bytesAvailable();
    if(!process.waitForStarted(10000)) { if(error) *error=process.errorString(); return false; }
    QByteArray said,bytes;
    while(process.state()!=QProcess::NotRunning) {
        process.waitForFinished(50); said=(said+process.readAllStandardError()).right(8192);
        auto const chunk=process.readAllStandardOutput(); if(output) bytes+=chunk;
        bool const cancelled=keepGoing && !keepGoing();
        qint64 const available=scratch.isEmpty()?-1:QStorageInfo(scratch).bytesAvailable();
        bool const timeout=elapsed.elapsed()>300000,full=available>=0 && (available<512*1024*1024 || (freeAtStart>=0 && freeAtStart-available>qint64(10)*1024*1024*1024));
        if(cancelled || timeout || full || bytes.size()>8*1024*1024) {
            process.kill(); process.waitForFinished(1000);
            if(error) *error=cancelled?QString():full?QObject::tr("Not enough space for temporary playback files."):QObject::tr("The movie decoder exceeded its time or metadata limit.");
            return false;
        }
    }
    said=(said+process.readAllStandardError()).right(8192); if(output) *output=bytes+process.readAllStandardOutput();
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) { if(error) *error=QString::fromUtf8(said).trimmed(); if(error && error->isEmpty()) *error=QObject::tr("The movie could not be decoded."); return false; }
    return true;
}
double number(QJsonValue const& value) { bool okay=false; double const result=value.toVariant().toString().toDouble(&okay); return okay && std::isfinite(result)?result:0; }
double parseRate(QString const& text)
{
    auto const parts=text.split(QLatin1Char('/')); if(parts.size()>2) return 0;
    double const divisor=parts.size()==2?parts[1].toDouble():1;
    double const value=divisor>0?parts[0].toDouble()/divisor:0;
    return std::isfinite(value) && value>=.001 && value<=1000?value:0;
}
}
std::unique_ptr<PlaybackMovie> PlaybackMovie::decode(QString const& path,std::function<bool()> const& keepGoing,QString* error)
{
    auto fail=[&](QString const& why)->std::unique_ptr<PlaybackMovie> { if(error) *error=why; return {}; };
    QString const ffmpeg=ThirdParty::executable(QStringLiteral("ffmpeg"),QStringLiteral("ffmpeg")),probe=ThirdParty::executable(QStringLiteral("ffmpeg"),QStringLiteral("ffprobe"));
    if(ffmpeg.isEmpty() || probe.isEmpty()) return fail(QObject::tr("The bundled movie tools are missing. Run scripts/install_thirdparty.bat."));
    if(!QFileInfo(path).isFile()) return fail(QObject::tr("The movie file does not exist."));
    QString const input=QFileInfo(path).absoluteFilePath();
    QByteArray metadata;
    if(!run(probe,{QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-show_streams"),QStringLiteral("-show_format"),QStringLiteral("-of"),QStringLiteral("json"),input},keepGoing,error,&metadata)) return {};
    auto const json=QJsonDocument::fromJson(metadata).object(); QJsonObject video,audio;
    for(auto const& value : json[QStringLiteral("streams")].toArray()) {
        auto const stream=value.toObject(); QString const type=stream[QStringLiteral("codec_type")].toString();
        if(type==QLatin1String("video") && video.isEmpty() && !stream[QStringLiteral("disposition")].toObject()[QStringLiteral("attached_pic")].toInt()) video=stream;
        if(type==QLatin1String("audio") && audio.isEmpty()) audio=stream;
    }
    if(video.isEmpty()) return fail(QObject::tr("This file has no playable video stream."));
    auto result=std::make_unique<PlaybackMovie>(); result->source=QFileInfo(path).absoluteFilePath(); result->rate=parseRate(video[QStringLiteral("avg_frame_rate")].toString());
    if(!result->rate) result->rate=parseRate(video[QStringLiteral("r_frame_rate")].toString());
    double const duration=number(video[QStringLiteral("duration")])>0?number(video[QStringLiteral("duration")]):number(json[QStringLiteral("format")].toObject()[QStringLiteral("duration")]);
    if(!result->rate || duration*result->rate>100000) return fail(QObject::tr("Playback requires a valid movie rate and at most 100000 decoded frames."));
    qint64 const width=video[QStringLiteral("width")].toInt(),height=video[QStringLiteral("height")].toInt(),limit=qint64(QImageReader::allocationLimit())*1024*1024;
    if(width<1 || height<1 || width>32768 || height>32768 || (limit>0 && width*height>limit/8)) return fail(QObject::tr("Movie frames exceed the image allocation limit."));
    result->folder=std::make_unique<QTemporaryDir>(QDir::tempPath()+QStringLiteral("/EclipsePlayback_XXXXXX"));
    if(!result->folder->isValid()) return fail(QObject::tr("Cannot create temporary playback storage."));
    QDir folder(result->folder->path());
    if(!run(ffmpeg,{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-i"),input,QStringLiteral("-map"),QStringLiteral("0:")+QString::number(video[QStringLiteral("index")].toInt()),
        QStringLiteral("-vf"),QStringLiteral("setpts=PTS-STARTPTS"),QStringLiteral("-r"),QString::number(result->rate,'g',15),QStringLiteral("-frames:v"),QStringLiteral("100001"),QStringLiteral("-pix_fmt"),QStringLiteral("rgba64be"),folder.filePath(QStringLiteral("frame_%06d.png"))},keepGoing,error,nullptr,folder.path())) return {};
    auto const frames=folder.entryList({QStringLiteral("frame_*.png")},QDir::Files);
    if(frames.isEmpty() || frames.size()>100000) return fail(QObject::tr("The movie has no frames or exceeds the 100000-frame playback limit."));
    double const seconds=frames.size()/result->rate;
    if(!audio.isEmpty()) {
        double const offset=number(audio[QStringLiteral("start_time")])-number(video[QStringLiteral("start_time")]);
        if(std::abs(offset)>86400) return fail(QObject::tr("The movie has unsupported audio/video start times."));
        if(offset>=seconds) return result;
        if(seconds*44100*4>256*1024*1024) return fail(QObject::tr("The decoded movie sound exceeds the 256 MB playback-audio limit."));
        qint64 const samples=qint64(std::llround(offset*44100)); QString filter=QStringLiteral("aresample=44100,asetpts=PTS-STARTPTS");
        if(samples>0) filter+=QStringLiteral(",adelay=%1S:all=1").arg(samples);
        else if(samples<0) filter+=QStringLiteral(",atrim=start_sample=%1,asetpts=PTS-STARTPTS").arg(-samples);
        QString const pcm=folder.filePath(QStringLiteral("sound.pcm"));
        if(!run(ffmpeg,{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-i"),input,QStringLiteral("-map"),QStringLiteral("0:")+QString::number(audio[QStringLiteral("index")].toInt()),QStringLiteral("-vn"),
            QStringLiteral("-af"),filter,QStringLiteral("-t"),QString::number(seconds,'g',15),QStringLiteral("-ac"),QStringLiteral("2"),QStringLiteral("-ar"),QStringLiteral("44100"),QStringLiteral("-c:a"),QStringLiteral("pcm_s16le"),QStringLiteral("-f"),QStringLiteral("s16le"),pcm},keepGoing,error,nullptr,folder.path())) return {};
        QFile file(pcm); if(!file.open(QIODevice::ReadOnly) || file.size()>256*1024*1024) return fail(QObject::tr("Cannot read the movie's playback sound."));
        QByteArray const bytes=file.readAll(); result->audio.resize(bytes.size()/4*2);
        std::memcpy(result->audio.data(),bytes.constData(),size_t(result->audio.size())*sizeof(qint16));
    }
    return result;
}
