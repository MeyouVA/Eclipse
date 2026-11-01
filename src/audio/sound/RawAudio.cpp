#include "audio/sound/RawAudio.h"
#include "audio/sound/Sound.h"
#include "model/scene/AtomicFile.h"
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>

bool RawAudio::convert(QString const& source,QString const& destination,Options const& options,QString* error,std::function<bool()> const& progress)
{
    auto fail=[&](QString const& message) { if(error) *error=message; return false; };
    Format const* const input=format(options.format);
    if(!input || options.rate<1000 || options.rate>384000 || options.channels<1 || options.channels>32 || options.skipBytes<0)
        return fail(QStringLiteral("Invalid raw audio encoding, rate, channels or byte offset."));
    if(input->codes && (options.channels!=1 || options.codeBits<2 || options.codeBits>5))
        return fail(QStringLiteral("G.726 requires mono audio and 2 to 5 bits per code."));
    QFile file(source);
    if(!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    qint64 const size=file.size()-options.skipBytes;
    if(size<=0 || !file.seek(options.skipBytes)) return fail(QStringLiteral("The selected byte offset leaves no audio data."));
    if(input->sampleBytes && size%(input->sampleBytes*options.channels)) return fail(QStringLiteral("The raw data does not contain complete sample frames at the selected depth and channel count."));
    QFileInfo const origin(source),target(destination);
    if(origin.absoluteFilePath()==target.absoluteFilePath() || (!target.canonicalFilePath().isEmpty() && origin.canonicalFilePath()==target.canonicalFilePath()))
        return fail(QStringLiteral("The converted sound must not replace its raw source."));
    QString const ffmpeg=Sound::findFfmpeg();
    if(ffmpeg.isEmpty()) return fail(QStringLiteral("The bundled media tools are missing. Run scripts/install_thirdparty.bat."));
    QTemporaryDir scratch;
    if(!scratch.isValid()) return fail(QStringLiteral("Cannot create a raw audio conversion folder."));
    QString const path=scratch.filePath(QStringLiteral("input.raw")),output=scratch.filePath(QStringLiteral("output.wav"));
    QFile raw(path);
    if(!raw.open(QIODevice::WriteOnly)) return fail(raw.errorString());
    while(!file.atEnd()) {
        if(progress && !progress()) return fail(QStringLiteral("Stopped."));
        QByteArray const bytes=file.read(1024*1024);
        if(bytes.isEmpty() || raw.write(bytes)!=bytes.size()) return fail(QStringLiteral("Cannot prepare the raw audio data."));
    }
    raw.close(); file.close();
    QStringList args{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-f"),input->demuxer};
    if(input->codes) args << QStringLiteral("-sample_rate") << QString::number(options.rate) << QStringLiteral("-code_size") << QString::number(options.codeBits);
    else args << QStringLiteral("-ar") << QString::number(options.rate) << QStringLiteral("-ac") << QString::number(options.channels);
    if(!input->decoder.isEmpty()) args << QStringLiteral("-c:a") << input->decoder;
    args << QStringLiteral("-i") << path << QStringLiteral("-map") << QStringLiteral("0:a:0") << QStringLiteral("-c:a") << QStringLiteral("pcm_f64le") << QStringLiteral("-ar") << QString::number(options.rate)
         << QStringLiteral("-ac") << QString::number(options.channels) << QStringLiteral("-f") << QStringLiteral("wav") << output;
    QProcess process; process.start(ffmpeg,args);
    if(!process.waitForStarted(10000)) return fail(QStringLiteral("Cannot start the raw audio decoder."));
    QElapsedTimer elapsed; elapsed.start();
    while(process.state()!=QProcess::NotRunning) {
        if(process.waitForFinished(50)) break;
        if((progress && !progress()) || elapsed.elapsed()>=300000) {
            process.kill(); process.waitForFinished(1000);
            return fail(elapsed.elapsed()>=300000?QStringLiteral("The raw audio decoder timed out."):QStringLiteral("Stopped."));
        }
    }
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) return fail(QString::fromUtf8(process.readAllStandardError()).trimmed());
    if(progress && !progress()) return fail(QStringLiteral("Stopped."));
    if(QFileInfo(output).size()<=80) return fail(QStringLiteral("The raw audio decoder returned no samples."));
    return AtomicFile::replaceFrom(output,destination,error);
}
