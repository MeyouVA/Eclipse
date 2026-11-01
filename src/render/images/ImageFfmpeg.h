#pragma once
#include "render/images/ImageCodecProcess.h"
#include "model/scene/AtomicFile.h"
#include <QImage>
#include <QTemporaryDir>

namespace ImageCodec {
inline QString imageEncoder(QString const& suffix)
{
    if(suffix==QLatin1String("jls")) return QStringLiteral("jpegls");
    if(suffix==QLatin1String("vbn")) return QStringLiteral("vbn");
    if(suffix==QLatin1String("xface")) return QStringLiteral("xface");
    if(suffix==QLatin1String("xwd")) return QStringLiteral("xwd");
    return {};
}

inline QImage readFfmpeg(QString const& path,int frame,QString* error)
{
    QByteArray bytes;
    QStringList args{QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-i"),QFileInfo(path).absoluteFilePath()};
    if(frame>0) args << QStringLiteral("-vf") << QStringLiteral("select=eq(n\\,%1)").arg(frame);
    args << QStringLiteral("-frames:v") << QStringLiteral("1") << QStringLiteral("-c:v") << QStringLiteral("png") << QStringLiteral("-pix_fmt") << QStringLiteral("rgba64be") << QStringLiteral("-f") << QStringLiteral("image2pipe") << QStringLiteral("-");
    if(!runProgram(ThirdParty::executable(QStringLiteral("ffmpeg"),QStringLiteral("ffmpeg")),args,error,&bytes)) return {};
    QImage image=QImage::fromData(bytes,"PNG");
    if(image.isNull() && error) *error=QStringLiteral("The image decoder returned no readable picture.");
    return image;
}

inline bool writeFfmpeg(QImage const& image,QString const& path,QString const& suffix,QString* error)
{
    if(suffix==QLatin1String("xface") && image.size()!=QSize(48,48)) { if(error) *error=QStringLiteral("X-Face requires a 48 by 48 image."); return false; }
    QTemporaryDir folder;
    if(!folder.isValid()) { if(error) *error=QStringLiteral("Cannot create an image export folder."); return false; }
    QString const input=folder.filePath(QStringLiteral("source.png")),output=folder.filePath(QStringLiteral("output.")+suffix);
    if(!image.save(input,"PNG")) { if(error) *error=QStringLiteral("Cannot prepare the image pixels."); return false; }
    QStringList args{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-i"),input,QStringLiteral("-frames:v"),QStringLiteral("1"),QStringLiteral("-c:v"),imageEncoder(suffix)};
    if(suffix==QLatin1String("vbn")) args << QStringLiteral("-pix_fmt") << QStringLiteral("rgba");
    args << QStringLiteral("-f") << QStringLiteral("image2") << output;
    if(!runProgram(ThirdParty::executable(QStringLiteral("ffmpeg"),QStringLiteral("ffmpeg")),args,error)) return false;
    return AtomicFile::replaceFrom(output,path,error);
}
}
