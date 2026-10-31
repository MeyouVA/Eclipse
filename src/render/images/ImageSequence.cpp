#include "render/images/ImageFiles.h"
#include "render/images/ImageCodecProcess.h"
#include "render/images/ImageApng.h"
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>

QVector<ImageFiles::Frame> ImageFiles::readSequence(QString const& path, QString* error)
{
    if(QFileInfo(path).suffix().compare(QLatin1String("apng"),Qt::CaseInsensitive)==0 || ImageCodec::isApng(path)) return ImageCodec::readApng(path,error);
    Format const* const format=find(QFileInfo(path).suffix());
    if(!format || !format->animated) {
        QImage image=load(path,error);
        return image.isNull() ? QVector<Frame>() : QVector<Frame>{{image,0}};
    }
    QImageReader reader(path);
    if(reader.supportsAnimation() && reader.imageCount()>0) {
        QVector<Frame> frames;
        for(int i=0;i<reader.imageCount();++i) {
            QImage image=reader.read();
            if(image.isNull()) { if(error) *error=reader.errorString(); return {}; }
            frames.push_back({image,reader.nextImageDelay()});
        }
        return frames;
    }
    QByteArray delays;
    QString const source=QFileInfo(path).absoluteFilePath();
    if(!ImageCodec::run({QStringLiteral("identify"),QStringLiteral("-format"),QStringLiteral("%T\n"),source},error,&delays)) return {};
    QList<QByteArray> times=delays.trimmed().split('\n');
    if(times.size()>10000) { if(error) *error=QStringLiteral("The animation has more than 10000 image frames. Import it as a movie sequence."); return {}; }
    QTemporaryDir folder;
    if(!folder.isValid()) { if(error) *error=QStringLiteral("Cannot create an animation conversion folder."); return {}; }
    if(!ImageCodec::run({source,QStringLiteral("-coalesce"),QStringLiteral("-depth"),QStringLiteral("16"),QStringLiteral("PNG64:")+folder.filePath(QStringLiteral("frame%06d.png"))},error)) return {};
    QStringList const files=QDir(folder.path()).entryList({QStringLiteral("frame*.png")},QDir::Files,QDir::Name);
    QVector<Frame> frames;
    for(qsizetype i=0;i<files.size();++i) {
        QImage image=load(folder.filePath(files[i]),error);
        if(image.isNull()) return {};
        frames.push_back({image,times.value(i).toInt()*10});
    }
    if(frames.isEmpty() && error) *error=QStringLiteral("The animation has no readable frames.");
    return frames;
}

bool ImageFiles::writeSequence(QVector<Frame> const& frames, QString const& path, QString const& format, QString* error, int loops)
{
    if(format.compare(QLatin1String("apng"),Qt::CaseInsensitive)==0) return ImageCodec::writeApng(frames,path,error,loops);
    Format const* const known=find(format);
    if(!known || !known->writable || !known->animated || frames.isEmpty()) { if(error) *error=QStringLiteral("Unsupported or empty image animation."); return false; }
    QTemporaryDir folder;
    if(!folder.isValid()) { if(error) *error=QStringLiteral("Cannot create an animation conversion folder."); return false; }
    QStringList args;
    QSize const size=frames.first().image.size();
    for(qsizetype i=0;i<frames.size();++i) {
        if(frames[i].image.isNull() || frames[i].image.size()!=size) { if(error) *error=QStringLiteral("Animation frames must have the same nonempty size."); return false; }
        QString const file=folder.filePath(QStringLiteral("frame%1.png").arg(i,6,10,QLatin1Char('0')));
        if(!frames[i].image.save(file,"PNG")) { if(error) *error=QStringLiteral("Cannot write an animation frame."); return false; }
        args << QStringLiteral("-delay") << QString::number(std::max(1,frames[i].delayMs))+QStringLiteral("x1000") << file;
    }
    QString const output=folder.filePath(QStringLiteral("animation."))+known->suffix;
    args << QStringLiteral("-loop") << QString::number(std::max(0,loops));
    if(known->suffix==QLatin1String("webp")) args << QStringLiteral("-define") << QStringLiteral("webp:lossless=true");
    args << known->suffix+QLatin1Char(':')+output;
    if(!ImageCodec::run(args,error)) return false;
    QFile input(output);
    QSaveFile file(path);
    if(!input.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly)) { if(error) *error=QStringLiteral("Cannot open animation output."); return false; }
    while(!input.atEnd()) {
        QByteArray const block=input.read(1024*1024);
        if(block.isEmpty() || file.write(block)!=block.size()) { if(error) *error=QStringLiteral("Cannot write animation output."); return false; }
    }
    bool const ok=file.commit();
    if(!ok && error) *error=file.errorString();
    return ok;
}
