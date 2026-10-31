#pragma once
#include "platform/ThirdParty.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace ImageCodec {

inline bool runProgram(QString const& program, QStringList const& args, QString* error, QByteArray* output = nullptr)
{
    if (program.isEmpty()) {
        if (error) *error = QStringLiteral("The bundled image codecs are missing. Run scripts/install_thirdparty.bat.");
        return false;
    }
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString const ffmpeg = ThirdParty::executable(QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg"));
    if (!ffmpeg.isEmpty()) environment.insert(QStringLiteral("PATH"), QFileInfo(ffmpeg).absolutePath() + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    process.setProcessEnvironment(environment);
    process.start(program, args);
    bool const ok = process.waitForStarted(10000) && process.waitForFinished(300000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (error->isEmpty()) *error = process.errorString();
        }
    }
    if(output) *output=process.readAllStandardOutput();
    return ok;
}

inline bool run(QStringList const& args, QString* error, QByteArray* output = nullptr)
{
    return runProgram(ThirdParty::executable(QStringLiteral("imagemagick"),QStringLiteral("magick")),args,error,output);
}

}
