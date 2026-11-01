#include "audio/sound/AudioFiles.h"
#include "audio/sound/Sound.h"
#include "model/media/MediaFormats.h"
#include "model/scene/AtomicFile.h"
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>

static bool writeAudio(QString const& path,QVector<double> const& stereo,AudioFiles::Options const& options,int sourceRate,QString* error)
{
    auto fail=[&](QString const& message) { if(error) *error=message; return false; };
    auto const* format=MediaFormats::audio(options.format.isEmpty()?QFileInfo(path).suffix():options.format);
    if(!format) return fail(QStringLiteral("Unsupported audio format."));
    if(stereo.isEmpty() || stereo.size()%2) return fail(QStringLiteral("There are no complete stereo samples to export."));
    if(options.rate<8000 || options.rate>192000 || (options.channels!=1 && options.channels!=2)) return fail(QStringLiteral("Invalid audio sample rate or channel count."));
    if(format->pcm && (options.bits!=8 && options.bits!=16 && options.bits!=24 && options.bits!=32)) return fail(QStringLiteral("PCM audio requires 8, 16, 24 or 32 bits."));
    if(options.floating && (!format->floating || options.bits!=32)) return fail(QStringLiteral("Floating-point output requires 32-bit WAV or AIFF."));
    QString const ffmpeg=Sound::findFfmpeg();
    if(ffmpeg.isEmpty()) return fail(QStringLiteral("The bundled media tools are missing. Run scripts/install_thirdparty.bat."));
    QTemporaryDir folder;
    if(!folder.isValid()) return fail(QStringLiteral("Cannot create an audio export folder."));
    QString const input=folder.filePath(QStringLiteral("source.wav")),output=folder.filePath(QStringLiteral("output.")+format->suffix);
    if(!Sound::writePreciseWav(input,stereo,sourceRate,error)) return false;
    QString encoder=format->encoder;
    if(format->pcm) {
        QString const endian=format->id==QLatin1String("wav")?QStringLiteral("le"):QStringLiteral("be");
        encoder=options.floating?QStringLiteral("pcm_f32")+endian:options.bits==8?(format->id==QLatin1String("wav")?QStringLiteral("pcm_u8"):QStringLiteral("pcm_s8")):QStringLiteral("pcm_s%1").arg(options.bits)+endian;
    }
    QStringList args{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-i"),input,QStringLiteral("-map"),QStringLiteral("0:a:0"),QStringLiteral("-vn"),QStringLiteral("-c:a"),encoder,
        QStringLiteral("-ar"),QString::number(options.rate),QStringLiteral("-ac"),QString::number(options.channels)};
    if(options.channels==1) args << QStringLiteral("-af") << QStringLiteral("pan=mono|c0=0.5*c0+0.5*c1");
    if(!format->lossless) args << QStringLiteral("-b:a") << QString::number(std::clamp(options.kbps,32,320))+QLatin1Char('k');
    if(format->id==QLatin1String("alac")) args << QStringLiteral("-sample_fmt") << (options.bits>16?QStringLiteral("s32p"):QStringLiteral("s16p"));
    if(format->id==QLatin1String("flac")) args << QStringLiteral("-sample_fmt") << (options.bits>16?QStringLiteral("s32"):QStringLiteral("s16"));
    if(format->id==QLatin1String("flac") && options.bits>16) args << QStringLiteral("-bits_per_raw_sample") << QStringLiteral("24");
    args << QStringLiteral("-f") << format->muxer << output;
    QProcess process; process.start(ffmpeg,args);
    if(!process.waitForStarted(10000) || !process.waitForFinished(300000) || process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
        process.kill(); process.waitForFinished(1000);
        QString message=QString::fromUtf8(process.readAllStandardError()).trimmed();
        return fail(message.isEmpty()?process.errorString():message);
    }
    return AtomicFile::replaceFrom(output,path,error);
}

bool AudioFiles::write(QString const& path,QVector<double> const& stereo,Options const& options,QString* error)
{
    return writeAudio(path,stereo,options,options.rate,error);
}

bool AudioFiles::write(QString const& path,QVector<qint16> const& stereo,Options const& options,QString* error)
{
    QVector<double> precise(stereo.size());
    for(qsizetype i=0;i<stereo.size();++i) precise[i]=stereo[i]/32768.0;
    return writeAudio(path,precise,options,SoundData::kRate,error);
}
