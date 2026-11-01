#include "model/media/AudioDuration.h"
#include "platform/ThirdParty.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cmath>

double MediaFormats::audioDuration(QString const& path)
{
    QString const program=ThirdParty::executable(QStringLiteral("ffmpeg"),QStringLiteral("ffprobe"));
    if(program.isEmpty()) return -1;
    QProcess process;
    process.start(program,{QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-select_streams"),QStringLiteral("a:0"),QStringLiteral("-show_entries"),QStringLiteral("stream=duration:format=duration"),QStringLiteral("-of"),QStringLiteral("json"),path});
    if(!process.waitForStarted(10000) || !process.waitForFinished(10000) || process.exitCode()!=0) { process.kill(); process.waitForFinished(1000); return -1; }
    QJsonObject const info=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    QJsonArray const streams=info[QStringLiteral("streams")].toArray();
    if(streams.isEmpty()) return -1;
    bool valid=false;
    double duration=streams.first().toObject()[QStringLiteral("duration")].toString().toDouble(&valid);
    if(!valid) duration=info[QStringLiteral("format")].toObject()[QStringLiteral("duration")].toString().toDouble(&valid);
    return valid && std::isfinite(duration) && duration>=0?duration:-1;
}
